# Estado do projeto

**Data da auditoria:** 2026-11-05  
**Branch de desenvolvimento:** `main`  
**Estado global:** shell Android ARM64, interface FORGE e seletor SAF/ISO9660 compilados pela CI e abertos em um aparelho Android real, conforme relato do mantenedor. Nenhum runtime de God of War está integrado; não há versão jogável.

## Gow_android

**[VERIFICADO — build CI]**

- Repositório público `CarlosCosta07-s/Gow_android`, branch padrão `main`.
- Fundação documental, créditos, licença e regras de proteção de dados adicionados.
- Projeto Gradle Android com AndroidX AppCompat 1.8.0, Core 1.19.1 e GameActivity 4.4.2.
- Configurado para compileSdk 37, targetSdk 36, minSdk 28 e ABI `arm64-v8a`.
- Shell C++/Vulkan cria surface Android, seleciona GPU/filas, cria swapchain, render pass, framebuffers, semáforos e fence, limpa a imagem e chama apresentação.
- Launcher AppCompat temático, com ícone adaptativo próprio, painel visual, botões de menu e estados de disco; mantém o seletor Storage Access Framework, tentativa de permissão persistente e validação ISO9660 no setor 16. O botão do teste Vulkan isolado foi removido.
- GitHub Actions `42125956300`, commit `993c1a9ec010a00db688da2affddad7dab09f07c`, concluiu com `success` para o launcher SAF/ISO9660.
- GitHub Actions `42128232489`, commit `e2094dc922cff86313a1e0f46a92bdffae74fdea`, concluiu com `success`: compilou o launcher temático, os recursos do ícone adaptativo e o APK Debug ARM64, e publicou o artefato.
- Build anterior `42125687906`, commit `7ed5a254e0fcfa3a7b77db47a2d236f047acac71`, também concluiu com sucesso.
- O APK gerado contém `lib/arm64-v8a/libandroid-game.so` e `lib/arm64-v8a/libc++_shared.so`.

A checagem ISO atual **só confirma a presença de um descritor primário ISO9660**. Ela ainda não identifica serial/revisão do jogo nem prova que o disco selecionado é God of War.

## [VERIFICADO — relato do mantenedor]

- O aplicativo FORGE foi aberto em pelo menos um aparelho Android real.
- Ao acionar o antigo teste Vulkan isolado, o aplicativo fecha. O botão foi removido e essa falha não será tratada como bloqueio; a validação gráfica ocorrerá junto do renderizador GS e do jogo.

## [NÃO TESTADO — aparelho físico e jogo]

- Proporções/legibilidade da interface em aparelhos de dimensões distintas e atualização do ícone adaptativo na tela inicial.
- Seleção SAF e validação com diferentes provedores de documentos Android reais.
- Identificação do serial/revisão por leitura de `SYSTEM.CNF`.
- Recompilação estática do ELF dentro deste repositório e integração do runtime EE/IOP/VU.
- Renderizador GS/Vulkan executando comandos reais do jogo; primeira imagem, gameplay, áudio, FMVs, controles e saves.

## Referências

### CodeAbhi826/God-Of-War-Recomp
**[VERIFICADO — documentação e arquivos públicos consultados; sem execução independente]**

- Branch `android` contém shell Android, Gradle, NDK/CMake, runtime e workflows ARM64.
- Possui seleção SAF e validação ISO9660.
- O código Vulkan consultado é um probe, não um renderer GS completo.
- O README atual da branch `android` declara que ainda não há release jogável.
- O projeto público contém o recompiler, runtime, shell Android e hooks específicos; não publica ELF, dados do disco ou C++ gerado a partir do jogo.
- O README relata que o build privado chegou a executar o código recompilado do jogo no Galaxy A20, mas parou na espera GS/DMA sem apresentar frame.
- O backend GS/Vulkan Android ainda é o bloqueio principal; VU1 ainda depende de interpretador como fallback no bring-up Android, e áudio, FMV e saves seguem incompletos.
- Esta base é candidata à integração, não código já incorporado ao `Gow_android`.

### KIexster e Waffle565
**[VERIFICADO — conforme documentação pública; sem execução independente]**

- KIexster documenta mapa de funções e progresso desktop até menu/primeiro combate.
- Waffle565 documenta implementação desktop para GS/OpenGL, VU1, IOP, áudio, DMA e saves.
- Esses relatos não demonstram que o código compile ou funcione em Android ARM64.

## Próximos bloqueios reais

1. Confirmar em aparelho a seleção SAF e atualizar a leitura da imagem para identificar `SYSTEM.CNF`/serial.
2. Fixar o commit exato da base de runtime Android e registrar licença/proveniência.
3. Integrar a recompilação estática e os serviços nativos EE/IOP.
4. Garantir execução VU1 por código gerado antecipadamente, sem interpretador no caminho final.
5. Integrar o GS ao backend Vulkan e apresentar a primeira imagem real do jogo.
6. Completar controles, gameplay, áudio, FMVs e saves.

Nenhum gameplay foi testado ou declarado funcional.
