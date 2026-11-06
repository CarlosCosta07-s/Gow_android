# Estado do projeto

**Data da auditoria:** 2026-11-06  
**Branch de desenvolvimento:** `main`  
**Objetivo:** port nativo de God of War (PS2, 2005) para Android ARM64, usando recompilação estática e runtime de serviços PS2 nativo; não é um emulador.

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

**Próxima prioridade:** validar em aparelho a instalação e executar o probe CDVD com a ISO do usuário. Depois, integrar o serviço CDVD ao IOP/runtime vivo, exercitar chamadas originadas pelo jogo e disponibilizar/carregar os módulos IRX necessários. Nenhuma ISO, ELF, IRX ou arquivo extraído deve ser enviado ao GitHub.

## [COMPILADO, NÃO TESTADO EM APARELHO] Refinamento do launcher, novo ícone, hora e bateria

- Novo ícone adaptativo original (bigorna com faísca; fundo, primeiro plano e camada monocromática). Conferido por renderização de prévia em círculo, quadrado arredondado e tema monocromático, com a arte dentro da zona segura. Não foi instalado nem visto em um launcher real.
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
- O provider `CdRootVfs` continua atendendo operações por caminho. O JNI valida a classe real `DiscSource` e contém o novo probe CDVD/IOP; o teste host atravessa `IopCdvd::dispatchImport` com uma instância isolada e armazenamento sintético. Ainda não há teste com dados reais nem execução do IOP que pertence a uma sessão de gameplay.


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
- O launcher ainda não inicia a GameActivity/runtime depois da preparação.
- O PS2Runtime ainda não recebe o ELF privado e não executa `loadELF()`/serviços de boot.
- A pasta legada `gow_boot_seed`, criada por versões anteriores do fluxo parcial, pode permanecer nos dados privados de instalações atualizadas; ela não é usada pelo fluxo atual. A instalação atual cria diretórios por serial e extrai a árvore ISO9660 completa, mas ainda não comprova a cadeia de boot nem a disponibilidade/cobertura AOT dos módulos IRX.
- O patch Waffle para encaminhar `cdrom0:` ao `CdRootVfs` e configurar o caminho físico em `IoPaths.cdRoot` compilou no host estrito e no target Android ARM64. A ponte para configuração de `IoPaths` está compilada; a validação efetiva desse caminho ocorre quando o app instala uma imagem. O dispatcher CDVD foi exercitado por teste host sintético; o probe Android foi compilado no run `42227233881`, mas ainda falta executá-lo em aparelho e, depois, exercitar o serviço CDVD do IOP de uma sessão real.
- Não há cobertura comprovada de VU1 AOT/IRX para o conteúdo da cópia do usuário.
- Backend GS/Vulkan Android, presenter completo e saída de áudio Android ainda não estão integrados ao ciclo de gameplay.
- Primeira imagem, menus, controles touch visuais, áudio, FMVs, saves, desempenho e compatibilidade ainda não foram testados em aparelhos reais.

## Próximos bloqueios reais — ordem de prioridade

1. Criar um smoke test em nível do PS2 VFS que abra, leia, faça seek e feche arquivos via `gowcdroot:/` usando fixtures sintéticas; a compilação host/Android do patch já foi verificada.
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
