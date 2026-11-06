# Créditos e avisos de terceiros

Este arquivo distingue as bibliotecas externas usadas pelo shell Android de projetos consultados apenas como referência. **Nenhum código de outros ports de jogos, recompiladores ou emuladores foi incorporado ao runtime do FORGE.**

## Dependências atualmente usadas

| Componente | Uso atual | Versão / licença |
|---|---|---|
| [AndroidX AppCompat](https://developer.android.com/jetpack/androidx/releases/appcompat) | Activity e diálogos compatíveis usados pelo launcher FORGE. | `androidx.appcompat:appcompat:1.8.0`; Apache-2.0. |
| [AndroidX Core](https://developer.android.com/jetpack/androidx/releases/core) | APIs de compatibilidade da camada Android. | `androidx.core:core:1.19.1`; Apache-2.0. |
| [AndroidX GameActivity](https://developer.android.com/games/agdk/game-activity) | Activity nativa e integração de lifecycle/eventos com C++. | `androidx.games:games-activity:4.4.2`; Apache-2.0. |
| [Android NDK](https://developer.android.com/ndk) | Toolchain, headers e APIs nativas Android. | NDK `30.0.16248370`; toolchain oficial Android. |
| Vulkan | API gráfica fornecida pelo sistema/driver do aparelho. | Headers e loader do Android NDK/SO do sistema; sem renderer de terceiros incorporado. |

As dependências AndroidX são bibliotecas genéricas da plataforma e já fazem parte do launcher atual. Elas não fornecem o código do jogo, o recompilador MIPS, o runtime PS2 ou o renderer GS. Novas funcionalidades devem preferir APIs nativas Android (por exemplo, AAudio, AInputEvent e AChoreographer) sempre que forem suficientes.

## Referências consultadas — sem incorporação

| Projeto | O que foi estudado | Código incorporado |
|---|---|---|
| [Black para Android](https://github.com/RanggaCasper/Black-Android) | Recompilação estática, Vulkan, SDL3, controles e arquitetura mobile. | Nenhum. |
| [Dragon Rage / Budokai Tenkaichi 3](https://github.com/dfdx047/Tenkaichi3DecompAndroid) | Separação app/engine, instalação de ISO, organização de dados e testes em GPUs móveis. | Nenhum. |
| [God of War: Recomp](https://github.com/CodeAbhi826/God-Of-War-Recomp) | Documentação do fluxo Android, boot e subsistemas PS2. | Nenhum. A tentativa de submódulo foi revertida. |
| [KIexster / Waffle565 / PS2Recomp / PCSX2](https://github.com/KIexster/god-of-war-recomp) | Documentação e comportamento de referência de God of War/PS2. | Nenhum. |

## Política para novas dependências

1. Preferir Android SDK/NDK, CMake e APIs nativas do sistema.
2. Não adicionar código de ports, emuladores, recompiladores ou renderizadores externos.
3. Só adicionar biblioteca genérica quando houver necessidade técnica demonstrada, após revisar licença, proveniência, tamanho, manutenção e compatibilidade Android.
4. Fixar versão e registrar licença/avisos de qualquer dependência aprovada.
5. Não tratar projeto de referência como runtime nem copiar seus arquivos.

A licença deste repositório cobre apenas o código que seus autores podem licenciar. Ela não concede direitos sobre o código original, marcas, personagens ou conteúdo de God of War.
