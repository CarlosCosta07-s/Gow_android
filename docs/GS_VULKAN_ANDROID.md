# Auditoria do GS e plano de backend Vulkan Android

**Revisão:** 2026-11-06  
**Runtime auditado:** Waffle/KIexster, commit `49ca62c1d20b4659c61df6286c54e86edf411012`  
**Estado:** frontend identificado; backend GS/Vulkan ainda não implementado.

## 1. Caminho real do GS na base Waffle

O frontend público está em `ps2xRuntime/include/runtime/gs/gs_frontend.h` e `ps2xRuntime/src/lib/gs/gs_frontend.cpp`. `GS::GS()` cria por padrão `GSThreadedBackend::MakeDefault()`. Na revisão auditada, esse factory instancia:

```cpp
std::make_unique<GSThreadedBackend>(std::make_unique<GSHwBackend>())
```

`GSHwBackend` está em `ps2xRuntime/include/runtime/gs/gs_hw_backend.h` e `ps2xRuntime/src/lib/gs/gpu/gs_hw_backend.cpp`. O backend usa o subsistema `GSGL`, shaders GLSL e render targets OpenGL. `GSHwBackend::Initialize()` chama `m_impl->ensure()`, que cria/valida o contexto GL. Portanto, ligar o runtime ao APK não torna o GS compatível com Vulkan: a implementação atual continua sendo OpenGL.

O caminho de apresentação desktop é separado do frontend: `GSHostPresentation` está em `gs_host_present.h/.cpp`, inclui `rlgl.h` e trabalha com `Texture2D` de Raylib. O runner desktop mantém o contexto host, coordena o contexto GL do worker e apresenta a textura resultante. Essa camada também não pode ser chamada diretamente por GameActivity.

## 2. Contrato que o backend Android precisa cumprir

O ponto de extensão correto é `GSRasterBackend`, em `runtime/gs/gs_backend.h`. Um backend Vulkan precisa implementar todo o contrato, não apenas receber triângulos:

- ciclo de vida: `Initialize(vram, size)` e `Reset()`;
- desenho: `Submit(GSPrimitiveBatch)`, preservando o estado de draw e vértices GS;
- texturas e paleta: `LoadClut()`, cache de textura e conversão CLUT;
- transferências: `BeginTransfer()` e `UploadImage()` para GIF/GS local-to-local e host-to-local;
- sincronização: `Flush()`, `TextureFlush()` e `Sync(GSSyncReason)`;
- apresentação: `Present(GSPresentationRequest)`, incluindo PMODE, circuitos de display, framebuffers, fundo e seleção de origem;
- coerência de memória: `ClearFramebuffer()`, `ConsumeLocalToHostBytes()`, `ReadVram()`, `WriteVram()`, `SnapshotVram()` e `GetTransferSnapshot()`.

Os formatos observados no frontend incluem PSM CT32/CT24/CT16/CT16S, T8/T4/T8H/T4HL/T4HH e Z32/Z24/Z16/Z16S. As rotinas de swizzle, layout de memória GS, CLUT e decodificação de registradores podem ser reaproveitadas somente depois de confirmar que não fazem chamadas diretas a GL.

## 3. Apresentação e sincronização Vulkan

`PresentationFrame` na revisão atual contém pixels RGBA em memória CPU, largura/altura e metadados de framebuffer. Isso é suficiente para um primeiro teste de correção por readback, mas uma cópia GPU→CPU→GPU por frame não deve ser a solução de desempenho definitiva.

A arquitetura Android terá que escolher explicitamente uma destas etapas:

1. **Primeiro marco de correção:** backend GS Vulkan renderiza para imagem Vulkan própria, lê de volta um frame de apresentação e o entrega ao presenter Android. Serve para validar GS, composição e comparação de imagem; não é meta de desempenho.
2. **Marco de desempenho:** compartilhar a imagem Vulkan produzida pelo GS com o compositor de apresentação do GameActivity, com ownership, layout transitions e semáforos definidos. Não reutilizar o contrato de textura compartilhada OpenGL (`UsesSharedPresentation` / `GSSharedPresent`) como se fosse compatível com `VkImage`.

O contexto/dispositivo Vulkan, command pools, descriptors, pipelines, render targets, staging buffers e sincronização devem ter um dono claro. O worker `GSThreadedBackend` executa chamadas do backend em thread própria; portanto, criação/destruição de recursos e submissão de comandos precisam respeitar essa thread e a janela nativa. `APP_CMD_TERM_WINDOW` deve drenar trabalho, liberar recursos ligados à surface e parar a apresentação antes de o `ANativeWindow` desaparecer. Recriar a surface não pode destruir o estado GS do jogo.

## 4. Blocos gráficos obrigatórios

A implementação deverá ser dividida em blocos pequenos e comparáveis:

- alocação e cache de render targets color/depth, incluindo formatos de 16/24/32 bits;
- interpretação dos estados de rasterização GS: scissor, z-test e escrita de depth, alpha test, DATE/destination alpha, blending, color clamp, PABE, FBA, dither e write masks;
- primitivas point/line/triangle/sprite, flat shading, textura perspective-correct, filtragem e clamp/repeat;
- upload de imagem e leitura do buffer GS com swizzle, CLUT e formatos PSM;
- transferências locais/host e coerência de leitura do EE/IOP;
- composição de DISPLAY1/DISPLAY2, PMODE, ALP, BGCOLOR, modos de resolução e seleção do framebuffer;
- sincronização FINISH, local-to-host, presentation e GPU fences.

Não simplificar essas operações para um shader genérico; God of War usa transparências, efeitos e transferências do GS que dependem da semântica do hardware.

## 5. Integração com o runtime

`PS2Runtime::initialize()` ainda não é seguro para Android. O caminho Waffle atual cria janela Raylib, inicializa o host GL/GS e abre áudio pelo host. Antes da primeira inicialização real do runtime, precisamos introduzir uma camada de plataforma que receba:

- `ANativeWindow` / lifecycle de GameActivity;
- backend GS Vulkan;
- presenter Vulkan da janela Android;
- adaptador AAudio/Oboe (a avaliar) em vez do áudio Raylib/miniaudio desktop;
- paths privados do usuário e VFS para os arquivos do disco.

O construtor/destrutor do objeto `PS2Runtime` já está ligado ao loop nativo como teste de ciclo de vida; isso não significa que `initialize()`, `loadELF()` ou `run()` possam ser chamados. O smoke Vulkan de clear/present foi desativado por padrão pelo CMake (`GOW_ANDROID_ENABLE_VULKAN_SMOKE=OFF`), pois não é o GS real e já causou encerramento no aparelho do mantenedor.

## 6. Marcos de aceitação

1. Teste unitário do decodificador de registradores e transferências PSM com entradas sintéticas.
2. Backend Vulkan apresenta primitives simples e escreve corretamente no VRAM local.
3. Comparação de imagens de referência por cenário: framebuffer simples, textura/CLUT, alpha, DATE, blending, depth e transferência.
4. Primeiro frame real de God of War, comparado com a mesma cena no renderer de referência.
5. Testes em GPU Adreno e Mali, incluindo perda/recriação de surface, pausa/retorno e memória baixa.
6. Benchmark de GPU/CPU, frame pacing, memória, temperatura e readback; nenhuma meta de FPS sem medições.

**Não implementado:** nenhum dos blocos Vulkan acima está conectado ao jogo. O estado atual é o contrato auditado e a compilação do shell/runtime; ainda não há frame GS de God of War apresentado no Android.

## Estado do handoff de boot (06/11/2026)

O FORGE encaminha `cdRoot` e BOOT2 para `GowGameActivity`; a ponte JNI monta o VFS privado e sincroniza a configuração antes de a thread `android_main()` prosseguir. Depois de `initializeCoreHeadless()`, a ponte `gowRuntimeLoadElfAndCheckAot()` confere a tabela AOT, resolve o executável estritamente dentro da instalação, carrega o ELF quando a tabela está ligada e verifica a função de entrada. Não executa guest code.

CI `42250619735` aprovou a checagem estática, teste host do resolvedor BOOT2, VFS e APK ARM64. O artefato ainda precisa ser instalado/aberto em aparelho para provar o handoff Java/native. A tabela AOT no APK público permanece vazia, e não há renderer GS/Vulkan nem áudio Android conectado. Portanto, o primeiro frame real não está alcançado.
## 7. Primeiro caminho de apresentação RGBA (06/11/2026)

Foi preparada uma ponte opcional entre o contrato `PresentationFrame` do frontend GS e a swapchain Android. Em `main.cpp`, `VulkanSmokeRenderer::submitFrame()` recebe pixels RGBA, dimensões e mantém o último frame. `drawFrame()` usa um buffer de staging host-visible; a cópia ocorre por `vkCmdCopyBufferToImage` depois da transição da imagem da swapchain para `VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL`, seguida pela transição para `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`. Memória não coerente é publicada com `vkFlushMappedMemoryRanges`.

O helper `android_frame_scaler.cpp` valida dimensões/limites de memória, preserva a proporção, centraliza o conteúdo com barras pretas opacas e troca canais R/B quando a surface usa `VK_FORMAT_B8G8R8A8_UNORM`. O tamanho e os pixels escalados são armazenados; frame novo ou recriação da swapchain invalida o cache e força uma nova conversão. Se a surface não expõe `VK_IMAGE_USAGE_TRANSFER_DST_BIT` ou formato RGBA/BGRA compatível, o código volta ao clear-only do smoke.

**Evidência:** o teste host `android_frame_scaler_test.cpp` passou e o APK ARM64 compilou nas CIs `42255177609` e `42255283925`. O caminho está protegido por `GOW_ANDROID_ENABLE_VULKAN_SMOKE`, OFF por padrão. Isso prova o caminho de transferência/apresentação de um frame que já existe em CPU; não prova nem implementa o rasterizador GS.

## 8. Próximo componente necessário: GSRasterBackend Android

O runtime ainda instala `GSNullBackend` no Android. O próximo trabalho não deve modificar o smoke presenter para fingir que desenha GS. Deve fornecer uma implementação real de `GSRasterBackend` e conectá-la ao ciclo de vida da `GowGameActivity` quando o dispositivo Vulkan estiver pronto. A implementação precisa:

- manter a imagem GS de 4 MiB e operações de VRAM com o layout correto para CT32/CT24/CT16/CT16S, T8/T4 e variantes de alpha/depth; reutilizar `GSTextureStore`/`GSMem` e as rotinas de swizzle sem levar dependências OpenGL;
- respeitar `BeginTransfer`/`UploadImage`, cópias local-to-local, CLUT/CSM1/CSM2, invalidação de cache e leitura/escrita da memória que o EE acessa;
- rasterizar POINT, LINE, LINESTRIP, TRIANGLE, TRISTRIP, TRIFAN e SPRITE, incluindo interpolação de cor, STQ/UV, textura e clamp;
- implementar scissor, depth test/write, alpha test, DATE, blending AB/CD, PABE, FBA, dithering, color clamp e masks;
- compor os dois circuitos de display por PMODE/DIS​​PFB/DISPLAY/BGCOLOR, sincronizar FINISH/transferências e apresentar um `PresentationFrame` ou recurso compartilhado;
- provar cada bloco com frames de referência e fixtures sintéticas antes de habilitar `run()`.

**Não implementado:** o presenter recebe frames, mas o backend que gera esses frames ainda não existe. A tabela AOT pública também está vazia; áudio Android, VU1/IOP AOT e cadeia de boot ainda bloqueiam gameplay.