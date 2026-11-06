# Leitura de disco e instalação no Android

**Revisão:** 2026-11-06  
**Base runtime:** Waffle/KIexter `49ca62c1d20b4659c61df6286c54e86edf411012`

## 1. Leitor ISO9660 disponível

O módulo `android/app/src/main/cpp/iso9660_reader.h/.cpp` implementa leitura aleatória e enumeração de diretórios em imagens ISO9660 com setores de 2048 bytes.

- Recebe um descritor aberto pelo Storage Access Framework e duplica o FD.
- Verifica o descritor primário ISO9660 no setor 16 e o registro do diretório raiz.
- Resolve arquivos por diretórios e extents, sem carregar a imagem inteira na RAM.
- Normaliza nomes sem diferenciar maiúsculas/minúsculas e aceita sufixos de versão como `;1`.
- Lê intervalos por offset, inclusive atravessando limites de setor.
- Valida limites da imagem, registros, tamanho de diretórios e componentes de caminho; rejeita traversal, diretório usado como arquivo e extents fora da imagem.

O componente tem fixture ISO sintética e testes host. Ele é a **fonte do instalador completo atual**. A implementação também enumera recursivamente os diretórios das duas camadas e produz um layout com todos os arquivos regulares ISO9660 e seus LSNs originais. A extração de boot inicial documentada em [IMPORTACAO_ISO.md](IMPORTACAO_ISO.md) foi substituída no fluxo do FORGE pela instalação completa descrita abaixo.

## 2. Limitações atuais

A primeira versão não aceita:
- imagens raw de CD com setores de 2352 bytes;
- CHD, CSO ou formatos compactados;
- extensões Joliet ou Rock Ridge;
- arquivos ISO9660 multi-extent.

A imagem selecionada precisa ser uma ISO9660 compatível com o parser. O reconhecimento do PVD não prova que a revisão do jogo é suportada nem que todos os arquivos necessários foram instalados.

## 3. Decisão de arquitetura: instalação privada inspirada no Dragon Rage BT3

A referência é [Dragon Rage / Tenkaichi3DecompAndroid](https://github.com/dfdx047/Tenkaichi3DecompAndroid). Seu fluxo BT3 valida a imagem e copia o executável e diretórios de dados selecionados para a área privada do aplicativo. Os arquivos AFS são mantidos como arquivos inteiros, em vez de serem desmembrados em milhares de arquivos menores.

**Decisão para Gow_android:** adotar a instalação privada, fora do loop do jogo. A primeira implementação copia todos os arquivos regulares encontrados na árvore ISO9660 suportada para preservar os extents de dados necessários à reconstrução de qualquer leitura por LSN. Arquivos grandes permanecem nos formatos originais; não são descompactados em árvores artificiais.

Esta primeira versão não é uma extração seletiva otimizada. O manifesto é derivado da própria imagem escolhida, nunca de caminhos presumidos de outro jogo. Reduzir o conjunto extraído exigirá uma estratégia explícita para continuar atendendo os setores omitidos e evidência de acessos reais durante o boot.

### Fluxo de instalação planejado

1. **Selecionar e validar:** o launcher recebe a URI pelo SAF, verifica PVD ISO9660, `SYSTEM.CNF`, serial/região, revisão e identidade do ELF.
2. **Planejar espaço:** enumerar os arquivos que pertencem ao manifesto de instalação, calcular o tamanho necessário e conferir o espaço disponível antes de começar.
3. **Extrair para staging:** copiar em blocos para uma pasta temporária privada, mantendo nomes, diretórios e arquivos de dados originais. O ELF deve ficar no caminho local esperado pelo loader.
4. **Preservar arquivos grandes:** manter pacotes/arquivos de dados em seus formatos originais. Não expandir archives em milhares de arquivos pequenos nem carregar todo o conteúdo para a RAM.
5. **Verificar:** conferir tamanho e, quando houver hash de referência confiável, hash dos arquivos copiados. Um manifesto incompleto ou um erro de leitura impede a publicação da instalação.
6. **Publicar atomicamente:** somente depois da extração e validação, promover a pasta temporária para a pasta versionada da instalação. Cancelamentos não podem deixar uma instalação parcial marcada como pronta.
7. **Executar localmente:** configurar o VFS/CD root para a pasta instalada. A sessão não deve precisar manter um descritor da ISO aberto.
8. **Reparar/reinstalar:** se arquivos locais estiverem ausentes ou corrompidos, solicitar a ISO novamente e refazer a validação antes de reparar.

O identificador da instalação usa o serial/região reconhecidos para não misturar os dois alvos suportados. A validação atual confere a presença do par de metadados e o ELF local; ainda falta um manifesto/hash de integridade persistente para detectar corrupção ou substituição de arquivos após a instalação.

### Regras de desempenho para aparelhos modestos

- A extração acontece fora do loop do jogo, com progresso, tratamento de falta de espaço e cancelamento seguro.
- O instalador usa buffers limitados e não mantém a ISO nem os arquivos extraídos inteiros na memória.
- Arquivos grandes continuam como arquivos grandes; só serão convertidos ou divididos quando existir necessidade técnica comprovada.
- Durante a partida, o VFS lê do armazenamento privado sob demanda. Qualquer cache será limitado, configurável e medido; não haverá preload indiscriminado de toda a árvore.
- Prefetch assíncrono só será adicionado para padrões de leitura identificados por profiling. Não vamos presumir que mais cache sempre melhora aparelhos com pouca RAM.
- O primeiro objetivo é remover SAF/ISO do caminho crítico de leitura durante o jogo. Isso melhora a previsibilidade do I/O, mas não garante, sozinho, maior FPS.

## 4. Provider local `cdRoot` implementado

O módulo `android/app/src/main/cpp/cdroot_vfs.h/.cpp` fornece leitura somente de arquivos regulares dentro de uma raiz privada do aplicativo:

- `openRoot()` fixa o descritor da pasta privada, sem seguir symlink no componente final;
- `listDirectory()` cria fluxos de diretório independentes e limita a listagem a 65.536 entradas;
- `openFile()` usa `openat` e `O_NOFOLLOW`, rejeita caminhos absolutos, vazios, traversal e componentes inválidos; a resolução de componentes segue ISO9660 (ASCII case-insensitive e sufixo opcional `;versão` ignorado), rejeitando nomes locais ambíguos após normalização;
- `readFile()` faz leitura aleatória por `pread`, com handles explícitos e comportamento de EOF testado;
- a API C ABI está em `android_cdroot_bridge.h`, e a ponte JNI `MainActivity.nativeSetCdRootDirectory()` abre a pasta preparada.

Os testes `cdroot_vfs_test.cpp` cobrem raiz e subdiretórios, listagens repetidas, leituras parciais/EOF, handles fechados, traversal, nomes inválidos e symlinks. O erro encontrado na listagem repetida foi corrigido: a raiz agora é aberta com `openat(rootFd, ".", ...)`, evitando compartilhar o offset de diretório do descritor persistente.

## 5. Integração Android com o runtime Waffle/KIexter

A integração agora separa o caminho físico da instalação do caminho lógico usado pelo dispositivo PS2.

### VFS por caminho

- `PS2Vfs::open()` e `PS2Vfs::stat()` reconhecem `cdrom0:` a partir de `ParsedPs2Path`, antes de transformar a rota em um caminho do host.
- As operações de arquivo são encaminhadas ao `CdRootVfs` seguro por meio de `gowAndroidCdRootOpenFile`, `gowAndroidCdRootReadFile`, `gowAndroidCdRootCloseFile` e `gowAndroidCdRootStat`.
- A abertura continua somente para leitura. Escritas, criação e truncamento no disco de jogo são rejeitados.

### Caminho físico para a fonte de setores

O runtime mantém `IoPaths.cdRoot` como o caminho físico absoluto da pasta privada de instalação. A ponte JNI guarda a raiz recebida por `MainActivity.nativeSetCdRootDirectory()` e a nova função C ABI `gowAndroidCdRootCopyPath()` a entrega ao runtime.

Isso é necessário porque `PS2Runtime::setIoPaths()` normaliza os caminhos com `std::filesystem::absolute()`. Um token como `gowcdroot:/` não deve ser passado como se fosse um caminho físico: ele pode ser normalizado como um diretório comum do host e deixar de ser reconhecido.

O contrato do runtime Waffle/KIexter também inclui `ps2x::iop::DiscSource::openExtracted(root)`. Esse componente abre `.disc_layout` e `.disc_meta` na pasta física, localiza arquivos por seu LSN e atende leituras de setores. Portanto, a pasta entregue em `IoPaths.cdRoot` precisa conter o layout completo produzido pelo extrator de disco, não apenas `SYSTEM.CNF` e o ELF BOOT2.

### Limite atual

**[VERIFICADO NA CI ANDROID, run `42225710064`]** O JNI `nativeInstallIsoDisc()` chama o instalador completo. O instalador produz `.disc_layout` e `.disc_meta` em `disc/` e só publica a instalação depois da geração dos dois arquivos. Após a publicação, o FORGE monta o `cdRoot`, configura os caminhos estáticos do `PS2Runtime`, valida o ELF local e abre a pasta com a implementação real de `ps2x::iop::DiscSource::openExtracted()`.

**[VERIFICADO NO HOST, run `42226830317`]** Um teste compila os fontes reais de `IopCdvd`, `IopMemory` e `IopKernel`, injeta apenas um `IopHost` de fixture e chama `IopCdvd::dispatchImport()`: ordinal 10 (`sceCdSearchFile`) encontra um arquivo aninhado e devolve LSN/tamanho; ordinal 6 (`sceCdRead`) copia setores das duas camadas para a RAM IOP; uma leitura além do fim do disco é recusada.

**[COMPILADO NA CI ANDROID, run `42227233881`]** O FORGE chama `nativeValidateIopCdvd()` após a instalação. A ponte C ABI prepara uma instância isolada do dispatcher CDVD apontada para a raiz privada, procura BOOT2 e lê seu primeiro setor na RAM IOP para conferir a assinatura ELF. O build ARM64 e o APK Debug foram aprovados e publicados. O probe ainda não foi executado em aparelho.

**[NÃO IMPLEMENTADO]** As verificações anteriores não inicializam `PS2Runtime`, não chamam `loadELF()`/`run()` e não exercitam o dispatcher CDVD que pertence a uma sessão real. O runtime base ainda depende da inicialização host Raylib para gráficos/áudio; a Activity Android não está conectada a um runner PS2 jogável. Nenhuma ISO real, ELF real ou sequência de boot foi testada nesta etapa.

A auditoria do contrato de referência e o formato dos metadados estão em [DISC_RUNTIME_AUDIT.md](DISC_RUNTIME_AUDIT.md).

## 6. Próximas etapas

1. Validar em aparelho a instalação e os probes `IoPaths`/DiscSource/CDVD com a ISO do usuário, sem compartilhar a imagem ou os dados extraídos.
2. Adicionar verificação de integridade persistente e recuperação/reinstalação por serial/revisão.
3. Disponibilizar os IRX no layout esperado e instrumentar os caminhos de arquivo e CDVD usados durante o boot.
4. Conectar o serviço CDVD ao IOP vivo do runtime e verificar chamadas `sceCdRead` originadas pelo código do jogo; os fixtures atuais exercitam o dispatcher em instância isolada.
5. Adaptar a inicialização do PS2Runtime para GameActivity, separando GS, áudio e serviços que hoje dependem de Raylib.
6. Gerar/compilar o C++ privado do ELF e ligar `loadELF()` ao runner Android.
7. Integrar GS/Vulkan e apresentar a primeira imagem real.

## 6. Privacidade e integridade

ISO, ELF, IRX, arquivos extraídos, saves e C++ gerado a partir do executável permanecem fora do Git, Issues, releases e CI pública. A CI usa somente fixtures sintéticas sem dados proprietários.

## Referências do Dragon Rage

- [IsoImage.kt](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/android/app/src/main/java/com/dfdx047/dragonrage/data/IsoImage.kt): PVD, árvore ISO9660 e leitura aleatória.
- [GameDataInstaller.kt](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/android/app/src/main/java/com/dfdx047/dragonrage/data/GameDataInstaller.kt): validação, extração seletiva, progresso e verificação.
- [Bt4Installer.kt](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/android/app/src/main/java/com/dfdx047/dragonrage/data/Bt4Installer.kt): referência do modo híbrido, que não será o caminho principal escolhido para Gow_android.
