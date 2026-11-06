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

O componente tem fixture ISO sintética e testes host. Ele é a **fonte do instalador seletivo**. A implementação inclui enumeração imediata de diretórios e leitura de arquivos por offset; a extração de boot inicial está documentada em [IMPORTACAO_ISO.md](IMPORTACAO_ISO.md).

## 2. Limitações atuais

A primeira versão não aceita:
- imagens raw de CD com setores de 2352 bytes;
- CHD, CSO ou formatos compactados;
- extensões Joliet ou Rock Ridge;
- arquivos ISO9660 multi-extent.

A imagem selecionada precisa ser uma ISO9660 compatível com o parser. O reconhecimento do PVD não prova que a revisão do jogo é suportada nem que todos os arquivos necessários foram instalados.

## 3. Decisão de arquitetura: instalação por extração seletiva, inspirada no Dragon Rage BT3

A referência é [Dragon Rage / Tenkaichi3DecompAndroid](https://github.com/dfdx047/Tenkaichi3DecompAndroid). Seu fluxo BT3 valida a imagem e copia o executável e diretórios de dados selecionados para a área privada do aplicativo. Os arquivos AFS são mantidos como arquivos inteiros, em vez de serem desmembrados em milhares de arquivos menores.

**Decisão para Gow_android:** adotar esse modelo de instalação. A ISO será usada como fonte de importação; depois que a instalação terminar e for validada, o jogo deverá ler os arquivos preparados no armazenamento privado do aplicativo, e não depender de acesso aleatório contínuo à ISO via SAF durante a partida.

Não vamos copiar cegamente a lista de caminhos do Dragon Rage. God of War tem sua própria estrutura, e a lista de arquivos será determinada pela revisão suportada e pelo rastreamento dos acessos de boot/runtime.

### Fluxo de instalação planejado

1. **Selecionar e validar:** o launcher recebe a URI pelo SAF, verifica PVD ISO9660, `SYSTEM.CNF`, serial/região, revisão e identidade do ELF.
2. **Planejar espaço:** enumerar os arquivos que pertencem ao manifesto de instalação, calcular o tamanho necessário e conferir o espaço disponível antes de começar.
3. **Extrair para staging:** copiar em blocos para uma pasta temporária privada, mantendo nomes, diretórios e arquivos de dados originais. O ELF deve ficar no caminho local esperado pelo loader.
4. **Preservar arquivos grandes:** manter pacotes/arquivos de dados em seus formatos originais. Não expandir archives em milhares de arquivos pequenos nem carregar todo o conteúdo para a RAM.
5. **Verificar:** conferir tamanho e, quando houver hash de referência confiável, hash dos arquivos copiados. Um manifesto incompleto ou um erro de leitura impede a publicação da instalação.
6. **Publicar atomicamente:** somente depois da extração e validação, promover a pasta temporária para a pasta versionada da instalação. Cancelamentos não podem deixar uma instalação parcial marcada como pronta.
7. **Executar localmente:** configurar o VFS/CD root para a pasta instalada. A sessão não deve precisar manter um descritor da ISO aberto.
8. **Reparar/reinstalar:** se arquivos locais estiverem ausentes ou corrompidos, solicitar a ISO novamente e refazer a validação antes de reparar.

O identificador do cache deve considerar título, serial/revisão, versão do instalador e hash do manifesto. A instalação não pode ser aceita apenas porque existe um marcador antigo.

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
- `openFile()` usa `openat` e `O_NOFOLLOW`, rejeita caminhos absolutos, vazios, traversal e componentes inválidos;
- `readFile()` faz leitura aleatória por `pread`, com handles explícitos e comportamento de EOF testado;
- a API C ABI está em `android_cdroot_bridge.h`, e a ponte JNI `MainActivity.nativeSetCdRootDirectory()` abre a pasta preparada.

Os testes `cdroot_vfs_test.cpp` cobrem raiz e subdiretórios, listagens repetidas, leituras parciais/EOF, handles fechados, traversal, nomes inválidos e symlinks. O erro encontrado na listagem repetida foi corrigido: a raiz agora é aberta com `openat(rootFd, ".", ...)`, evitando compartilhar o offset de diretório do descritor persistente.

## 5. Provider local preparado na integração Waffle

O script `tools/build_recompiler_host.py` foi alterado para que a árvore Waffle/KIexter preparada para Android use o prefixo `gowcdroot:/` no PS2 VFS e no adaptador IOP. As chamadas passam a ser:

- `gowAndroidCdRootOpenFile`;
- `gowAndroidCdRootReadFile`;
- `gowAndroidCdRootCloseFile`;
- `gowAndroidCdRootIsReady`.

O runtime define `paths.cdRoot` para `gowcdroot:/` quando o provider privado está aberto. O código antigo de patch que roteava `gowdisc:/` para o leitor ISO foi substituído nessa preparação. O bridge ISO continua presente como leitor/importador, mas deixa de ser o provider previsto para gameplay.

**Limite:** o patch só modifica o runtime privado em `android/app/build/gow-runtime-source` durante a preparação. Isso ainda não significa que o PS2Runtime está sendo iniciado pela Activity nem que os serviços do jogo estejam abrindo arquivos pelo VFS durante um boot real. A execução e a CI precisam validar que as âncoras do patch ainda correspondem à revisão fixada.

## 6. Próximas etapas

1. Validar a preparação e compilação Waffle com o provider `gowcdroot:/` e com o novo VFS incluído.
2. Ligar o lifecycle do `PS2Runtime` à GameActivity e definir a ordem correta entre montagem de `cdRoot`, carregamento do ELF e inicialização dos serviços PS2.
3. Derivar o manifesto completo de God of War a partir da árvore real da ISO e de rastreamento de boot/runtime; não presumir caminhos.
4. Preparar o ELF privado no caminho usado por `PS2Runtime::loadELF()` e executar a geração C++ com PS2Recomp no workspace privado.
5. Gerar e integrar cobertura AOT de VU1/IRX para os módulos realmente usados, sem fallback interpretado.
6. Integrar GS/Vulkan e o presenter Android para alcançar a primeira imagem.
7. Validar instalação, reparo, cancelamento, falta de espaço, boot, menu e primeiro combate com imagem do usuário em aparelho real.
8. Medir I/O, memória, frame time e temperatura antes de ajustar cache/prefetch ou resolução.

## 6. Privacidade e integridade

ISO, ELF, IRX, arquivos extraídos, saves e C++ gerado a partir do executável permanecem fora do Git, Issues, releases e CI pública. A CI usa somente fixtures sintéticas sem dados proprietários.

## Referências do Dragon Rage

- [IsoImage.kt](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/android/app/src/main/java/com/dfdx047/dragonrage/data/IsoImage.kt): PVD, árvore ISO9660 e leitura aleatória.
- [GameDataInstaller.kt](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/android/app/src/main/java/com/dfdx047/dragonrage/data/GameDataInstaller.kt): validação, extração seletiva, progresso e verificação.
- [Bt4Installer.kt](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/android/app/src/main/java/com/dfdx047/dragonrage/data/Bt4Installer.kt): referência do modo híbrido, que não será o caminho principal escolhido para Gow_android.
