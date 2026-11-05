# Estado do projeto

**Data da auditoria:** 2026-11-05  
**Branch de desenvolvimento:** `main`  
**Estado global:** shell Android ARM64 compilado na CI; launcher e validação inicial ISO9660 adicionados, aguardando confirmação de compilação da última alteração; nenhum runtime de jogo integrado.

## Gow_android

**[VERIFICADO — código e build CI]**

- Repositório público `CarlosCosta07-s/Gow_android`, branch padrão `main`.
- Fundação documental, créditos, licença e regras de proteção de dados adicionados.
- Projeto Gradle Android com AndroidX AppCompat 1.8.0, Core 1.19.1 e GameActivity 4.4.2.
- Configurado para compileSdk 37, targetSdk 36, minSdk 28 e ABI `arm64-v8a`.
- Shell C++/Vulkan com surface Android, seleção de GPU/filas, swapchain, render pass, framebuffers, semáforos, fence, clear pass e chamada de apresentação.
- A execução GitHub Actions `42125687906`, commit `7ed5a254e0fcfa3a7b77db47a2d236f047acac71`, concluiu com `success`: instalação explícita de Android SDK Platform 37.0, build `assembleDebug` e upload do APK.
- Log dessa build registra `BUILD SUCCESSFUL in 49s`, 37 tarefas (28 executadas e 9 do cache).
- O APK verificado contém `lib/arm64-v8a/libandroid-game.so` e `lib/arm64-v8a/libc++_shared.so`.

**[IMPLEMENTADO NO CÓDIGO — CI PENDENTE]**

- `MainActivity` como launcher AppCompat.
- Seletor de imagem pelo Storage Access Framework.
- Tentativa de manter permissão persistente de leitura.
- Validação do descritor primário ISO9660 (setor 16) em thread de I/O.
- Botão separado para abrir o smoke test Vulkan.
- A execução CI acionada pela alteração da Activity/manifesto é `42125956300`; no momento do último registro, ainda estava na instalação de pacotes Android. Não marcar esse código como build verificado até a execução terminar.

A validação ISO9660 só confirma que o arquivo contém um descritor primário ISO9660. Ela ainda **não identifica** o serial/revisão de God of War nem prova que o disco é compatível.

## [NÃO TESTADO]

- Instalação/abertura do APK em aparelho Android físico.
- Clear/present Vulkan em GPU móvel real e recuperação após lifecycle.
- Seleção SAF e validação em diferentes provedores de documentos reais.
- Identificação serial/revisão via `SYSTEM.CNF`.
- Recompilação estática do ELF, runtime EE/VU/IOP e execução do jogo.
- GS renderer, gameplay, áudio, FMVs, controles e saves.

## Referências

### CodeAbhi826/God-Of-War-Recomp
**[VERIFICADO — documentação e arquivos públicos consultados; sem execução independente]**

- Branch `android` contém shell Android, Gradle, NDK/CMake, runtime e workflows ARM64.
- Possui seleção SAF e validação ISO9660.
- O código Vulkan consultado é um probe, não um renderer GS completo.
- O README declara que ainda não há release jogável e identifica a ausência do GS Android renderer como bloqueio.
- O build público é shell; o build completo depende de insumos privados.

### KIexster e Waffle565
**[VERIFICADO — conforme documentação pública; sem execução independente]**

- KIexster documenta mapa de funções e progresso desktop até menu/primeiro combate.
- Waffle565 documenta implementação desktop para GS/OpenGL, VU1, IOP, áudio, DMA e saves.
- Esses relatos não demonstram que o código compile ou funcione em Android ARM64.

## Bloqueios reais

1. Confirmar a CI do launcher SAF atualizado e executar o shell em aparelho.
2. Ler `SYSTEM.CNF` para identificar serial e revisão do disco.
3. Fixar a base de recompilação e licenças.
4. Integrar runtime estático ARM64.
5. Ligar frontend GS ao backend Vulkan.
6. Apresentar a primeira imagem real do jogo.
7. Validar controles, gameplay, áudio, FMVs e saves.

Nenhum gameplay foi testado ou declarado funcional.
