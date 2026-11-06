# Roadmap — Gow_android

Uma fase só será considerada concluída quando houver código, build e evidência que satisfaçam os critérios de aceitação. O projeto será desenvolvido na branch `main`, com alterações pequenas e testáveis.

## Prioridade de desenvolvimento

A prioridade é fazer God of War funcionar antes de iniciar otimização fina para aparelhos modestos. O foco imediato é completar a recompilação ARM64, o runtime e os serviços PS2 necessários, integrar o GS/Vulkan, alcançar o boot real, apresentar a primeira imagem e validar menu, novo jogo e primeiro combate.

A arquitetura já deve evitar escolhas que inviabilizem o desempenho móvel (código nativo/AOT, Vulkan e I/O local), mas tuning de FPS, perfis gráficos, cache/prefetch e ajustes térmicos ficam adiados até existir gameplay funcional e uma linha de base mensurável. Black for Android é a referência para essa etapa posterior, documentada em [ANDROID_LOW_END_OPTIMIZATION.md](ANDROID_LOW_END_OPTIMIZATION.md).

## Fase 0 — Fundação e auditoria

- [x] Criar repositório público.
- [x] Confirmar estado inicial e branch `main`.
- [x] Auditar referências Android e desktop para God of War PS2.
- [x] Registrar arquitetura, créditos e política de dados.
- [x] Auditar KIexster, Waffle565 e PS2Recomp como base de código do port nativo de PC.
- [x] Importar seletivamente mapa/configuração, overrides de God of War e geradores AOT para staging, com origem registrada.

## Fase 1 — Shell Android reproduzível

- [x] Configurar Gradle, Android NDK, CMake e ABI `arm64-v8a`.
- [x] Adicionar AndroidX GameActivity e Activity nativa.
- [x] Compilar APK Debug ARM64 e confirmar build CI no commit `7ed5a254`.
- [x] Confirmar a CI do launcher SAF/ISO9660 no run `42125956300`.
- [x] Criar ícone adaptativo e launcher temático God of War e confirmar a CI no run `42128232489`.
- [x] Abrir o shell em um aparelho Android real (confirmado pelo mantenedor em 05/11/2026).
- [ ] Verificar legibilidade, proporções e ícone em outros aparelhos e dimensões.
- [x] Adicionar tela inicial e seletor SAF para a imagem do usuário.
- [x] Solicitar permissão persistente de leitura e validar o descritor primário ISO9660 do setor 16.
- [x] Implementar no launcher a leitura de SYSTEM.CNF e a identificação inicial de SCUS_973.99/SCES_531.33.
- [x] Implementar leitor ISO9660 por FD e fixture ISO sintética, com testes host na CI.
- [x] Compilar a ponte híbrida C ABI/JNI e o provider temporário `gowdisc:/` no runtime privado (Android run `42161298942`, host run `42161298950`); essa implementação é transitória e não será o caminho final após a decisão BT3.
- [x] Implementar listagem segura de diretórios ISO9660 (`listDirectory`), com limites de tamanho/quantidade e fixture sintética cobrindo raiz, subdiretório, metadados e traversal (etapa host passou no run `42163257138`; APK ainda em execução na verificação desta revisão).
- [x] Compilar o parser SYSTEM.CNF pela CI (run `42142814087`).
- [ ] Validar essa leitura com imagens de teste do usuário em aparelho Android.
- [ ] Diagnosticar permissão, formato e região.
- [ ] Exportar logs sem dados pessoais ou do jogo.

**Aceitação:** APK instala e abre em aparelho real, seleciona a imagem do usuário e informa claramente arquivos inválidos ou incompatíveis.

## Fase 2 — Recompilação estática ARM64 com base KIexster

- [x] Importar `funcmap.csv` (6.414 funções) e `recomp.template.toml` para `recompiler/config/`.
- [x] Adicionar pré-validação da tabela AOT gerada (não vazia, slots/endereço coerentes, sem duplicatas e com entrypoint do ELF registrado); fixtures de aceitação/rejeição passaram na CI host `42270355202`.
- [x] Fixar PS2Recomp no commit `c5a9d02573410a2085a4b4b831b0b68ba3515440` e integrar/buildar a ferramenta host (run `42159359104`).
- [x] Preparar o pipeline privado que recebe o ELF NTSC-U sem armazená-lo no repositório; falta executá-lo com o ELF real do usuário.
- [x] Criar o orquestrador local `tools/build_android_game.py` para validar o ELF NTSC-U privado, executar PS2Recomp, exportar C++ para staging ignorado e compilar APK ARM64; testes de planejamento/privacidade e CLI passam em CI. Execução com o ELF real do usuário ainda pendente.
- [ ] Gerar C++ usando a configuração específica de `SCUS_973.99` a partir do ELF real do usuário.
- [ ] Compilar código gerado para `arm64-v8a`; corrigir ABI, MMI/intrinsics e particularidades de ARM64.
- [ ] Comparar checkpoints/estados e produzir relatório de funções traduzidas, stubs e pendências.
- [ ] Manter ISO, ELF e C++ gerado fora do repositório público e da CI pública.

**Aceitação:** pipeline reproduzível que gera e compila código nativo ARM64 do ELF privado. O recompilador executa no ambiente de build, não no celular nem durante a partida; o runtime final não deve usar interpretação/JIT do EE.

## Fase 3 — GS no Android: reaproveitar frontend e substituir backend

- [x] Auditar o frontend GS/GIF e identificar as fronteiras OpenGL/Raylib; documentar o contrato do backend em `docs/GS_VULKAN_ANDROID.md`.
- [x] Reaproveitar a memória GS/VRAM, swizzle, transferências e CLUT da `GSTextureStore`/GSMem sem levar o contexto GL para a rota Android; o frontend GS Waffle ainda mantém dependências host que precisam ser isoladas integralmente.
- [ ] Isolar completamente o backend OpenGL/WGL/Raylib do runner Android.
- [x] Implementar o primeiro backend CPU de referência para VRAM/FBP/PSM no Android, com depth, alpha test, DATE, blending básico, PABE/FBA e CLUT; cobertura limitada e registrada em `docs/GS_VULKAN_ANDROID.md`.
- [ ] Substituir/otimizar a rota CPU de correção por renderização GS Vulkan nativa, mantendo a mesma semântica de registradores e memória.
- [x] Ligar a primeira via Vulkan de primitivas sólidas (point/line/triangle/sprite), fila thread-safe entre GS worker e surface, criação/destruição conforme `ANativeWindow` e opção ativa somente no Debug; CIs `42258381042`–`42258945494` aprovaram build ARM64 e checagens estruturais. Não é rasterizador GS completo: sem texturas/CLUT, FBP/PSM, depth, alpha/DATE, blending/fog ou composição de display.
- [x] Testar adaptador real de memória GS com `GSTextureStore`/`ps2_gs_memory.cpp`: CT32 read/write, scissor/FBMSK, HOST→LOCAL e snapshot 4 MiB aprovados na CI `42260019446`; artefato ARM64 ID `13524986210`.
- [x] Rasterizar primitivas opacas não texturizadas em VRAM FBP/FBW/PSM e apresentar o circuito CRTC básico; host test e APK ARM64 passaram na CI `42261321872`. Texturas, Z, blending e composição completa continuam pendentes.
- [x] Implementar leitura básica do circuito ativo DISPFB/DISPLAY e criar `PresentationFrame` com stride host de 640 pixels; triângulo/sprite sintéticos conferidos em CI `42261321872`.
- [x] Compor DISPLAY1/DISPLAY2, PMODE, ALP, SLBG/BGCOLOR no caminho CPU/CRTC e verificar alpha/fundo com fixtures sintéticas; interlace/overscan ainda pendentes.
- [ ] Completar interlace, overscan e demais combinações de registradores de display exigidas por cenas reais.
- [x] Preparar presenter Vulkan de frames RGBA para a GameActivity (staging `VkBuffer`, barriers e `vkCmdCopyBufferToImage`), scaler aspect-fit e invalidação do cache após recriação da swapchain; host tests e builds ARM64 aprovados nas CIs `42255177609`/`42255283925`. Esse marco **não** implementa rasterização GS nem produz frames do jogo.
- [ ] Adaptar shaders e operações para GPUs móveis; usar passes intermediários quando necessário.
- [x] Criar comparador PPM offline e captura local do primeiro `PresentationFrame` Android; testes host do gravador e comparador executados pela CI.
- [ ] Obter o primeiro frame realmente produzido pelo God of War no Android e comparar com a mesma cena no renderer de referência; AOT/boot real ainda bloqueiam esse marco.

**Aceitação:** primeira imagem real do jogo apresentada em Android e comparada com referência visual. Um clear/present isolado não conta como conclusão.

## Fase 4 — Integrar runtime e boot ao FORGE

- [ ] Selecionar e importar os módulos portáveis do runtime Waffle/KIexter (EE, memória, scheduler, stubs e serviços usados pelo título).
- [x] Adaptar o runner nativo para AndroidX GameActivity/NDK, ligar `ps2_runtime` ao alvo `android-game` e preservar o MainActivity FORGE (smoke apenas de construção/destruição; sem iniciar o runtime).
- [ ] Implementar importador BT3 no FORGE: validação da ISO/revisão, estimativa de espaço, progresso e cancelamento.
- [ ] Definir manifesto de instalação específico de God of War com base na árvore real e no rastreamento dos acessos de boot; não copiar a lista de arquivos do Dragon Rage por suposição.
- [ ] Extrair ELF, arquivos de boot e dados necessários para staging privado, preservando archives grandes nos formatos originais.
- [x] Gerar `.gow_disc_integrity` na instalação privada com SHA-256 incremental de cada arquivo ISO extraído e hashes de `.disc_layout`/`.disc_meta`; publicar o manifesto junto com a instalação atômica. Verificação segura com `openat`/`O_NOFOLLOW` e fixtures de corrupção, arquivo ausente e symlink aprovadas na CI `42237906079`.
- [x] Executar a verificação de instalações existentes fora da thread principal, informar bytes/arquivos verificados e permitir cancelamento; qualquer cancelamento ou verificação parcial permanece inválido. Teste de cancelamento e build ARM64 aprovados na CI `42240082689`.
- [x] Separar o núcleo PS2 do ciclo host de janela/áudio: `initializeCoreHeadless()` inicializa memória e liga EE/IOP/VU sem Raylib; Android `android_main` chama a etapa via C ABI. O perfil Android usa `GSNullBackend`, bloqueia `initialize()` desktop e mantém `run()`/guest execution desativados. Validador do código Waffle preparado, testes host/VFS e APK ARM64 aprovados na CI `42243707680`.
- [x] Preparar um worker EE Android separado da GameActivity: exige núcleo e entrypoint AOT, executa o scheduler numa thread própria e usa `join()` seguro no encerramento, sem `detach()`. Checker e compilação host aprovados nas CIs `42274965500`/`42274999877`.
- [x] Ligar a tentativa de primeiro boot em `android_main()`, condicionada ao ELF/AOT privado válido, backend GS anexado e surface/presenter Vulkan prontos; informar início e encerramento pelo painel JNI. O teste inicial é silencioso até existir AAudio Android. CI #362 passou no worker/GS e publicou o APK shell; as alterações posteriores de captura VU1/IOP e exportação dinâmica seguem nas CIs #381–#386.
- [x] Ligar o backend GS Android de referência (CPU→VRAM→CRTC) e o presenter Vulkan ao ciclo da GameActivity para diagnóstico de primeiro boot.
- [ ] Completar a semântica GS e a rota de renderização GPU nativa; integrar a saída SPU2 ao AAudio Android e validar som no aparelho.
- [x] Encaminhar `cdRoot`/BOOT2 do FORGE à GameActivity, sincronizar a configuração JNI com `android_main()`, resetar BOOT2 entre sessões e chamar a porta ELF/AOT protegida após a inicialização headless; CIs `42250619735` e `42252442268` aprovaram checker, testes host/VFS e APK ARM64. A tabela AOT pública continua vazia; a tentativa de worker só ocorre quando código AOT privado válido e o presenter GS/Vulkan estão presentes.
- [x] Implementar painel de diagnóstico da inicialização nativa na `GowGameActivity`, com callback JNI, atualização na thread principal e retenção de mensagens precoces; checker e build ARM64 passaram nas CIs `42250619735` e `42252442268`.
- [x] Confirmar compilação JNI ARM64 com a API real de GameActivity: referência global registrada pela Activity, referência local por callback e liberação por identidade (CI `42246883029` e builds seguintes).
- [x] Configurar armazenamento privado para capturas VU1/IOP e exportação ZIP pela GameActivity/SAF; APK ARM64 e bateria de testes do pipeline aprovados na CI #380 (artefato shell, sem dados proprietários).
- [x] Orquestrar no host os modos explícitos `--capture-bootstrap` e `--aot-captures`: preparar Waffle, compilar EE e incorporar C++ VU1/IOP validado em staging Git-ignored. A CI #380 validou fixtures/CLI; execução com o ELF real ainda pendente.
- [ ] Executar o ciclo de captura/exportação/build no aparelho com a imagem e o ELF NTSC-U do usuário; preservar ZIP/ELF/código gerado fora do Git/CI.
- [ ] Validar visualmente as mensagens de progresso/sucesso/falha em aparelho Android e conferir o Logcat correspondente.
- [ ] Configurar `PS2Runtime::loadELF()` para o ELF local e o `PS2IopHostAdapter`/`PS2Vfs` para usar o `cdRoot` local da instalação.
- [x] Criar a ponte `gowRuntimeLoadElfAndCheckAot()`: recusar tabela AOT vazia, resolver BOOT2 dentro do `cdRoot` privado, carregar o ELF quando houver AOT e conferir a função de entrada sem chamar `run()`; testes de caminho privado e CI `42250619735` aprovados. Execução com ELF/AOT real ainda não testada.
- [x] Manter `IoPaths.cdRoot` como o caminho físico da instalação privada; encaminhar `cdrom0:` no PS2 VFS antes de normalizar caminhos virtuais. A CI `42225710064` compilou o patch e verificou a configuração estática de `IoPaths` pelo JNI; o uso em boot/CDVD real ainda está pendente.
- [x] Implementar descoberta da segunda camada do DVD e enumeração recursiva de diretórios/extents nas duas camadas; fixtures ISO sintéticas cobrem extents em ambas as camadas.
- [x] Implementar o gerador `.disc_layout`/`.disc_meta` com LSNs originais, medição de espaço e publicação do par com `.disc_layout` por último; testes sintéticos isolados aprovados no run `42218565639`.
- [x] Integrar o gerador ao instalador completo `installIsoDisc()`: extrair todos os arquivos ISO9660 para staging, criar os metadados antes da publicação atômica e cancelar com limpeza segura (CI `42225710064`).
- [x] Validar o produto do instalador com a implementação real de `ps2x::iop::DiscSource` compilada da árvore Waffle: `openExtracted()`, `readSectors()`, `findFile()` e `layer1Start()` em ISO sintética de dupla camada (CI `42225710064`).
- [x] Configurar e verificar o caminho estático `PS2Runtime::IoPaths` para a instalação privada, sem iniciar o runtime; a CI compila a ponte Android ARM64 (run `42225710064`).
- [x] Exercitar `IopCdvd::dispatchImport()` em host com `IopMemory`, `IopKernel` e um `IopHost` de fixture: `sceCdSearchFile`, `sceCdRead` nas duas camadas e rejeição de LSN fora do disco (run `42226830317`).
- [x] Compilar e ligar o probe JNI `nativeValidateIopCdvd()`, que prepara a busca do BOOT2 e a leitura do setor ELF com a raiz privada; CI Android `42227233881` aprovou e publicou o APK Debug.
- [ ] Executar o probe JNI em aparelho com a ISO do usuário e conferir os diagnósticos da instalação privada.
- [ ] Disponibilizar os módulos IRX no layout `IOP_MOD/` esperado pelo jogo, sem enviar módulos do disco para Git/CI.
- [x] Testar `DiscSource::openExtracted()`, `readSectors()`, `findFile()` e `layer1Start()` com ISO sintética de dupla camada e validar a checagem JNI pós-instalação (CI `42225710064`).
- [ ] Remover o uso residual do provider ISO transitório `gowdisc:/` do caminho normal de gameplay após validar a instalação local completa; não manter um FD da ISO aberto durante a partida.
- [x] Implementar verificação da instalação existente e fluxo de reinstalação explicitamente confirmado; fechar o `cdRoot` e remover apenas a pasta privada da revisão selecionada usando `openat`/`O_NOFOLLOW`. Testes sintéticos de symlinks, traversal e preservação de diretórios irmãos aprovados na CI `42235192717`.
- [ ] Exercitar no aparelho a instalação, cancelamento, falta de espaço, reparo e verificação SHA-256 com progresso/cancelamento, além das validações `IoPaths`/DiscSource/CDVD com a ISO do usuário; medir o tempo e o I/O de leitura.
- [ ] Exercitar o CDVD já incorporado ao `IopEmulator` na sessão real e registrar chamadas originadas pelo jogo; os probes atuais ainda são sintéticos/isolados.
- [ ] Gerar/ligar código recompilado, overrides de God of War e serviços mínimos de boot; a ponte Android de carga protegida existe, mas a tabela AOT pública está vazia.
- [ ] Resolver CD/DVD, IRX, SIF, DMA e cadeia inicial de execução.
- [ ] Registrar diagnóstico por etapa, PC/RA e último subsistema confirmado.
- [ ] Apresentar a primeira imagem real do jogo.

**Aceitação:** app apresenta a imagem do jogo sem tela vazia indefinida; timeout mostra a última etapa confirmada e o erro correspondente.

## Fase 5 — VU1/IOP AOT e gameplay

- [x] Importar os geradores KIexter/Waffle de VU1 e IOP para staging.
- [ ] Capturar/obter localmente os microprogramas VU1 e módulos IRX da cópia do usuário, sem publicá-los.
- [x] Implementar geração VU1 parcial estritamente nativa (`--strict-partial`), ignorando blocos que chamariam o motor exato; integrar seis unidades geradas ao target `ps2_runtime` ARM64. A CI testa sintaxe/contrato; nenhuma captura do jogo foi compilada ainda.
- [ ] Executar `vu1n_gen.py` sobre micro-memórias reais, resolver blocos ausentes por PCs dinâmicos (`vu1n_heads.txt)) e compilar as unidades C++ resultantes para ARM64.
- [x] Ativar a política estrita `PS2X_VU1_NATIVE_ONLY` no Android: se um microprograma não tiver bloco nativo, a VU1 para com diagnóstico em vez de cair no interpretador. A compilação host/ARM64 foi aprovada em `42274965500`/`42274999877`; isso não comprova cobertura do microcódigo do jogo.
- [ ] Garantir cobertura dos microprogramas usados pelo jogo; o caminho final não pode depender do fallback interpretado.
- [x] Implementar validação FNV-1a/manifesto e geração/registro CMake de unidades IRX/IOP privadas no target Android; a CI usa módulos sintéticos.
- [ ] Executar `iopn_gen.py` sobre os módulos IRX reais necessários ao boot e compilar as unidades geradas para ARM64.
- [ ] Garantir que os IRX necessários ao jogo não caiam em caminho interpretado no produto final.
- [ ] Adaptar comandos VIF/GIF/DMA e sincronização do GS.
- [x] Implementar a ponte de eventos Android para estado virtual DualShock 2 (porta 0/slot 0), deadzone/sensibilidade/inversão e API compartilhada para entrada física/touch. A CI compila a ponte; gamepads físicos ainda não foram testados no aparelho.
- [ ] Implementar overlay visual de toque e ligar a configuração dos controles às opções do próprio jogo; não criar tela independente no FORGE.
- [ ] Validar mapeamento, gatilhos analógicos, desconexão e reconexão com controles reais em aparelho Android.
- [ ] Validar movimento, câmera, combate, HUD, partículas e efeitos.

**Aceitação:** menu, novo jogo e primeiro combate reproduzidos em Android ARM64 por código recompilado/AOT, sem interpretadores no caminho normal do jogo.

## Fase 6 — Áudio, vídeos e saves

- [ ] Reaproveitar o motor de som SPU2/LIBSD do runtime quando for independente do host.
- [ ] Substituir a saída desktop por adaptador AAudio Android.
- [ ] Validar efeitos, música e sincronização.
- [ ] Decodificar e apresentar FMVs.
- [ ] Implementar cartão de memória virtual.
- [ ] Testar salvar, carregar, exportar e importar.

**Aceitação:** áudio/vídeo sincronizados e saves persistentes, testados após fechar e reabrir o app.

## Fase 7 — Compatibilidade e otimização (após gameplay funcional)

- [ ] Testar áreas, transições, checkpoints, mortes e carregamentos em aparelhos de classes diferentes.
- [ ] Medir FPS médio, 1% low, distribuição de frame time, CPU/GPU, memória/PSS, I/O e temperatura em sessões curtas e prolongadas.
- [ ] Identificar gargalos por aparelho antes de alterar renderer, runtime, cache ou sincronização.
- [ ] Avaliar escala de resolução interna, frame pacing e perfis Econômico/Equilibrado/Qualidade sem alterar a temporização do jogo.
- [ ] Definir limites de cache e eventual prefetch com base em medições de memória e latência; evitar preload indiscriminado.
- [ ] Testar GPUs e drivers ARM64 Vulkan diferentes, incluindo throttling térmico.
- [ ] Corrigir regressões e preparar instalação/documentação.

**Referência futura:** [Black for Android](https://github.com/RanggaCasper/Black-Android). **Plano:** [Otimização para celulares modestos](ANDROID_LOW_END_OPTIMIZATION.md). Esta fase só começa depois de boot, menu, novo jogo e primeiro combate funcionarem.

**Aceitação:** progresso validado nos aparelhos publicados na matriz de compatibilidade; limitações descritas sem promessas não testadas.

## Regras de progresso

- Abrir a Activity não significa que o jogo está portado.
- Um shell APK não é uma build completa do jogo.
- O teste Vulkan clear/present isolado foi retirado do launcher após encerrar o app no aparelho do mantenedor; não será um bloqueio do projeto.
- A validação de gráficos acontecerá com o GS real e o jogo em execução.
- Reutilizar seletivamente KIexter/Waffle/PS2Recomp quando isso reduzir trabalho e for necessário ao jogo; documentar cada arquivo, commit e licença.
- Não importar o runner desktop, o renderer OpenGL/WGL ou a camada de janela/áudio/entrada de PC diretamente para Android.
- As opções de remapeamento, gamepad e controle touch do port devem ser integradas às opções/menu de pausa do próprio jogo; o FORGE não terá uma interface separada de configuração de controles.
- O caminho final não usará emulador, interpretador de EE, interpretador de VU1 ou fallback interpretado dos IRX necessários.
- O jogo será recompilado estaticamente; os serviços PS2 serão nativos e adaptados para o host Android.
- Não declarar FPS sem medições reproduzíveis.
- Usar **[VERIFICADO]**, **[HIPÓTESE]** e **[NÃO TESTADO]** em auditorias.
