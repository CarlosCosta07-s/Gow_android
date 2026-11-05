# Estado do projeto

**Data da auditoria:** 2026-11-05  
**Branch de desenvolvimento:** `main`  
**Estado global:** shell Android e smoke test de apresentação Vulkan implementados; build CI ainda em execução; nenhum gameplay integrado.

## Gow_android

**[VERIFICADO]**

- Repositório público `CarlosCosta07-s/Gow_android`.
- Branch padrão `main`.
- Fundação documental, créditos, licença e regras de proteção de dados adicionados.
- Projeto Gradle Android criado com AndroidX GameActivity 4.4.2.
- Configurado para Android API 28+ e ABI `arm64-v8a`.
- Código C++ nativo implementa criação da instância Vulkan, surface Android, seleção de GPU/filas, swapchain, render pass, framebuffers, semáforos, fence, clear pass e apresentação.
- Workflow `.github/workflows/android-shell.yml` adicionado.

**[NÃO TESTADO]**

- Compilação completa do APK.
- Instalação e execução em aparelho real.
- Apresentação de frame em dispositivo físico e recuperação após pausa/retomada.
- Seleção e leitura de ISO.
- Recompilação ou execução de código do jogo.

## CI

A execução `42124077586`, disparada por push em 2026-11-05, foi observada inicialmente em estado `in_progress` durante a etapa de localização do Android SDK. A conclusão deverá ser atualizada após consultar o resultado final da execução.

## Referência CodeAbhi826/God-Of-War-Recomp

**[VERIFICADO — documentação e arquivos públicos consultados; sem execução independente]**

- Branch `android` contém shell Android, Gradle, NDK/CMake, runtime e workflows ARM64.
- Possui seleção por SAF e validação ISO9660.
- O código Vulkan consultado é um probe, não um renderer GS completo.
- O README declara que ainda não há release jogável e identifica a ausência do GS Android renderer como bloqueio.
- O build público é shell; o build completo depende de insumos privados.

## Referências KIexster e Waffle565

**[VERIFICADO — conforme documentação pública; sem execução independente]**

- KIexster documenta mapa de funções e progresso desktop até menu/primeiro combate.
- Waffle565 documenta implementação desktop para GS/OpenGL, VU1, IOP, áudio, DMA e saves.
- Esses relatos não demonstram que o código compile ou funcione em Android ARM64.

## Bloqueios reais

1. Confirmar build do shell pela CI e executar em aparelho.
2. Adicionar seletor SAF e validação de disco/revisão.
3. Fixar base de recompilação e licenças.
4. Integrar runtime estático ARM64.
5. Ligar frontend GS ao backend Vulkan.
6. Apresentar a primeira imagem do jogo.
7. Validar controles, gameplay, áudio, FMVs e saves.

Nenhum gameplay foi testado ou declarado funcional.
