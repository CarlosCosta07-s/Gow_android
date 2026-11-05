# Estado do projeto

**Data da auditoria:** 2026-11-05  
**Branch de desenvolvimento:** `main`  
**Estado global:** shell Android ARM64, launcher SAF/ISO9660, ícone adaptativo e interface temática compilados com sucesso na CI. Nenhum runtime do jogo está integrado.

## Gow_android

**[VERIFICADO — build CI]**

- Repositório público `CarlosCosta07-s/Gow_android`, branch padrão `main`.
- Fundação documental, créditos, licença e regras de proteção de dados adicionados.
- Projeto Gradle Android com AndroidX AppCompat 1.8.0, Core 1.19.1 e GameActivity 4.4.2.
- Configurado para compileSdk 37, targetSdk 36, minSdk 28 e ABI `arm64-v8a`.
- Shell C++/Vulkan cria surface Android, seleciona GPU/filas, cria swapchain, render pass, framebuffers, semáforos e fence, limpa a imagem e chama apresentação.
- Launcher AppCompat temático, com ícone adaptativo próprio, painel visual, botões de menu e estados de disco; mantém o seletor Storage Access Framework, tentativa de permissão persistente, validação ISO9660 no setor 16 e botão separado para o smoke test Vulkan.
- GitHub Actions `42125956300`, commit `993c1a9ec010a00db688da2affddad7dab09f07c`, concluiu com `success` para o launcher SAF/ISO9660.
- GitHub Actions `42128232489`, commit `e2094dc922cff86313a1e0f46a92bdffae74fdea`, concluiu com `success`: compilou o launcher temático, os recursos do ícone adaptativo e o APK Debug ARM64, e publicou o artefato.
- Build anterior `42125687906`, commit `7ed5a254e0fcfa3a7b77db47a2d236f047acac71`, também concluiu com sucesso.
- O APK gerado contém `lib/arm64-v8a/libandroid-game.so` e `lib/arm64-v8a/libc++_shared.so`.

A checagem ISO atual **só confirma a presença de um descritor primário ISO9660**. Ela ainda não identifica serial/revisão do jogo nem prova que o disco selecionado é God of War.

## [NÃO TESTADO — aparelho físico e jogo]

- Proporções/legibilidade da nova interface em aparelhos de dimensões distintas.
- Instalação e atualização do ícone adaptativo na tela inicial de um aparelho real.

- Instalação e abertura do APK em aparelho Android físico.
- Clear/present Vulkan em GPU móvel real e recuperação após lifecycle.
- Seleção SAF e validação com diferentes provedores de documentos Android reais.
- Identificação do serial/revisão por leitura de `SYSTEM.CNF`.
- Recompilação estática do ELF e runtime EE/VU/IOP.
- Renderer GS, primeira imagem do jogo, gameplay, áudio, FMVs, controles e saves.

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

## Próximos bloqueios reais

1. Instalar o APK em aparelho e validar launcher, SAF e Vulkan.
2. Ler `SYSTEM.CNF` para identificar serial/revisão.
3. Fixar a base de recompilação e licenças.
4. Integrar runtime estático ARM64.
5. Ligar frontend GS ao backend Vulkan.
6. Apresentar a primeira imagem real do jogo.
7. Validar controles, gameplay, áudio, FMVs e saves.

Nenhum gameplay foi testado ou declarado funcional.
