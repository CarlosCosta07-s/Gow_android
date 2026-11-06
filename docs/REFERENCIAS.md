# Auditoria de referências — 5 de novembro de 2026

Esta auditoria diferencia shell Android, progresso desktop e bibliotecas genéricas. Um port de PC que chega ao gameplay é uma referência valiosa, mas não prova compatibilidade Android.

## 1. God of War: Recomp — CodeAbhi826

Repositório: https://github.com/CodeAbhi826/God-Of-War-Recomp  
Branch auditada: `android`  
Commit consultado: `ea5f61bf61f3faf972a816cf9ba2309b0f9876ef`  
Licença declarada pelo projeto: GPL-3.0.

**Referências úteis:** shell Java/Android, SAF, validação ISO9660, NDK/CMake, ABI ARM64, runtime EE/VU/IOP, logs e probe Vulkan.

**Limites declarados no próprio README e confirmados na leitura dos arquivos:**

- Ainda não existe release jogável.
- O build público produz um shell APK sem dados do jogo e sem código de jogo gerado.
- O build completo depende de ELF e configuração privados.
- O README informa que o runtime chega à espera GS/DMA sem apresentar um frame.
- `ps2_android_runtime.cpp` chama o teste Vulkan de probe; não é o backend GS completo.
- A sobreposição de controles touch ainda é TODO.

**Conclusão:** melhor referência observada para o shell Android/ARM64, mas não é uma solução pronta para jogar.

## 2. god-of-war-recomp — KIexster

Repositório: https://github.com/KIexster/god-of-war-recomp  
Branch auditada: `main`  
O README declara GPL-3.0.

O README descreve recompilação estática do ELF NTSC-U `SCUS_973.99`, mapa de cerca de 6.414 funções, configuração PS2Recomp e integração de runtime nativo. Reporta boot, menus, novo jogo e combate no navio em Windows, além de progresso em VU1/IOP e renderer desktop.

**Limite:** o projeto documenta execução Windows x64/Linux x64, não um APK Android. Seu mapa/configuração e testes são referências para a parte específica do jogo; a portabilidade deve ser verificada.

## 3. God-Of-War-Native-Port — Waffle565

Repositório: https://github.com/Waffle565/God-Of-War-Native-Port  
Branch auditada: `main`.

O README descreve recompilação EE para C++, renderer GS OpenGL, VU1, IOP, DMA, serviços de pad/cartão de memória, áudio e testes de zonas/saves/FMV no Windows.

**Limites:** a documentação é voltada a Windows x64 e OpenGL desktop; não comprova funcionamento em Android. A auditoria não encontrou arquivo LICENSE na raiz da branch `main`; não copiar diretamente dessa branch até confirmar licença e proveniência do arquivo exato. O projeto CodeAbhi lista seu runtime Waffle como GPL-3.0 em seus avisos, mas isso não substitui a validação do arquivo/commit.

## 4. PS2Recomp — ran-j

Repositório: https://github.com/ran-j/PS2Recomp

Recompilador estático e runtime genérico usados como base declarada pelas referências de God of War acima. Fixar a revisão exata e manter os avisos de licença antes da integração.

## 5. PCSX2

Repositório: https://github.com/PCSX2/pcsx2

Referência de comportamento para EE, VU, GS, DMA e IOP. Não será o runtime final do projeto, pois a meta é código de jogo recompilado e serviços nativos, não um emulador genérico.

## 6. APIs oficiais Android

- [GameActivity](https://developer.android.com/games/agdk/game-activity): recomendação atual do Android para integração C/C++ em novos jogos, com lifecycle e eventos nativos.
- [Vulkan no Android](https://developer.android.com/games/develop/vulkan/native-engine-support): base gráfica de baixo nível; é necessário verificar capacidades reais de dispositivo/driver.
- [Oboe](https://github.com/google/oboe): saída de áudio C++ de baixa latência.
- [Frame Pacing / Swappy](https://developer.android.com/games/sdk/frame-pacing): sincronização do loop e apresentação.
- [ACTION_OPEN_DOCUMENT](https://developer.android.com/reference/android/content/Intent): seleção de arquivos e permissões persistentes.

## Decisão de integração

1. **Base principal do runtime Android:** adotar a branch android de CodeAbhi826/God-Of-War-Recomp como ponto de partida para PS2Recomp, runtime EE/IOP, hooks do jogo e configuração Android ARM64. Antes de importar qualquer arquivo, fixar o SHA exato do commit e conferir os avisos de terceiros.
2. **Preservar o FORGE:** manter a interface, o seletor SAF e a identidade visual deste repositório; não substituir o launcher pela interface da referência.
3. **Código específico do jogo:** usar KIexster como referência para mapa de funções, configuração e regressões NTSC-U.
4. **GS/VU1/IOP desktop:** estudar Waffle565 para entender os componentes já funcionais no PC. Não transportar diretamente o backend OpenGL desktop para Android.
5. **Compatibilidade gráfica:** completar o backend GS/Vulkan Android e validar somente com comandos e frames reais do jogo, não com um clear/present isolado.
6. **Execução final:** exigir recompilação estática do EE e VU1 compilada antecipadamente; nenhum interpretador/JIT deve permanecer no caminho final.

A base CodeAbhi está **fixada como submódulo**, mas ainda é uma candidata de integração: não está compilada pelo app FORGE nem foi demonstrada no aparelho do mantenedor. O próximo passo é adaptar a configuração CMake/Gradle e a ponte NativeActivity/SAF, preservando a interface existente.
