# Estado do projeto

**Data da auditoria:** 2026-11-06  
**Branch de desenvolvimento:** `main`  
**Objetivo:** port nativo de God of War (PS2, 2005) para Android ARM64, usando recompilação estática e runtime de serviços PS2 nativo; não é um emulador.

**Fluxo de entrada vigente (07/11/2026):** a única origem de jogo é a ISO original NTSC-U. O host pode receber essa ISO por `tools/build_android_game.py --iso` ou ler `SYSTEM.CNF` e BOOT2 diretamente da instalação privada Android por `--installed-android`. O host valida o executável e gera o C++ AOT EE em workspace privado; também tenta sincronizar as capturas VU1/IOP privadas do aparelho. O FORGE recupera a instalação por SYSTEM.CNF mesmo se o URI original da ISO não estiver mais salvo. Para substituir o APK Debug do CI (assinatura temporária), a opção explícita `--migrate-installed-debug-signature` cria e valida um backup SHA-256 da instalação e do APK antigo antes de reinstalar o app e restaurar os arquivos; o backup permanece no workspace e há tentativa de rollback automático. Os testes sintéticos de validação do backup passaram após correções; a transferência/migração real em aparelho ainda não foi executada. **Nenhum boot ou gameplay real foi comprovado.**

**Diagnóstico privado:** o runtime grava `files/gow-runtime.log` com limite de 2 MiB e proteção contra symlink. O arquivo não possui botão de exportação no FORGE e permanece no armazenamento privado. O teste C++ de limite e proteção contra symlink passou na CI Android #411/#412.

## [DECISÃO ARQUITETURAL] Instalação BT3

- A ISO será fonte de importação; o runtime deverá ler do `cdRoot` local depois que a instalação estiver validada.
- O primeiro instalador completo enumera e extrai todos os arquivos regulares ISO9660 para preservar cada extent original usado pelas leituras por LSN. Não presume que um subconjunto de arquivos seja suficiente.
- Arquivos/pacotes grandes serão mantidos nos formatos originais. Não haverá preload de toda a instalação na RAM.
- Reduzir a instalação a um manifesto seletivo só será considerado após rastrear os acessos reais do jogo e comprovar que os setores omitidos continuam disponíveis ao `DiscSource`; não serão presumidos caminhos do Dragon Rage.
- O leitor ISO e a ponte híbrida já compilada serão reaproveitados onde fizer sentido, mas o provider que mantém a ISO aberta durante o jogo é transitório e não define a arquitetura final.
- O objetivo de desempenho em aparelhos modestos será avaliado por frame time, 1% low, memória, I/O e temperatura em hardware real. A instalação BT3, isoladamente, não comprova aumento de FPS.

## Prioridade atual

**Fazer God of War funcionar vem antes de otimização fina.** O projeto ainda não comprovou boot real nem gameplay. O foco é gerar e integrar o código do jogo, completar runtime/IOP/VU1, GS/Vulkan, serviços de boot e alcançar primeira imagem, menu e primeiro combate. O tuning para aparelhos modestos fica para depois dessa linha de base.

**Referência futura de otimização:** [Black for Android](https://github.com/RanggaCasper/Black-Android), documentada em [ANDROID_LOW_END_OPTIMIZATION.md](ANDROID_LOW_END_OPTIMIZATION.md). Não estamos adotando agora suas opções nem assumindo equivalência entre Xbox e PS2.

## Auditoria da origem de disco (06/11/2026)

**[VERIFICADO NA REFERÊNCIA KIEXSTER/WAFFLE]** O runtime de PC contém `ps2x::iop::DiscSource`, com `readSectors(lsn, count, destination)`, `findFile()` e `layer1Start()`. O extrator `tools/iso_extract.py --layout` produz `.disc_layout` (tamanho total, início da segunda camada e LSN/tamanho/caminho de cada arquivo) e `.disc_meta` (setores não pertencentes a arquivos, cada registro com LSN + 2048 bytes). O contrato é descrito em [DISC_RUNTIME_AUDIT.md](DISC_RUNTIME_AUDIT.md).

**[VERIFICADO NA REFERÊNCIA DO JOGO]** A documentação de KIexster explica que `SMPD_IOP.IRX` carrega dados consultando setores com `sceCdRead`; `GODOFWAR.TOC` e os pacotes `PART*.PAK` dependem de LSNs originais. O mesmo port disponibiliza os módulos IRX no diretório `IOP_MOD/` porque o jogo os solicita por caminhos relativos.

**[VERIFICADO NA CI ANDROID, run `42225710064`]** O novo `installIsoDisc()` enumera os arquivos ISO9660 nas duas camadas, extrai `disc/<caminho-original>` em staging, gera `.disc_layout` e `.disc_meta` antes de publicar e limpa o staging em caso de falha/cancelamento. O FORGE usa esse fluxo completo. A CI também compila a ponte JNI que chama a implementação real de `DiscSource` e a configuração de `PS2Runtime::IoPaths`. A validação usa apenas ISO sintética; ainda não há teste com ISO real em aparelho nem boot/gameplay.

**[PONTE COMPILADA NA CI ANDROID, run `42225710064`]** O runtime recebe a raiz privada como caminho físico, não como token virtual. A função C ABI/JNI chama `PS2Runtime::configureIoPathsFromElf()` — o mesmo ponto usado por `loadELF()` — e verifica `IoPaths.cdRoot`, `.disc_layout`, `.disc_meta` e `IoPaths.elfPath` quando é acionada após a instalação. A CI comprova compilação/link da ponte; não invoca essa validação com uma instalação real nem chama `PS2Runtime::initialize()`, `loadELF()` ou `run()`.

**[VERIFICADO NO HOST E NA CI ANDROID, runs `42218565639` e `42225710064`] Gerador e integração de metadados:** fixtures ISO sintéticas verificam o conteúdo de `.disc_layout`, os registros de `.disc_meta` (LSN little-endian + setor original), arquivos ausentes/tamanho errado/symlink, layout divergente e limpeza de temporários. O instalador mede o espaço, permite cancelamento durante análise/geração e publica os dois metadados dentro do staging antes do rename final. O teste de integração compila a implementação real de `ps2x::iop::DiscSource` do Waffle e valida `openExtracted()`, `readSectors()`, `findFile()` e `layer1Start()` numa ISO sintética de dupla camada.

**Evidência do aparelho (06/11/2026):** as capturas enviadas mostram a extração ISO9660 concluída, `.disc_layout`/`.disc_meta` publicados e os caminhos `IoPaths`/ELF reconhecidos pelo FORGE. Isso confirma a instalação nessa execução, mas não comprova boot ou gameplay. **Smoke automatizado:** a CI `42232085986` passou pelo `PS2Vfs` real via provider `cdRoot` Android. **Reparo seguro:** a CI `42235192717` aprovou a remoção limitada ao diretório da instalação, com rejeição de symlinks no topo, remoção de symlinks internos sem seguir destinos, bloqueio de traversal e preservação de arquivos irmãos. **Integridade SHA-256:** a CI `42237906079` passou pelo vetor conhecido `abc`, instalação sintética em DVD de dupla camada, detecção de corrupção com tamanho preservado, arquivo ausente e symlink. **Progresso e cancelamento:** a CI `42240082689` aprovou a verificação fora da thread principal, callback de progresso, cancelamento após arquivo parcial, rejeição de resultado cancelado, smoke VFS, compilação ARM64 e publicação do APK Debug. **Núcleo PS2 headless:** a CI `42243707680` aprovou a preparação da árvore Waffle, o contrato `initializeCoreHeadless()`, os testes nativos, smoke VFS, compilação ARM64 e publicação de novo APK. A GameActivity agora solicita inicialização da memória e EE/IOP/VU sem janela/áudio Raylib; o GS fica em backend nulo e `run()` continua bloqueado. O APK e o ciclo de inicialização ainda não foram executados em aparelho. **Próxima prioridade:** instalar esse APK, conferir o log da inicialização headless e medir estabilidade/memória no aparelho; depois implementar GS/Vulkan e áudio Android. Nenhuma ISO, ELF, IRX ou arquivo extraído deve ser enviado ao GitHub.

## [VERIFICADO NA CI, run `42235192717`] Reparo seguro da instalação privada

- A validação de uma instalação existente agora pode oferecer reinstalação após falha, com uma segunda confirmação destrutiva explícita.
- O app reabre a imagem e confirma novamente serial e `BOOT2` antes de remover a instalação anterior.
- `removeIsoDiscInstallation()` aceita somente um nome simples de diretório dentro de `files/installations`; abre a raiz e os diretórios com `O_NOFOLLOW`, não atravessa symlinks e remove apenas o filho indicado. O provider `cdRoot` é fechado antes da remoção.
- Fixtures host verificam symlink dentro da instalação, symlink no próprio caminho de instalação, traversal e preservação de arquivos irmãos. A CI `42235192717` passou em todos os testes host, compilou o APK Debug ARM64 e publicou `gow-android-arm64-shell`.
- A reinstalação ainda não foi validada no aparelho. Não há alteração no estado de boot: o runtime de gameplay continua sem inicialização Android.

## [VERIFICADO NA CI, run `42243707680`] Separação do núcleo PS2 e ciclo host Android

- `PS2Runtime` agora expõe `initializeCoreHeadless()`. No perfil Android, a etapa inicializa a memória PS2 e chama `syncCoreSubsystems()` para ligar EE/IOP/VU sem criar janela Raylib, abrir áudio, capturar contexto gráfico ou executar instruções do guest.
- `GS::GS()` seleciona `GSNullBackend` somente no perfil Android gerenciado; desktop mantém `GSThreadedBackend::MakeDefault()`.
- `android_main()` chama a nova ponte C ABI de inicialização e registra diagnóstico de sucesso/falha. O destrutor evita consultar/fechar o dispositivo Raylib que não foi aberto no Android.
- `PS2Runtime::initialize()` desktop retorna bloqueado no perfil Android; `loadELF()` exige o núcleo inicializado e `run()` continua bloqueado até GS/áudio Android e o código AOT privado estarem disponíveis.
- O script `tools/build_recompiler_host.py` prepara a transformação na árvore Waffle fixada. `tools/check_android_runtime_lifecycle.py` verifica que o método headless não toca em janela/áudio/renderer e que os gates permanecem ativos.
- A CI `42243707680` aprovou a preparação Waffle, validador de ciclo de vida, testes nativos, smoke VFS, APK Debug ARM64 e publicação dos artefatos.
- **Limite:** a CI prova transformação, compilação e ligação, não a execução dessa inicialização no aparelho. O núcleo ainda não carrega ELF nem executa o jogo; GS/Vulkan e áudio Android continuam pendentes.

## [VERIFICADO NA CI, run `42237906079`] Manifesto persistente de integridade SHA-256

- `sha256.h/.cpp` implementa SHA-256 sem dependência externa. Um vetor conhecido (`abc`) confirma o digest padrão.
- Durante a extração, cada arquivo ISO é hashado incrementalmente no mesmo fluxo de cópia; não é necessária uma segunda leitura integral para criar os hashes. Depois de gerar `.disc_layout` e `.disc_meta`, o instalador também calcula os hashes desses dois arquivos.
- O manifesto `.gow_disc_integrity` é escrito em `disc/` dentro do staging, contabilizado na estimativa de espaço e publicado junto com a instalação somente depois de sincronizado.
- `verifyIsoDiscIntegrity()` lê o manifesto em linhas, limita tamanhos/quantidade, rejeita caminhos duplicados/inseguros e abre cada componente com `openat`/`O_NOFOLLOW`. Cada alvo precisa ser arquivo regular, ter o tamanho registrado e corresponder ao SHA-256 calculado em blocos de 1 MiB.
- Na instalação nova, `MainActivity` confia no SHA-256 calculado no mesmo fluxo dos bytes copiados e na sincronização dos arquivos antes da publicação; não relê imediatamente todos os gigabytes. Para uma instalação já existente, `MainActivity` executa `verifyIsoDiscIntegrity()` no executor de I/O, fora da thread principal; o callback informa bytes e quantidade de arquivos e permite cancelar. Cancelamento ou verificação parcial nunca é tratado como resultado válido. Falha por manifesto ausente, conteúdo alterado, arquivo ausente ou symlink bloqueia a validação e conduz à opção de reparo já confirmada.
- Fixtures em ISO sintética de duas camadas verificam o vetor SHA-256, instalação íntegra, alteração de conteúdo sem mudar o tamanho, remoção de arquivo, substituição por symlink e cancelamento após o primeiro arquivo. A CI `42237906079` aprovou a base do manifesto; a CI `42240082689` aprovou o callback/cancelamento, todos os testes host, o smoke VFS, a compilação ARM64 e publicou o APK Debug.
- O APK novo ainda não foi executado no aparelho. A verificação SHA-256 percorre todos os arquivos instalados e, portanto, o tempo de execução em armazenamento real ainda precisa ser medido.

## [COMPILADO, NÃO TESTADO EM APARELHO] Refinamento do launcher, novo ícone, hora e bateria

- Novo ícone adaptativo original: Ômega branco sobre fundo vermelho em degradê para preto com pontos de brasa (fundo, primeiro plano e camada monocromática). A revisão por prévia renderizada (círculo, quadrado arredondado e tema monocromático) mostrou o Ômega grande demais para a zona segura; ele foi reduzido a 85% e recentralizado. A prévia é uma renderização dos próprios XML, não uma captura de launcher: o ícone ainda não foi instalado nem visto em um aparelho.
- Cabeçalho do FORGE com hora/data e bateria (`DeviceStatusMonitor`, `BatteryGaugeView`, `BatteryReading`). A lógica de `BatteryReading` tem teste JVM (que passa no host e foi adicionado à CI). As classes Android compilam sem avisos novos contra o `android.jar` da API 34; o comportamento em tela, o receptor e o layout em aparelhos reais **não foram exercitados**.
- Correções: duas caixas de diálogo exibiam `\n\n` literal em vez de quebra de linha; a versão exibida agora vem do pacote (antes estava duplicada no código); o modo imersivo é reaplicado ao recuperar o foco; rótulos de 7–8 sp foram aumentados para 8–9 sp onde havia espaço.
- Pendente: validar legibilidade e proporções em outros tamanhos de tela (item já aberto na Fase 1).

## [VERIFICADO] Leitor ISO9660 e importador privado

- `Iso9660Reader` abre o PVD ISO9660 no setor 16, resolve arquivos por diretórios/extents, enumera os filhos imediatos e lê por offset sem carregar a imagem inteira.
- `listDirectory()` e o parser aplicam limites de tamanho/profundidade/entradas e rejeitam traversal, registros/extents inválidos e caminhos ambíguos.
- `iso9660_installer.cpp` mantém a API genérica de manifesto e acrescenta `installIsoDisc()`: enumera todos os arquivos ISO9660 suportados, extrai-os em blocos de 64 KiB para `disc/`, calcula o espaço dos metadados e os escreve no staging antes da publicação atômica.
- O FORGE chama `nativeInstallIsoDisc()`, monta o `cdRoot`, chama a configuração de `PS2Runtime::IoPaths`, valida o ELF local e abre a pasta com a implementação real de `DiscSource::openExtracted()`. A checagem verifica o PVD no LSN 16, o descritor da segunda camada quando houver e o primeiro setor do BOOT2.
- Um teste host adicional compila a implementação real de `IopCdvd` e chama `dispatchImport(10)` (`sceCdSearchFile`) e `dispatchImport(6)` (`sceCdRead`) contra a instalação sintética: verifica dados copiados para a RAM IOP na camada 0 e 1, busca de arquivo aninhado e rejeição de leitura além do disco. Run `42226830317`: aprovado.
- Um probe C ABI/JNI `nativeValidateIopCdvd()` também foi adicionado ao FORGE. Ele cria uma instância isolada do dispatcher IOP apontada para a raiz privada, procura BOOT2 e lê seu setor inicial para conferir a assinatura ELF. A CI `42227233881` aprovou a compilação ARM64 e publicou o APK Debug. O probe ainda não foi executado em aparelho; nem o teste host nem essa checagem isolada equivalem a uma sessão viva do jogo.

## [VERIFICADO] Provider local cdRoot

- `CdRootVfs` abre a raiz privada e executa listagem/leitura somente de arquivos regulares; usa `openat`/`O_NOFOLLOW` para bloquear symlinks e traversal e `pread` para leitura aleatória. A resolução de nomes segue ISO9660 (maiúsculas/minúsculas indiferentes e sufixo `;versão` opcional), rejeitando colisões após normalização.
- A enumeração é limitada a 65.536 entradas; paths têm limites de tamanho, componente e profundidade. Handles podem ser abertos, lidos por offset e fechados.
- O teste sintético `cdroot_vfs_test.cpp` cobre raiz/subdiretórios, listagem repetida, leitura parcial/EOF, handles fechados, nomes/traversal inválidos e symlinks.
- `validatePs2Elf()` lê somente o cabeçalho ELF32 e a tabela de program headers do arquivo local; confere MIPS/little-endian/ET_EXEC, limites de `PT_LOAD` e cobertura do entry point.
- Fixtures sintéticas do ELF cobrem um executável válido, magic inválido e entry point fora de `PT_LOAD`. O teste valida também a busca local case-insensitive com sufixo ISO `;1`.
- A primeira implementação compartilhou acidentalmente o offset do descritor raiz via `dup()`. Foi corrigida usando `openat(rootFd, ".", ...)` para gerar descrição de arquivo independente por operação.
- A CI Android run `42187116530` concluiu os testes host, compilou o APK Debug ARM64 e publicou o artefato. A validação foi feita com ELF sintético; nenhum executável real de God of War foi usado.
- O provider `CdRootVfs` continua atendendo operações por caminho. O teste `tools/run_ps2_vfs_cdroot_smoke.py`, aprovado na CI `42232085986`, compila o `ps2_vfs.cpp` efetivamente preparado da revisão Waffle e valida abertura/leitura/seek/stat/fechamento por `cdrom0:` contra o provider Android com dados sintéticos. A auditoria da revisão Waffle também confirma que `IopEmulator` já instancia `IopCdvd` e encaminha imports `cdvdman` para `dispatchImport`; `PS2IopHostAdapter` fornece `IoPaths.cdRoot` a esse serviço. O que falta é executar esse caminho pelo IOP de uma sessão real do runtime no Android. Não há gameplay.


## [VERIFICADO] Shell FORGE e instalação privada do disco

- O FORGE usa Storage Access Framework para selecionar a imagem e identifica `SYSTEM.CNF`/`BOOT2`, reconhecendo inicialmente `SCUS_973.99` (NTSC-U) e `SCES_531.33` (PAL).
- A ação `INSTALAR JOGO` reabre e revalida a imagem, enumera a árvore ISO9660 completa suportada, extrai os arquivos para armazenamento privado e gera `.disc_layout`/`.disc_meta` dentro do staging antes da publicação.
- Após a publicação, o FORGE abre `<instalação>/disc` no `CdRootVfs` e valida o BOOT2 local como ELF32 little-endian MIPS, incluindo segmentos carregáveis e entry point.
- A instalação valida o ELF, a configuração de `IoPaths` e a implementação real de `DiscSource::openExtracted()`. O probe adicional cria uma instância isolada de `IopCdvd` para exercitar busca e leitura do BOOT2; não inicializa o runtime de gameplay nem executa instruções do ELF.
- O host test CDVD passou no run `42226830317`; a CI Android `42227233881` aprovou o build ARM64 e publicou o APK com a ponte JNI. Ainda não há teste em aparelho com ISO do usuário, IOP vivo, `loadELF()` ou gameplay.


## [VERIFICADO] Componentes KIexter importados

Fonte: [KIexster/god-of-war-recomp](https://github.com/KIexster/god-of-war-recomp), commit `a226151feb6ed22937e42be485aaf28a83acd166`, licença declarada GPL-3.0.

- `recompiler/config/funcmap.csv`: mapa das 6.414 funções de `SCUS_973.99`. Verificação estrutural: intervalos ordenados, tamanhos coerentes e sem sobreposições.
- `recompiler/config/recomp.template.toml`: configuração de stubs, entry points, skips e patches do executável NTSC-U; marcadores `@ELF@`, `@MAP@`, `@OUT@` presentes.
- `port/kiexter/gow_overrides.cpp`: overrides específicos de God of War, em staging.
- `port/kiexter/tools/vu1n_gen.py`: gerador AOT VU1, em staging.
- `port/kiexter/tools/iopn_gen.py`: gerador AOT dos módulos IRX/IOP, em staging.
- `port/kiexter/tools/vu1n_heads.txt` e `port/kiexter/gow_pad2_packet.h`: arquivos auxiliares importados.

**Importante:** os arquivos de staging não estão no CMake/Gradle e ainda não são compilados pelo APK. O override depende do runtime Waffle/KIexster e de headers auxiliares; os geradores exigem dumps privados produzidos a partir da cópia do usuário.

## [VERIFICADO] Proveniência das bases

- O projeto KIexster usa como base o runtime de [Waffle565/God-Of-War-Native-Port](https://github.com/Waffle565/God-Of-War-Native-Port), branch `runtime`, licença GPL-3.0 nessa branch.
- Os cabeçalhos dos overrides e dos geradores VU1/IOP apontam para código originalmente derivado de Waffle565, commit `34f30d5e432751369177af115104f4d19af6d080`.
- O build KIexster fixa [PS2Recomp](https://github.com/ran-j/PS2Recomp) no commit `c5a9d02573410a2085a4b4b831b0b68ba3515440`, licença GPL-3.0.
- Nenhuma árvore Waffle/PS2Recomp/renderer desktop foi copiada para a árvore versionada do projeto. O script prepara revisões fixadas em workspace privado e exporta a fonte Waffle para `android/app/build/gow-runtime-source` (ignorado pelo Git). O antigo submódulo CodeAbhi foi removido.

## [VERIFICADO] Bloqueios identificados na configuração Waffle

Auditoria estática do CMake e dos fontes na revisão Waffle de referência 49ca62c1d20b4659c61df6286c54e86edf411012:

- O CMake upstream inclui fontes ee_ref e vu_pcsx2 para teste diferencial/diagnóstico PCSX2. O perfil PS2X_VU1_NATIVE_ONLY agora as retira de ps2_runtime; no IOP, PS2X_IOP_NATIVE_ONLY também exclui core/iop_cpu.cpp. Builds de diagnóstico não estritos devem preservar os avisos PCSX2 GPL-3.0-or-later.
- ps2_vu1_core.cpp contém tanto o dispatcher AOT quanto rotinas da engine exata. No perfil estrito, o dispatcher não chama stepInterpreted quando um bloco não existe: registra diagnóstico e interrompe a VU1. O arquivo continua compilado porque abriga o próprio backend AOT e outros contratos; isso não equivale à remoção integral de todo código exato do mesmo translation unit.
- gs_gl.cpp é backend OpenGL e ps2_audio_device.cpp usa miniaudio/Raylib do host; substituir por GS/Vulkan Android e saída de áudio Android.
- ps2_pad_rumble_win.cpp é específico de Windows. ps2_android_runtime.cpp contém apenas um TODO de controles touch.
- ps2_vif1_interpreter.cpp decodifica comandos VIF1/UNPACK; não é o interpretador de instruções MIPS do EE. A decisão sobre sua permanência é por função de hardware, separada da regra contra fallback de CPU.

Essa revisão é referência para identificar fronteiras. O runtime Waffle/KIexter é preparado em `android/app/build/gow-runtime-source` (diretório ignorado), compilado e ligado ao alvo Android `android-game`. O runner Android ainda constrói e destrói `PS2Runtime`, mas não chama `initialize()`, `loadELF()` ou `run()`; a inicialização host Raylib/áudio continua sem adaptação Android.

## [VERIFICADO] Ferramenta PS2Recomp no host

- O script tools/build_recompiler_host.py passou em py_compile e no CLI --help.
- O modo --build-only baixou as revisões fixadas, aplicou ps2recomp-waffle-runtime.patch e compilou o alvo ps2_recomp com sucesso na execução GitHub Actions 42146201136, commit a8e373fda6d4f7ab88980dcb18d7f06d818e51ad.
- O log confirmou a compilação das dependências, de ps2_recomp_lib e do executável ps2_recomp.
- A execução não acessou ELF, ISO ou outro arquivo do jogo e não gerou C++ derivado do jogo.

## [VERIFICADO] IOP com fallback interpretado bloqueado na CI host

- O script aplica uma proteção em ps2xIOP/src/emulator/iop_emulator.cpp: após as rotas de import/HLE, overrides nativos e blocos IRX AOT, a ausência de uma faixa nativa gera diagnóstico [IOP:native-required], registra PC/RA, marca a execução como parada e retorna falha em vez de chamar iop_cpu.executeInstruction.
- O CMake introduz PS2X_IOP_NATIVE_ONLY; a opção é forçada para ON quando ANDROID está ativo. No modo estrito, o arquivo core/iop_cpu.cpp é removido da lista de fontes; IopCpuCore deixa de ser construído e a rotina de exceções é fornecida por helper independente.
- A execução GitHub Actions 42148409363 compilou com sucesso ps2_recomp e a biblioteca ps2_iop. O script analisou compile_commands.json e confirmou que nenhuma unidade iop_cpu.cpp foi compilada.
- O build utilizou iop_native_none.cpp, sem módulos IRX do jogo. Isso confirma a exclusão da unidade de interpretação e a compilação da proteção, não a cobertura AOT dos módulos reais nem uma execução do runtime no aparelho.
- O teste foi feito com GNU C++ no runner Linux; ainda falta compilar com Android NDK/ARM64 e validar com IRX privados.

## [VERIFICADO] Runtime Waffle estrito compilado no host

- O perfil PS2X_VU1_NATIVE_ONLY força o dispatcher nativo VU1 e bloqueia o caminho para stepInterpreted quando runNative não encontra um bloco registrado. O diagnóstico contém PC, instruções lower/upper, ciclo e flags de parada; a execução é interrompida.
- O perfil desativa gravação/replay e verificação A/B que usam a engine exata como oráculo.
- A configuração CMake estrita remove de ps2_runtime as fontes de diagnóstico diferencial ee_ref, ee_watch e vu_pcsx2; elas são mantidas somente em builds de diagnóstico não estritos.
- A execução GitHub Actions 42150329023 compilou ps2_runtime, ps2_iop e ps2_recomp com GNU C++ no runner Linux. compile_commands.json confirmou PS2X_VU1_NATIVE_ONLY=1 em ps2_vu1_core.cpp, nenhuma unidade iop_cpu.cpp e nenhuma fonte de diagnóstico PCSX2 no alvo estrito.
- Nenhum ELF, IRX ou microcódigo do jogo foi fornecido. Isso comprova a compilação das políticas e do runtime host; ainda não comprova cobertura AOT real ou execução no Android.

## [PREPARADO, COM TESTE SINTÉTICO] VU1 AOT estrito

- port/kiexter/tools/vu1n_gen.py agora aceita --strict: falha se nenhuma imagem válida for processada, nenhum bloco for gerado, qualquer bloco descoberto não tiver implementação nativa ou um bloco gerado contiver callLower/callUpper (chamadas para a engine exata/interpreter).
- A CI inclui dois microcódigos sintéticos: um vazio e outro com instrução lower não suportada que gera callLower. Ambos são rejeitados pelo gate; nenhum usa microcódigo do jogo.
- A trava PS2X_VU1_NATIVE_ONLY já compilou no runtime host (run 42150329023). A opção --strict ainda não foi executada com capturas reais; o runtime interromperá a execução no primeiro bloco não coberto. Ainda falta ligar os arquivos AOT gerados ao target Android.

## [VERIFICADO] Runtime Waffle ARM64 e camada SIMD NEON

- Foi criado port/kiexter/vu1/ps2_vu1_neon_compat.h para mapear a API SSE usada pela VU1 para NEON e para manter o controle de arredondamento usando FPCR/FPSR no Android.
- No runtime Waffle, o shim tem dois modos: smoke isolado usa as implementações NEON próprias; o runtime completo usa sse2neon para a API SSE2 mais ampla de EE/VU0, e o shim sobrescreve MAX/MIN e o mapeamento de CSR para FPCR/FPSR.
- O worker GS e ps2_runtime.cpp também selecionam o shim em ARM64, evitando includes diretos x86-only de xmmintrin.h.
- A execução Android shell 42152175783 compilou ps2_runtime e suas dependências estáticas com Android NDK 30.0.16248370, ABI arm64-v8a e API 28. O compile database confirmou PS2X_VU1_NATIVE_ONLY=1 e excluiu iop_cpu.cpp e as fontes diferenciais PCSX2. O runner NativeActivity do Waffle não foi construído.
- A mesma execução concluiu BUILD SUCCESSFUL para o APK Debug FORGE. O arquivo de smoke NEON continua incluído no alvo android-game, mas não é executado em runtime.
- Isso comprova compilação ARM64 e ligação da biblioteca estática ao `android-game`, não equivalência numérica bit-exata, execução em aparelho, inicialização do runtime ou gameplay. A Activity Java ainda não controla o ciclo de vida do `PS2Runtime`; o runner GameActivity nativo apenas constrói e destrói o objeto.

## [PREPARADO, NÃO TESTADO] Geração do C++ do jogo

- O mesmo script aceita --elf e usa funcmap.csv + recomp.template.toml para gerar C++ em diretório privado.
- Esta etapa ainda não foi executada com o ELF SCUS_973.99 do usuário.
- A triagem dos patches e as instruções estão em docs/PIPELINE_RECOMPILADOR.md e a seleção do runtime está em docs/BASE_KIEXTER.md.
- Não há ainda evidência de geração C++ do jogo, compilação ARM64 ou integração ao APK.

## [NÃO TESTADO] Runtime e jogo

- Ainda não foi executado um boot real do jogo, nem gameplay.
- O FORGE agora abre a `GowGameActivity` em modo de diagnóstico após a instalação/verificação aprovada; isso encaminha cdRoot/BOOT2, não inicia gameplay.
- A ponte Android já encaminha o BOOT2 e chama `gowRuntimeLoadElfAndCheckAot()` depois de `initializeCoreHeadless()`. O APK público ainda usa a tabela AOT placeholder vazia, então a porta recusa a carga antes de `loadELF()`; nenhum serviço real de boot é executado.
- A pasta legada `gow_boot_seed`, criada por versões anteriores do fluxo parcial, pode permanecer nos dados privados de instalações atualizadas; ela não é usada pelo fluxo atual. A instalação atual cria diretórios por serial e extrai a árvore ISO9660 completa, mas ainda não comprova a cadeia de boot nem a disponibilidade/cobertura AOT dos módulos IRX.
- O patch Waffle para encaminhar `cdrom0:` ao `CdRootVfs` e configurar o caminho físico em `IoPaths.cdRoot` compilou no host estrito e no target Android ARM64. A ponte para configuração de `IoPaths` está compilada; a validação efetiva desse caminho ocorre quando o app instala uma imagem. O dispatcher CDVD foi exercitado por teste host sintético; o probe Android foi compilado no run `42227233881`, mas ainda falta executá-lo em aparelho e, depois, exercitar o serviço CDVD do IOP de uma sessão real.
- Não há cobertura comprovada de VU1 AOT/IRX para o conteúdo da cópia do usuário.
- Backend GS/Vulkan Android, presenter completo e saída de áudio Android ainda não estão integrados ao ciclo de gameplay.
- Primeira imagem, menus, controles touch visuais, áudio, FMVs, saves, desempenho e compatibilidade ainda não foram testados em aparelhos reais.

## Próximos bloqueios reais — ordem de prioridade

1. [x] Executar smoke test do `PS2Vfs` via provider `cdRoot` Android: abertura, leitura, seek, stat, rejeição de escrita/traversal e fechamento com fixture sintética (CI `42232085986`).
2. Implementar e validar um manifesto completo de God of War com base na árvore real da revisão do usuário e nos acessos de boot; não adivinhar caminhos.
3. Preparar o ELF em caminho privado, executar PS2Recomp e compilar o C++ gerado para ARM64, sem enviar conteúdo proprietário ao Git/CI.
4. Ligar o lifecycle do PS2Runtime à GameActivity, chamar o loader e estruturar diagnóstico de cada fase de boot.
5. Gerar e integrar VU1/IRX AOT com cobertura comprovada para os módulos realmente usados.
6. Integrar CD/DVD, SIF/DMA e os serviços restantes necessários à cadeia de boot.
7. Implementar GS/Vulkan Android e presenter para alcançar a primeira imagem real.
8. Validar instalação, boot, menu, novo jogo e primeiro combate com uma ISO do usuário em aparelho real.
9. Integrar saída de áudio Android e concluir controles, saves e demais componentes de gameplay.

**Adiado:** medir e otimizar FPS/1% low, cache/prefetch, resolução e comportamento térmico em aparelhos modestos. Essa etapa começa depois do primeiro gameplay funcional; a referência definida é Black for Android.

**Nenhum gameplay foi testado ou declarado funcional.** Nenhum ELF, ISO, IRX, microcódigo, save ou dado proprietário foi enviado ao repositório ou à CI pública.

## Diagnóstico visível de inicialização Android — 06/11/2026

**[IMPLEMENTADO; BUILD COMPLETO E TESTE EM APARELHO PENDENTES]** A `GowGameActivity` agora possui um painel de status sobre a superfície nativa. O `android_main()` envia pelo JNI o estado de preparação do núcleo, o resultado da inicialização e mensagens de falha. A Activity entrega as alterações à thread principal e guarda a mensagem caso a chamada nativa chegue antes da criação do painel.

O painel informa explicitamente que o jogo ainda não iniciou quando o núcleo headless está pronto: a geração/integração do C++ AOT privado, o backend GS/Vulkan Android e o áudio continuam pendentes. A mensagem de erro inclui a orientação para consultar Logcat com a etiqueta `GowAndroid`.

**Limites de validação:** a CI `42246181055` executou os testes headless/host/VFS, mas o `assembleDebug` revelou que `GameActivity` não expõe `activity->clazz`; essa abordagem foi removida. A implementação seguinte registra a própria Activity por JNI e verifica sua identidade antes de liberar a referência global. Na última consulta, a nova CI `42246883029` já havia passado pelo checker e pelos testes nativos/VFS e estava compilando o APK ARM64. A build final e a apresentação visual em aparelho ainda não foram confirmadas. Essa alteração não habilita `loadELF()`, `run()` nem gameplay.
## Encaminhamento FORGE → GameActivity e porta ELF/AOT — 06/11/2026

**[IMPLEMENTADO; CI APROVADA]** Após a instalação/verificação privada, o FORGE abre `GowGameActivity` com `cdRoot` e o caminho BOOT2. `android_disc_bridge.cpp` monta o VFS privado; `main.cpp` recebe os parâmetros por um callback JNI atômico e usa uma condition variable para impedir que `android_main()` tente ler o BOOT2 antes da configuração Java. A Activity também libera a espera quando é destruída.

**[IMPLEMENTADO; NÃO É BOOT]** Depois da inicialização headless, `android_main()` chama `gowRuntimeLoadElfAndCheckAot()`. A ponte recusa imediatamente o build público quando a tabela de funções AOT está vazia; quando houver código gerado, resolve o BOOT2 apenas dentro do diretório privado, chama `PS2Runtime::loadELF()` e verifica se a tabela contém a função do endereço de entrada. A função não chama `PS2Runtime::run()`.

**[VERIFICADO NA CI, run `42250619735`]** Preparação Waffle, verificador de lifecycle/JNI, testes nativos de entrada/ISO/CDVD, PS2Vfs, teste de confinamento do caminho BOOT2 (traversal, caminho absoluto, nome vazio, diretório, prefixo de dispositivo e symlink externo) e build APK Debug ARM64 passaram. Artefato `gow-android-arm64-shell` ID `13520766214`, disponível até 13/11/2026. Ainda falta instalar e abrir esse APK em aparelho com uma instalação do usuário, conferir Logcat/painel e validar o callback visual.

**Bloqueios para gameplay:** (1) executar PS2Recomp com o ELF privado e integrar o C++ AOT/registro de funções ARM64 ao APK de teste; (2) gerar e integrar VU1/IOP AOT/IRX com cobertura; (3) implementar GS Vulkan Android e presenter real; (4) implementar áudio Android; (5) validar CDVD/serviços da sessão real e a primeira imagem. Nenhuma ISO, ELF, IRX, microcódigo ou C++ gerado foi enviado ao Git ou à CI pública.
## Revisão de sessão da GameActivity — CI 221

**[VERIFICADO NA CI `42252442268`]** A configuração BOOT2 é zerada ao registrar uma nova `GowGameActivity`; a Activity registra sua referência JNI antes de `super.onCreate()`, que inicia o ciclo nativo. `android_main()` aguarda a configuração recebida, e a limpeza da referência JNI também libera a espera quando a Activity encerra. O APK Debug ARM64 compilou e os testes host/VFS passaram.

**Limite:** a verificação da CI não substitui a execução em aparelho. Ainda falta abrir o APK com uma instalação privada real, observar o painel, verificar se a mensagem da tabela AOT vazia é apresentada e recolher Logcat `GowAndroid`. O APK ainda não contém AOT do jogo e permanece não jogável.
## Orquestrador local ELF → C++ AOT → APK — 06/11/2026

**[IMPLEMENTADO; TESTES DE ORQUESTRADOR EM CI]** `tools/build_android_game.py` coordena o recompilador host, staging de C++ gerado e `gradlew assembleDebug`. Exige o ELF `SCUS_973.99` fora do repositório, workspace ASCII fora do repositório e diretório de staging ignorado pelo Git. Também rejeita `SCES_531.33` porque o mapa/template atual é NTSC-U. O C++ gerado fica em `android/app/build/gow-generated-game/`; nenhum conteúdo derivado do jogo é enviado à CI.

**Uso:** consulte `docs/ANDROID_AOT_LOCAL_BUILD.md`. O script possui `--plan-only` e `--generate-only`. A CI verifica regras de caminho, perfil regional, comando Windows/Gradle, CLI e sintaxe; ela não gera C++ do jogo real, pois não recebe o ELF do usuário.

**Limite:** este fluxo local ainda não foi executado com um ELF real do usuário. Mesmo com a tabela AOT ligada, `PS2Runtime::run()` permanece bloqueado até GS/Vulkan, presenter, boot/IRX/VU1 e áudio estarem conectados.
## Presenter de frames Vulkan Android — CIs 230 e 231

**[IMPLEMENTADO; COMPILAÇÃO E TESTE HOST APROVADOS]** A `GowGameActivity` agora tem uma rota opcional de apresentação de frames RGBA: `GS::copyLatchedHostPresentationFrame()` pode entregar o frame ao `VulkanSmokeRenderer`, que ajusta proporção/letterboxing, converte RGBA/BGRA quando necessário e usa um `VkBuffer` host-visible para `vkCmdCopyBufferToImage` na imagem adquirida da swapchain. A implementação acompanha o layout Vulkan de cada imagem, trata memória não coerente com flush e reutiliza o buffer/pixels escalados entre frames iguais.

**[VERIFICADO NA CI]** Run `42255177609` (230) aprovou o teste host do scaler RGBA e compilou o APK Debug ARM64. Run `42255283925` (231) repetiu a build com invalidação do cache quando a swapchain é recriada; também aprovou. Artefato `gow-android-arm64-shell` ID `13522097351`, disponível até 13/11/2026.

**Limite essencial:** o caminho está atrás de `GOW_ANDROID_ENABLE_VULKAN_SMOKE=ON`, que continua OFF por padrão. É somente um consumidor de `PresentationFrame`, não um `GSRasterBackend` de gameplay. O `GSNullBackend` ainda é o backend em uso, não são emitidos triângulos/texels GS e o clear-only permanece como fallback. Nenhum frame real de God of War foi apresentado.

**Próxima etapa real do GS:** implementar e ligar um `GSRasterBackend` Android que preserve VRAM/PSM, transferências, CLUT, depth, alpha test, DATE, blending, rasterização de primitivas e composição de DISPLAY/PMODE; só então a ponte de apresentação pode receber pixels do jogo.
## Pipeline Vulkan GS experimental — CIs 251–257

**[IMPLEMENTADO; COMPILAÇÃO ARM64 APROVADA]** O adaptador `AndroidGSVulkanBackend` pode ser anexado ao `GS` quando a surface Vulkan da `GowGameActivity` está pronta. O caminho Debug substitui o `GSNullBackend` por uma implementação experimental que reutiliza a `GSTextureStore` para VRAM/transferências, converte `GSPrimitiveBatch` para vértices GPU e enfileira esses lotes de forma thread-safe. A surface cria pipelines Vulkan separados para pontos, linhas e triângulos/sprites e usa viewport/scissor por lote. Ao perder a janela, o backend é destacado e a fila é limpa.

**[VERIFICADO NA CI]** Runs `42258381042` (251), `42258466963` (252), `42258475240` (253), `42258481466` (254), `42258614346` (255), `42258720349` (256) e `42258945494` (257) aprovaram progressivamente a compilação Android ARM64, a opção Debug ligada, a proteção da fila, o reset da surface e a checagem estrutural do pipeline. Run 257 publicou `gow-android-arm64-shell`, artefato ID `13524346408`, válido até 13/11/2026.

**Limitações explícitas:** o pipeline desenha cor sólida diretamente na surface Android, não em render targets GS por FBP/PSM. Texturas/CLUT não são amostradas; depth, alpha test, DATE, blending PS2, PABE, FBA, fog, dithering, masks avançadas e composição de DISPLAY/PMODE ainda não são rasterizados. A CI não executou a aplicação numa GPU física. `PS2Runtime::run()` continua bloqueado e a tabela AOT pública permanece vazia.

**[EM VALIDAÇÃO HOST; AINDA NÃO APROVADO]** Runs `42259346530`/`42259354011` adicionaram um teste sintético de VRAM e transferência contra `GSTextureStore` e `ps2_gs_memory.cpp`. Não marque esse teste como aprovado até uma execução terminar com sucesso.
## Adaptador de VRAM GS validado com GSTextureStore — CI 261

**[VERIFICADO NA CI `42260019446`]** O teste host `android_gs_vulkan_backend_test.cpp` foi compilado contra `gs_texture_store.cpp` e `ps2_gs_memory.cpp` do Waffle. Passaram: escrita/leitura CT32 no mapa PS2, limpeza limitada pelo scissor, preservação de canal via FBMSK, transferência sintética HOST→LOCAL para dois pixels CT32 e snapshot de exatamente 4 MiB. O assert de transferência foi ajustado para o contrato real do Waffle: ao concluir, `totalPixels` volta a zero, `copiedPixels` fica em dois e `direction` retorna a 3 (idle).

**Artefato:** `gow-android-arm64-shell`, ID `13524986210`, build Debug ARM64, expira em 13/11/2026. Run `42260019446` também aprovou o APK.

**Limite:** esse teste valida a camada de memória/transferência, não rasterização correta de cenas. O pipeline ainda desenha lotes sólidos diretamente na surface; o passo seguinte é gravar primitivas em render targets FBP/PSM e produzir um `PresentationFrame` a partir do framebuffer selecionado.
## Rasterizador inicial em VRAM e saída CRTC — CI 268

**[IMPLEMENTADO; TESTE HOST E APK ARM64 APROVADOS]** O `AndroidGSVulkanBackend::Submit()` agora envia primitivas opacas não texturizadas ao rasterizador inicial, que escreve o framebuffer CT32/CT24/CT16 na mesma VRAM swizzled do Waffle, respeitando XYOFFSET, scissor e FBMSK. `Present()` decodifica o circuito ativo por DISPFB/DISPLAY e devolve um `PresentationFrame` com stride de 640 pixels por linha; o presenter Vulkan existente exibe esse frame.

**[VERIFICADO NA CI `42261321872`]** O teste sintético desenha um triângulo vermelho e um sprite azul em VRAM CT32, lê o CRTC de 64×32 pixels e verifica as cores no frame RGBA, o stride 640 e uma região não desenhada preta. O teste de VRAM/transferências de 4 MiB também continua passando. Artefato `gow-android-arm64-shell` ID `13524359107`, expira em 13/11/2026.

**Limitações:** apenas primitivos opacos sem textura em CT32/CT24/CT16; `Present()` escolhe um circuito e não compõe DISPLAY1+DISPLAY2/PMODE. Textura/CLUT, depth, alpha test/DATE, blending, fog, dithering e formatos não suportados são bloqueadores. `PS2Runtime::run()` continua bloqueado, e esta fixture não contém o jogo.
## Rasterizador GS, composição PMODE e preparação da comparação visual — 06/11/2026

**[IMPLEMENTADO; TESTES HOST APROVADOS; APK ARM64 EM VALIDAÇÃO NA ÚLTIMA CONSULTA]**

A rota Android de correção gráfica usa `AndroidGSVulkanBackend` para manter a VRAM real de 4 MiB por meio de `GSTextureStore`, rasterizar por CPU os fragmentos suportados e entregar o `PresentationFrame` ao presenter Vulkan da GameActivity. Isso é um backend de referência funcional para testes de semântica GS; ainda não é um rasterizador GS inteiramente executado em shaders Vulkan.

Cobertura atual exercitada em `android_gs_vulkan_backend_test.cpp`:

- Primitivas POINT, LINE/LINESTRIP, TRIANGLE/TRISTRIP/TRIFAN e SPRITE; escrita em FBP/FBW/PSM e CRTC básico.
- Texturas CT32 e formatos indexados T8/T4/T4HL/T4HH/T8H, com carregamento de CLUT CSM2 e fixture de índices swizzled CSM1.
- UV e STQ básicos, amostragem nearest/bilinear e modos de clamp suportados pelo caminho CPU.
- Z32 com teste GEQUAL e escrita de depth; alpha test com ação KEEP; DATE; equação GS ALPHA com color clamp.
- Composição dos circuitos 1/2 com PMODE, ALP/MMOD e SLBG/BGCOLOR.
- Testes FBP/VRAM e quadro CRTC com pixels esperados.

**[VERIFICADO NA CI, run `42268386758`]** A execução completa passou pelo verificador de lifecycle (incluindo controle de captura Debug), comparador PPM, gravador PPM, bateria nativa de entrada/disco/GS, VFS privado e compilação/publicação do APK Debug ARM64. Artefato `gow-android-arm64-shell`, ID `13528990480`. A CI usa apenas fixtures sintéticas; não recebe ELF, ISO, IRX nem imagens do jogo. A execução comprova build e integração do botão/caminho de captura, não que o runtime tenha produzido um frame real.

**Limites ainda existentes:** fog/FGE e antialiasing são rejeitados pelo rasterizador; dithering GS, mipmapping e todas as particularidades de filtering/blending ainda não foram demonstrados; interlace/overscan de vídeo não estão completos; a cobertura de PSM não equivale à implementação de todos os render targets e estados do GS. A rota de desenho é CPU/readback para correção inicial, não a solução final de desempenho móvel. Nenhuma GPU física Android foi validada por esses testes.

## Preparação da comparação de cenas reais — captura local e ferramenta offline

`android_gs_scene_capture.cpp` grava o primeiro `PresentationFrame` recebido em `gow_android_first_gs_frame.ppm` e permite até 32 capturas adicionais sob demanda pelo botão **Capturar cena** do APK Debug (`gow_android_scene_01.ppm` … `gow_android_scene_32.ppm`). Os arquivos ficam no diretório de arquivos externos da aplicação (fallback para o interno); o Logcat `GowAndroid` informa cada caminho. A gravação é local, validada e publicada via arquivo temporário; nenhuma captura é enviada à CI ou ao GitHub.

`tools/compare_gs_scene.py` compara duas imagens PPM P6 com dimensões idênticas e produz MAE, RMSE, PSNR, erro máximo por canal e porcentagem de pixels dentro da tolerância; opcionalmente grava um mapa de diferenças. Os limiares padrão são parâmetros iniciais para inspeção, não critérios definitivos de compatibilidade.

**[PENDENTE — CENA REAL]** O runtime ainda mantém `PS2Runtime::run()` bloqueado e a tabela AOT pública vazia. Portanto, ainda não existe captura comprovada do God of War real no Android. A captura local só será gerada quando o runtime entregar um frame; a validação visual real exige o ELF/AOT privado do usuário, boot real em aparelho e uma referência da mesma cena, resolução e momento.


## Worker EE separado, pré-validação AOT e tentativa de primeiro boot Android — 06/11/2026

**[VERIFICADO NA CI HOST 42274965500]** A preparação local da árvore Waffle adiciona um worker EE Android separado do ciclo de apresentação da GameActivity. O worker exige núcleo inicializado, scheduler criado, PC válido e função AOT registrada; executa o scheduler em uma thread própria e a GameActivity mantém a responsabilidade pela apresentação.

**[ENCERRAMENTO SEGURO]** `stopGuestExecutionAndroid()` solicita parada e faz `join()` antes da destruição dos subsistemas. O caminho não usa `detach()`; destruir o runtime a partir da própria thread é tratado explicitamente como erro fatal, evitando uso de memória após destruição.

**[PRÉ-VALIDAÇÃO AOT]** `tools/build_android_game.py` rejeita tabela vazia, registros ausentes, endereços desalinhados/fora da faixa, slots inconsistentes/duplicados e entrypoint do ELF sem função registrada. Fixtures host cobrem tabela válida, vazia, entrypoint ausente e slot incorreto. Isso evita gerar um APK que o loader recusaria; ainda não foi executado com o ELF privado do usuário.

**[VERIFICADO NAS CI ANTERIORES]** Host run `42274965500` compilou o recompiler e o runtime Waffle sob políticas estritas; Android run `42274999877` aprovou o preparo do runtime, checker de lifecycle, testes sintéticos de captura/comparação, I/O/disco/VFS e build Debug ARM64. Artefato `gow-android-arm64-shell` ID `13532807788`. Nenhum conteúdo privado do jogo foi fornecido à CI.

**[IMPLEMENTADO; CI #362 EM VALIDAÇÃO]** `android_main()` agora tenta iniciar o worker EE quando os gates de ELF/AOT privado, backend GS Android e surface/presenter Vulkan estão satisfeitos. A tentativa é silenciosa até existir saída AAudio. O bridge informa início/encerramento à GameActivity; o Logcat também registra o primeiro estado GS não suportado.

**[AINDA NÃO É BOOT REAL]** O APK público continua com tabela AOT vazia. Nenhum ELF real foi recompilado, nenhum IRX ou microprograma VU1 real foi exportado/compilado e nenhuma sessão do jogo foi iniciada em aparelho. Portanto, o worker ainda não iniciou com código real de God of War. O próximo bloqueio externo é obter o ELF privado NTSC-U e produzir AOT do jogo, VU1 e IRX; então testar o boot no aparelho Android ARM64.


## Instalação com leitura mínima dos setores — 06/11/2026

**[IMPLEMENTADO; AGUARDANDO CI E MEDIÇÃO EM APARELHO]** A análise de .disc_meta agora percorre apenas os intervalos fora dos extents dos arquivos ISO. Os extents copiados já são lidos e hasheados durante a extração; relê-los nas duas passagens de contagem/gravação de metadados era redundante. A leitura de metadados usa lotes de 512 setores (1 MiB), e os registros .disc_meta são gravados em blocos, não um write() por setor.

A extração usa buffer de 1 MiB e atualiza o callback de progresso/cancelamento em intervalos de 4 MiB e ao concluir cada arquivo. O SHA-256 de verificação usa buffer heap de 1 MiB e notifica em intervalos de 16 MiB/por arquivo.

**[DECISÃO DE INTEGRIDADE]** Na instalação nova, cada buffer de origem é hasheado enquanto os mesmos bytes são enviados ao arquivo de destino; os arquivos e diretórios são sincronizados antes da publicação atômica. Por isso, o FORGE não faz uma segunda leitura integral imediata dos mesmos gigabytes após a cópia. Ao reconhecer uma instalação existente, verifyExistingInstallationAsync() continua realizando a verificação SHA-256 completa antes de aceitá-la.

**[PENDENTE]** O ganho real depende da proporção de setores ocupados pelos arquivos na imagem e do desempenho do provedor SAF/armazenamento do aparelho. Não anunciar um fator de aceleração antes de medir novamente no dispositivo.


## [VERIFICADO NA ANDROID CI #342] Instalação ISO com menos I/O — 06/11/2026

- A análise e a gravação de `.disc_meta` agora percorrem somente os intervalos fora dos extents de arquivos ISO. Os setores dos arquivos são lidos uma vez durante a extração; os setores não pertencentes a arquivos continuam preservados quando contêm dados.
- O scanner usa lotes de 512 setores (1 MiB) e os registros `.disc_meta` são escritos em lotes. As fixtures de duas camadas continuam exigindo os LSNs não pertencentes a arquivos `16, 20, 24, 32, 36` e comparam o conteúdo de cada setor com a ISO sintética.
- A extração usa buffer de 1 MiB, calcula SHA-256 no mesmo fluxo de cópia e atualiza progresso a cada 4 MiB e no fim de cada arquivo. O cancelamento continua disponível antes da publicação atômica.
- Uma instalação nova não relê imediatamente todos os arquivos para outra verificação SHA-256: os bytes foram hasheados durante a cópia e sincronizados antes da publicação. Instalações existentes ainda recebem a verificação SHA-256 completa antes de serem aceitas.
- O teste de cancelamento no limite de publicação e os testes de leitura/metadados passaram novamente; a Android CI #342 compilou e publicou o APK Debug ARM64.
- **[PENDENTE]** Medir o tempo real em um aparelho Android com a mesma imagem de disco. Não há ainda um fator de aceleração comprovado.

## [EM VALIDAÇÃO] Paralelismo limitado para build AOT local

- `tools/build_recompiler_host.py` usa `GOW_BUILD_JOBS` (padrão `1`, intervalo `1–16`) nos builds CMake do host.
- `tools/build_android_game.py` limita o Gradle a um worker; o CMake Android usa um job pool de compilação nativa configurável, padrão `1`.
- O objetivo é reduzir picos de memória ao compilar o runtime e as unidades C++ geradas pelo PS2Recomp. A CI dos commits `c022047`, `182e8bc` e `6fb6377` ainda precisa concluir antes de marcar essa parte como verificada.



## [VERIFICADO NAS CI ANDROID #346–#349] Exportação privada do ELF para AOT — 06/11/2026

- A tela FORGE disponibiliza `EXPORTAR ELF PARA BUILD AOT` apenas para a imagem NTSC-U cujo `SYSTEM.CNF` aponta para `SCUS_973.99` e quando existe a instalação privada com o manifesto de integridade.
- A ação usa o seletor Android `CreateDocument` e copia somente o executável ELF, não a ISO nem os demais arquivos do disco. Antes da cópia, a ponte nativa valida o ELF32/MIPS; o app compara caminho, tamanho e SHA-256 com a entrada correspondente de `.gow_disc_integrity` e calcula novamente o hash durante a exportação.
- A exportação não faz uma segunda verificação integral dos gigabytes do disco. Isso não substitui a verificação completa usada pelo fluxo de inicialização; serve apenas para obter a entrada privada do recompilador host.
- Cópias incompletas ou com hash diferente tentam remover o destino SAF e avisam quando o provedor não permite a remoção. O botão exige o nome exato `SCUS_973.99`, usado pelo perfil NTSC-U do host recompiler.
- A seleção de outro disco e a instalação/verificação ficam bloqueadas enquanto a exportação está em andamento. A compilação Android dessa funcionalidade passou nas CI #346, #347, #348 e #349.
- **[PENDENTE]** Executar a exportação em aparelho com uma instalação real e usar o ELF privado no `tools/build_android_game.py`. A CI pública não recebe o ELF, C++ AOT ou ISO.

## [VERIFICADO] Limites de paralelismo do build local AOT — 06/11/2026

- `GOW_BUILD_JOBS` controla os builds CMake host (padrão 1; permitido 1–16); Gradle local usa um worker e o CMake Android usa pool nativo padrão de 1 job.
- O host recompiler passou nas CI #90 e #91; o APK ARM64 compilou nas Android shell #343 e #344 com os limites de memória ativados.

## [VERIFICADO POR INSPEÇÃO DOS APKs] Assinatura Debug entre execuções — 06/11/2026

- Certificados SHA-256 foram extraídos dos APKs das CI #339, #344, #347, #348 e #349. Os fingerprints diferem entre execuções: os runners geram chaves Debug efêmeras, portanto esses APKs não são atualizações assinadas pela mesma identidade.
- `android/app/build.gradle` e `.github/workflows/android-shell.yml` agora aceitam uma chave Debug persistente por meio de quatro GitHub Actions Secrets. Sem esses Secrets, o build mantém o fallback Debug padrão e o APK fica marcado no log como não compatível com atualização entre execuções.
- A Android shell #352 passou após corrigir a configuração Gradle. A CI #353 adiciona a verificação `apksigner` e a impressão do fingerprint no resumo; ainda falta configurar e validar os Secrets com uma chave privada do mantenedor. Os APKs antigos não mudam retroativamente de assinatura.
- Não desinstalar um APK Debug antigo apenas para instalar uma execução diferente: isso apaga o armazenamento privado, incluindo a instalação do disco. A migração/backup dessa instalação ainda não foi implementada.


## Correção da CI de assinatura do APK — 06/11/2026

- **[DIAGNÓSTICO CONFIRMADO]** Na Android shell #355, as etapas de testes e `assembleDebug` terminaram com sucesso. A falha ocorreu exclusivamente em `Verify APK signature and print certificate`.
- A saída real do `apksigner` do runner usou o formato `V2 Signer: certificate SHA-256 digest: <fingerprint>`; o parser anterior procurava somente `Signer #1 certificate SHA-256 digest:`. Assim, o APK estava assinado e verificável, mas a variável do fingerprint ficava vazia e o step encerrava com código 1.
- **[CORREÇÃO ENVIADA À main]** Commit `786264d367ccf4ba79fdbc2a60b9ee0c8018a321` altera o parser para capturar o último campo após `: ` e exige exatamente 64 caracteres hexadecimais. Se o formato voltar a ser desconhecido, o step imprime a saída de diagnóstico e falha com mensagem explícita.
- **[VERIFICADO]** A execução #356 (`42290930731`) concluiu com sucesso: `assembleDebug`, `apksigner` e publicação do artefato `gow-android-arm64-shell` ID `13540251766`.
- Os quatro secrets de assinatura persistente continuam não configurados. Na ausência deles, o runner usa uma chave Debug efêmera: o APK pode ser instalado numa instalação nova, mas não é garantido que atualize uma instalação criada por outra execução. Não desinstalar o app antigo enquanto os dados privados do disco não estiverem preservados.


## Tentativa de primeiro boot Android e diagnóstico GS — 06/11/2026

- **[VERIFICADO NA CI #389]** O runtime bridge expõe `gowRuntimeStartGuestExecutionAndroid()` e `gowRuntimeGuestExecutionFinishedAndroid()`. O `android_main()` inicia o worker EE somente se o ELF privado passou pela validação AOT (tabela não vazia e entry point registrado), o backend GS Android está anexado e a surface Vulkan está pronta.
- A execução ocorre na thread nativa dedicada criada pelo runtime Waffle; não chama `PS2Runtime::run()` nem abre janela/áudio Raylib. O encerramento segue `requestStop()` + `join()`.
- O primeiro teste é silencioso: o subsistema SPU2 continua sem consumidor de áudio Android, e a fila interna é limitada. Isso permite diagnosticar o boot sem afirmar que AAudio/efeitos/música funcionam.
- O backend GS agora registra uma vez o primeiro lote rejeitado pelo rasterizador: Logcat `GowAndroid` no Android e `stderr` nos testes host. Assim uma primeira cena com fog, antialiasing ou estado ainda não suportado não é descartada sem explicação.
- **[VERIFICADO NA CI #389]** O checker final do worker, captura VU1/IOP, exportação ZIP, registro de PCs VU1 dinâmicos e compilação ARM64 passaram juntos; `apksigner` verificou a assinatura e o APK foi publicado como artefato `gow-android-arm64-shell` ID `13542334085` (run `42297885573`, commit `f113cc07c487b2cf3b1ec354dd5887476db45940`). A CI host #98 (`42297395258`) também concluiu com sucesso, compilando o runtime/recompilador estrito sem dados do jogo.
- **[NÃO TESTADO]** Ainda não há ELF/C++ AOT privado real ligado, microprogramas VU1 e IRX AOT reais, nem sessão de jogo em aparelho após essa alteração. A tabela AOT pública permanece vazia; o worker não deve iniciar no APK shell sem a build local do usuário.
- A CI #361 falhou porque o backend passou a incluir `android/log.h` também no teste host. Commit `6cc7c3ef32cea5c823d1dc933bc9fbd38c22fefe` tornou o include e a saída condicionais (`__ANDROID__` → Logcat; host → `stderr`). CI #362 está validando essa correção, junto com o caminho de primeiro boot.


## Captura incremental e geração AOT privada VU1/IOP — 06/11/2026

- **[VERIFICADO NA CI ANDROID #380]** A etapa `Test private Android VU1 and IOP AOT capture pipeline` passou; o checker de lifecycle/AOT passou; a bateria Android de disco/VFS/input passou; o APK ARM64 compilou, teve assinatura verificada e foi publicado como `gow-android-arm64-shell`, artefato ID `13541888117` (6.217.218 bytes). Run: `42296864820`, commit testado `66d673a0fe3b729069156c58bccc5b0cf98e4559`.
- **[IMPLEMENTADO]** `android_main()` prepara `files/gow-aot-captures/vu1` e `files/gow-aot-captures/iop` na instalação privada e configura `GOW_VU1_CAPTURA`/ `PS2X_IOP_DUMP_MODULES` antes de iniciar o guest. O worker só entra depois de validar ELF/AOT, anexar o GS Android e ter a surface/presenter Vulkan prontos.
- **[IMPLEMENTADO]** A GameActivity oferece `Exportar dados AOT` via SAF. O ZIP inclui somente capturas VU1/IOP selecionadas, manifesto e, na revisão mais recente, a lista de PCs VU1 dinâmicos ausentes. O app não envia esse ZIP a serviços externos.
- **[IMPLEMENTADO; TESTES HOST]** `build_android_native_aot.py` valida o ZIP, rejeita traversal/symlinks, valida micro-memórias VU1 de 16 KiB e confere tamanho/FNV-1a dos módulos IOP; gera C++ VU1/IOP em diretórios Git-ignored. O modo VU1 `--strict-partial` omite blocos que exigiriam callouts ao motor exato; nenhum callout/interpreter fallback é emitido para Android.
- **[IMPLEMENTADO]** `build_android_game.py` exige um modo explícito: `--capture-bootstrap` para a primeira build EE com captura estrita, ou `--aot-captures` para incorporar ZIPs parciais/completos. Ele prepara a árvore Waffle, recompila EE e só depois gera/integra as unidades VU1/IOP disponíveis.
- **[VERIFICADO NA CI #389]** As capturas VU1/IOP, exportação `vu1n_heads.txt`, merge dos PCs dinâmicos, geração parcial nativa e checker final passaram em conjunto. Artefato APK Debug ARM64 ID `13542334085`; execução host #98 passou também.
- **[NÃO TESTADO COM O JOGO]** Ainda não há ELF `SCUS_973.99` nem ZIP de capturas reais fornecidos ao host; a CI usa somente fixtures sintéticas. O APK #389 é shell/diagnóstico e não contém código AOT privado de God of War, portanto não é um build jogável nem prova de boot real.

**Próxima evidência necessária:** executar o modo `--capture-bootstrap` com o ELF obtido da cópia do usuário, instalar o APK correspondente no Android, iniciar a tentativa de boot, exportar o ZIP privado e repetir com `--aot-captures` até os subsistemas usados naquela fase estarem traduzidos e o runtime avançar.

