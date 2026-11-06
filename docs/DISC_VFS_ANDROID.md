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
5. **Verificar:** calcular SHA-256 incremental de cada arquivo ISO enquanto é copiado. Depois da criação de `.disc_layout` e `.disc_meta`, calcular também seus hashes e gravar `.gow_disc_integrity`. O manifesto é sincronizado antes da publicação; um erro impede a instalação atômica.
6. **Publicar atomicamente:** somente depois da extração e validação, promover a pasta temporária para a pasta versionada da instalação. Cancelamentos não podem deixar uma instalação parcial marcada como pronta.
7. **Executar localmente:** configurar o VFS/CD root para a pasta instalada. A sessão não deve precisar manter um descritor da ISO aberto.
8. **Reparar/reinstalar:** verificar `.gow_disc_integrity` quando uma instalação existente for revalidada. Se o manifesto ou qualquer arquivo estiver ausente, alterado ou substituído por symlink, solicitar a ISO novamente e refazer a validação antes de oferecer a reinstalação confirmada.

O identificador da instalação usa o serial/região reconhecidos para não misturar os dois alvos suportados. O manifesto `.gow_disc_integrity` valida os arquivos extraídos e o par de metadados. A duração da verificação integral e o I/O em armazenamento Android real ainda precisam ser medidos.

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

**[COMPILADO NA CI ANDROID, run `42227233881`]** O FORGE chama `nativeValidateIopCdvd()` após a instalação. A ponte C ABI prepara uma instância isolada do dispatcher CDVD apontada para a raiz privada, procura BOOT2 e lê seu primeiro setor na RAM IOP para conferir a assinatura ELF. O build ARM64 e o APK Debug foram aprovados e publicados. O probe ainda não foi confirmado em aparelho.

**[VERIFICADO NA CI, run `42232085986`]** `tools/run_ps2_vfs_cdroot_smoke.py` compila o `ps2_vfs.cpp` Android efetivamente preparado, junto de `CdRootVfs`, `ps2_rom_device.cpp` e `ps2_path.cpp`. Com uma instalação sintética, verifica `cdrom0:` open/read/seek, leitura aleatória, `stat` de arquivo e diretório, recusa de escrita/traversal e fechamento do descriptor. O mesmo run aprovou a compilação do APK Debug ARM64 e publicou os artefatos.

**[AUDITORIA DA REVISÃO WAFFLE FIXADA]** `IopEmulator::Impl` já possui uma instância real de `IopCdvd` e despacha imports da biblioteca `cdvdman` para `IopCdvd::dispatchImport()`. O `PS2IopHostAdapter` entrega `IoPaths.cdRoot` como `HostPathKind::CdRoot`, e o CDVD abre `DiscSource::openExtracted()` a partir dessa raiz. Portanto, a conexão interna entre o IOP do runtime e o serviço CDVD já existe na base; falta exercitá-la durante uma sessão iniciada no Android, não recriar um segundo dispatcher.

**[VERIFICADO NA CI, runs `42237906079` e `42240082689`] Integridade SHA-256:** o instalador hashia cada arquivo ISO durante a cópia, hashia `.disc_layout` e `.disc_meta` depois da geração e grava `.gow_disc_integrity` na raiz do `cdRoot` antes da publicação atômica. `verifyIsoDiscIntegrity()` lê e valida caminhos seguros sem seguir symlinks, confere tamanho e SHA-256 em blocos de 64 KiB. A revalidação de instalações existentes ocorre no executor de I/O, fora da thread principal; um callback informa bytes/arquivos, atualiza o progresso e permite cancelar. O callback é chamado no máximo a cada 1 MiB ou após cada arquivo; cancelamento retorna estado separado e nunca `valid=true`. Uma falha de integridade bloqueia o uso e oferece reparo confirmado. Fixtures host verificam vetor conhecido, sucesso, corrupção de mesmo tamanho, arquivo ausente, symlink e cancelamento parcial. A CI `42240082689` passou nos testes host e VFS, compilou o APK ARM64 e publicou os artefatos. Tempo total e custo de I/O em armazenamento Android real ainda precisam ser medidos.

**[VERIFICADO NA CI, run `42235192717`] Reparo da instalação:** quando a validação de uma instalação existente falha, o FORGE oferece uma reinstalação com confirmação explícita. A imagem é reaberta e sua identidade é comparada antes de remover a pasta anterior. O JNI fecha o provider `cdRoot` e chama `removeIsoDiscInstallation()`, que aceita somente um componente de nome seguro no diretório privado `installations`, rejeita um caminho final que seja symlink e remove conteúdo por descritores de diretório com `openat`/`O_NOFOLLOW`. Os testes sintéticos confirmam que symlinks internos são removidos sem seguir o destino, caminhos com traversal são recusados e arquivos irmãos permanecem intactos. A CI passou pelos testes host, compilou o APK Debug ARM64 e publicou o artefato. O uso da tela de reparo ainda precisa ser testado em aparelho.

**[VERIFICADO NA CI, run `42243707680`] Núcleo PS2 headless:** `initializeCoreHeadless()` foi integrado ao runtime Waffle preparado e é chamado a partir de `android_main()` pelo novo C ABI. A etapa inicializa a memória e sincroniza EE/IOP/VU sem janela, captura de contexto, áudio Raylib ou execução guest. O perfil Android usa `GSNullBackend`; `initialize()` desktop está bloqueado, `loadELF()` exige núcleo inicializado e `run()` permanece desabilitado até existir apresentação GS/áudio Android e código AOT privado. A CI passou pela preparação da árvore, verificador estático, testes host/VFS e build ARM64. Isso ainda não comprova que a inicialização funciona em um aparelho real.

**[NÃO IMPLEMENTADO]** O runtime ainda não chama `loadELF()` nem executa guest; o CDVD exercitado até agora é probe isolado ou teste sintético, não a sessão do jogo. O backend GS/Vulkan e o áudio Android ainda não estão conectados. Nenhuma ISO real, ELF real ou sequência de boot foi testada nesta etapa.

A auditoria do contrato de referência e o formato dos metadados estão em [DISC_RUNTIME_AUDIT.md](DISC_RUNTIME_AUDIT.md).

## 6. Próximas etapas

1. Confirmar em aparelho o resultado integral dos probes `IoPaths`/DiscSource/CDVD com a ISO do usuário, sem compartilhar a imagem ou os dados extraídos.
2. Medir em aparelho o tempo, o I/O e o impacto térmico da verificação SHA-256 integral; qualquer cache futuro precisará de invalidação segura sem deixar de detectar corrupção.
3. Disponibilizar os IRX no layout esperado e instrumentar os caminhos de arquivo e CDVD usados durante o boot.
4. Iniciar uma sessão real do runtime e confirmar chamadas `sceCdRead` originadas pelo código do jogo; o dispatcher `IopCdvd` já é chamado pelo `IopEmulator`, mas ainda não há execução de guest.
5. Implementar e conectar os adaptadores GS/Vulkan e áudio Android ao núcleo headless; depois ligar `loadELF()` e a execução guest quando os módulos e o AOT estiverem disponíveis.
6. Gerar/compilar o C++ privado do ELF e ligar `loadELF()` ao runner Android.
7. Integrar GS/Vulkan e apresentar a primeira imagem real.

## 6. Privacidade e integridade

ISO, ELF, IRX, arquivos extraídos, saves e C++ gerado a partir do executável permanecem fora do Git, Issues, releases e CI pública. A CI usa somente fixtures sintéticas sem dados proprietários.

## Referências do Dragon Rage

- [IsoImage.kt](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/android/app/src/main/java/com/dfdx047/dragonrage/data/IsoImage.kt): PVD, árvore ISO9660 e leitura aleatória.
- [GameDataInstaller.kt](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/android/app/src/main/java/com/dfdx047/dragonrage/data/GameDataInstaller.kt): validação, extração seletiva, progresso e verificação.
- [Bt4Installer.kt](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/android/app/src/main/java/com/dfdx047/dragonrage/data/Bt4Installer.kt): referência do modo híbrido, que não será o caminho principal escolhido para Gow_android.
