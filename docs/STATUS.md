# Estado do projeto

**Data da auditoria:** 2026-11-05  
**Branch de desenvolvimento:** `main`  
**Estado global:** fundação documental; sem código executável do port neste commit.

## Gow_android

**[VERIFICADO]**

- Repositório público `CarlosCosta07-s/Gow_android`.
- Branch padrão `main`.
- Commit inicial anterior à documentação: `0089c81687cbd61069b0419d5094156c47de02f4`.
- O repositório inicial continha apenas `README.md`.
- Não havia aplicativo Android, runtime, recompilador, renderizador, APK ou testes.

Este commit organiza arquitetura, roadmap, referências, política legal e créditos. Ele não implementa o motor do jogo.

## CodeAbhi826/God-Of-War-Recomp

**[VERIFICADO — documentação e arquivos públicos consultados; sem execução independente]**

- A branch `android` contém shell Android, Gradle, NDK/CMake, runtime e workflows ARM64.
- Há seleção por SAF e validação ISO9660.
- O código Vulkan consultado é um probe e não um renderer GS completo.
- O README declara que ainda não há release jogável e identifica a ausência do GS Android renderer como bloqueio para apresentar o primeiro frame.
- O build público é somente shell; a build completa depende de insumos privados.

## KIexster e Waffle565

**[VERIFICADO — conforme documentação pública; sem execução independente]**

- KIexster documenta mapa de funções e progresso desktop até menu/primeiro combate.
- Waffle565 documenta implementação desktop para GS/OpenGL, VU1, IOP, áudio, DMA e saves.
- Esses relatos não demonstram que o código compile ou funcione em Android ARM64.

## Bloqueios reais

1. Fixar base de código e licença dos componentes.
2. Compilar shell Android reproduzível.
3. Construir apresentação Vulkan Android real.
4. Integrar recompilação estática e runtime.
5. Apresentar a primeira imagem do jogo.
6. Validar controles, gameplay, áudio, FMVs e saves.

Nenhuma build ou teste em aparelho foi executado por este commit.
