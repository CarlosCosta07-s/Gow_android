# Testes e diagnóstico

**Build CI:** o APK Debug ARM64 com ícone adaptativo e launcher temático compilou com sucesso no run `42128232489` (commit `e2094dc9`). A build anterior do seletor SAF/ISO9660 também passou no run `42125956300`. Nenhuma execução em aparelho físico foi registrada. Este documento define a evidência necessária para evitar confundir teste de shell com teste do jogo.

## Ficha por aparelho

Registrar:

- Fabricante e modelo.
- SoC e GPU.
- Versão Android/API.
- Versão Vulkan reportada pelo driver.
- ABI e RAM disponível.
- Espaço livre antes do teste.
- Versão/commit do APK.
- Região/serial do jogo, sem anexar a ISO.
- Temperatura inicial/final quando disponível.

## Portas de validação

A compilação da interface e do ícone está verificada por CI; visualização e uso em tela física continuam pendentes.

| Porta | O que precisa ser comprovado | Evidência |
|---|---|---|
| A — Instalação | APK instala, abre e fecha sem crash. | Logcat e identificação da build. |
| B — Seleção | SAF abre o arquivo e mantém a permissão. | Resultado da permissão e estado do seletor. |
| C — ISO9660 | O descritor primário do setor 16 é validado (código presente; execução em aparelho pendente). | Estado da tela e log sem caminho privado. |
| D — Identificação | `SYSTEM.CNF`, serial e revisão são reconhecidos. | Log de validação sem caminho privado. |
| E — Vulkan | Surface, swapchain, filas e primeira apresentação de teste funcionam. | Logs de criação, resize e present. |
| E1 — Diagnóstico nativo | A Activity exibe preparação, estado do núcleo e erro sem depender apenas do Logcat. | Captura do painel e Logcat com `GowAndroid`; testar mensagem precoce antes da criação da view. |
| F — Runtime | EE progride por estágios conhecidos. | PC/RA/SP, thread e última etapa. |
| G — Primeiro frame | GS apresenta imagem real do jogo. | Captura de tela e log correlacionado. |
| H — Gameplay | Menu, novo jogo, combate e transições funcionam. | Lista de cenários e resultados. |
| I — Áudio/save | Som, FMV e save/load funcionam. | Testes e integridade do save. |

## Categorias de log

- `android`: lifecycle, janela, SAF e permissões.
- `disc`: validação ISO, serial e leitura.
- `recomp`: ELF, função, instrução e geração.
- `ee`: execução Emotion Engine.
- `vu`: VU0/VU1.
- `iop`: módulos e RPC.
- `dma`: cadeias e transferências.
- `gs`: comandos, estado e framebuffer.
- `vk`: surface, device, swapchain, sincronização e apresentação.
- `audio`: SPU2, mixer e saída Android.
- `input`: toque e controles físicos.
- `save`: cartão virtual.

## Desempenho

- Não usar apenas o menu para representar desempenho do jogo.
- Registrar cena, resolução interna, FPS médio, 1% low e frame time.
- Separar lógica, CPU, submissão GPU e apresentação.
- Medir temperatura e throttling em sessões prolongadas.
- Não anunciar 30/60 FPS sem evidência reproduzível.

## Relatórios

- **[VERIFICADO]**: reproduzido com versão, aparelho e evidência identificáveis.
- **[HIPÓTESE]**: explicação provável ainda não reproduzida.
- **[NÃO TESTADO]**: sem evidência suficiente.

Remover caminhos absolutos, nomes de usuário e dados pessoais antes de publicar logs.

## Diagnóstico de inicialização da GameActivity

O callback JNI `onNativeRuntimeStatus(String, boolean)` entrega mensagens do `android_main()` à interface Java pela thread principal. O painel diferencia estado informativo e falha, e a Activity preserva a última mensagem recebida antes de o painel estar pronto.

**Estado da verificação em 06/11/2026:** a primeira tentativa de build (CI `42246181055`) falhou porque `GameActivity` não possui `activity->clazz`, campo próprio de outra API Android. O bridge foi refeito para registrar a Activity por referência JNI global, usar referências locais no callback e liberar apenas a referência da Activity correspondente. A CI `42246883029` passou pelo checker e pelos testes host/VFS; `assembleDebug` estava em andamento na última consulta. A apresentação visual e o fluxo de falha precisam ser reproduzidos em aparelho antes de marcar esta porta como [VERIFICADO].
## Handoff do FORGE e porta de carga ELF/AOT

A tela FORGE só abre `GowGameActivity` após uma instalação privada existente e a validação atual de integridade/ELF/DiscSource/CDVD. A Activity envia `cdRoot` e BOOT2 por uma única chamada JNI; `android_main()` aguarda a configuração ou a destruição da Activity antes de consultar o caminho. O runtime inicializa o núcleo headless e chama `gowRuntimeLoadElfAndCheckAot()`.

A porta de carga recusa tabela AOT vazia, normaliza e confina o BOOT2 ao diretório privado, aceita apenas arquivo regular, chama `PS2Runtime::loadELF()` apenas quando a tabela está ligada e exige a função recompilada no endereço de entrada. **Não chama `run()`**. O teste host `private_boot_path_test.cpp` cobre path válido, traversal, caminho absoluto, componente vazio, diretório, prefixo de dispositivo e symlink apontando para fora.

**[VERIFICADO NA CI, run `42250619735`]** Checker de lifecycle/JNI, testes de entrada/ISO/CDVD, VFS privado, teste de caminho BOOT2 e APK Debug ARM64 passaram. Artefato `gow-android-arm64-shell` ID `13520766214`. Ainda [NÃO TESTADO] em aparelho: abrir pelo FORGE com ISO do usuário, resultado da montagem `cdRoot`, mensagem da tabela AOT vazia e Logcat `GowAndroid`. O APK público continua não jogável.
### Sincronização e troca de sessão da GameActivity

Na CI `42252442268`, passaram o checker de lifecycle, os testes nativos/ISO/CDVD/VFS e a compilação ARM64 após estas proteções: registrar a referência JNI antes de `super.onCreate()`, zerar caminho BOOT2 ao abrir uma nova Activity, aguardar a configuração na thread `android_main()` e liberar a espera se a Activity encerrar. Isso verifica a estrutura e a compilação; não prova o handoff em um aparelho físico.
## Orquestração local do AOT

O comando `tools/build_android_game.py` prepara o build inteiro sem enviar dados do jogo: recebe somente `SCUS_973.99` fora do repositório, chama PS2Recomp, exporta C++/headers para `android/app/build/gow-generated-game/` e executa `gradlew assembleDebug`. O script recusa workspace dentro do repositório, staging não ignorado e revisão PAL com mapa NTSC-U. Consulte `docs/ANDROID_AOT_LOCAL_BUILD.md`.

**Verificado na CI:** sintaxe, CLI, planejamento dos comandos, separação de caminhos privados, rejeição do perfil PAL e chamada do Gradle Wrapper Windows. **Não verificado:** execução com o ELF real, geração C++ do jogo real e compilação do APK com essa saída. As fixtures da CI não contêm dados do jogo.
## Presenter RGBA→Vulkan — CIs 230/231

O helper `android_frame_scaler.cpp` é testado em host para dimensões e tamanho de buffer, aspect-fit, letterboxing preto opaco, preservação de pixels e conversão RGBA→BGRA. O caminho Android, quando `GOW_ANDROID_ENABLE_VULKAN_SMOKE=ON`, usa um `VkBuffer` host-visible, `vkFlushMappedMemoryRanges` para memória não coerente e barriers `UNDEFINED/PRESENT → TRANSFER_DST → PRESENT` antes de `vkCmdCopyBufferToImage`.

**[VERIFICADO NA CI]** Run `42255177609` (230): scaler host e APK ARM64 aprovados. Run `42255283925` (231): recompilação ARM64 após a invalidação do cache de upload em swapchain resize/recreate. Artefato `gow-android-arm64-shell` ID `13522097351`.

**[NÃO TESTADO EM APARELHO]** O caminho permanece desligado por padrão; nenhuma imagem de God of War foi fornecida pelo backend GS. A implementação não prova render targets, rasterização, texturas, alpha/depth ou composição GS. Um clear/present ou cópia RGBA não conta como primeira imagem do jogo.
## Pipeline de primitivas Vulkan GS — CIs 251–257

O Debug compila com `GOW_ANDROID_ENABLE_GS_VULKAN=ON`. O verificador de lifecycle confere `vkCreateGraphicsPipelines`, `vkCmdBindVertexBuffers`, `vkCmdDraw`, ligação e desligamento de `AndroidGSVulkanBackend`, e a ausência de `runtime->run()` no startup Android. CIs `42258381042`–`42258945494` compilaram o APK ARM64 e passaram o checker.

Artefato run 257: `gow-android-arm64-shell`, ID `13524346408`, expira em 13/11/2026. O pipeline é só cor sólida e não tem teste de device/GPU; não prova texturas, VRAM render targets, alpha/depth, blending nem primeira imagem do jogo.

## Teste sintético de VRAM/transferências

`android_gs_vulkan_backend_test.cpp` verifica acesso CT32, scissor/FBMSK, HOST→LOCAL e snapshot 4 MiB usando o `GSTextureStore` e `ps2_gs_memory.cpp` originais do runtime Waffle. As CIs `42259346530` e `42259354011` foram abertas para essa validação; o teste só deve ser marcado como aprovado depois de uma execução verde.
## Teste de VRAM e transferências GS — CI 261

**[APROVADO]** Run `42260019446` compilou e executou `android_gs_vulkan_backend_test.cpp` contra `GSTextureStore`/`ps2_gs_memory.cpp` do Waffle. O teste valida read/write CT32, scissor, FBMSK, transferência HOST→LOCAL e snapshot 4 MiB. A expectativa do estado final segue o contrato Waffle: `totalPixels=0`, `copiedPixels=2`, `direction=3` depois que a transferência termina.

Artefato Debug ARM64: `gow-android-arm64-shell`, ID `13524986210`, expira em 13/11/2026.

**Ainda não testado:** rasterização em FBP/PSM, apresentação por DISPFB/DISPLAY/PMODE, texturas/CLUT no shader, depth, alpha/DATE, blending, áudio e boot real. A aprovação host não substitui a validação numa GPU Android.