# Créditos e avisos de terceiros

Este documento separa dependências efetivamente usadas de projetos consultados somente como referência. **Nenhum código de outro port de jogo, recompilador ou emulador foi importado para o runtime do FORGE.**

## Estado de integração

O shell Android utiliza AndroidX AppCompat, AndroidX Core e AndroidX GameActivity como dependências Maven; seus códigos não foram copiados manualmente para este repositório. Manter os avisos das versões efetivamente empacotadas.

A tentativa inicial de fixar God of War: Recomp como submódulo foi revertida por decisão do projeto. O repositório não contém submódulo, cópia ou integração de código dos projetos de referência. Black para Android, Dragon Rage/Budokai Tenkaichi 3 e os projetos de recompilação de God of War são estudados apenas em nível de arquitetura e documentação.

## Dependências integradas

| Componente | Uso | Versão / licença |
|---|---|---|
| [AndroidX AppCompat](https://developer.android.com/jetpack/androidx/releases/appcompat) | Classe-base de compatibilidade exigida pela GameActivity. | `androidx.appcompat:appcompat:1.8.0`; Apache-2.0. |
| [AndroidX Core](https://developer.android.com/jetpack/androidx/releases/core) | APIs AndroidX usadas pela camada de Activity/insets. | `androidx.core:core:1.19.1`; Apache-2.0. |
| [AndroidX GameActivity](https://developer.android.com/games/agdk/game-activity) | Activity de jogo e ponte de eventos/lifecycle para C++. | `androidx.games:games-activity:4.4.2`; Apache-2.0. |
| [Android NDK](https://developer.android.com/ndk) | Compilação nativa ARM64 e APIs Android/Vulkan. | NDK `30.0.16248370`, toolchain Android. |

## Referências consultadas — sem incorporação

| Projeto | O que foi estudado | Código incorporado |
|---|---|---|
| [Black para Android](https://github.com/RanggaCasper/Black-Android) | Recompilação estática, Vulkan, SDL3, controles e arquitetura mobile. | Nenhum. |
| [Dragon Rage / Budokai Tenkaichi 3](https://github.com/dfdx047/Tenkaichi3DecompAndroid) | Separação app/engine, instalação de ISO, organização de dados e testes em GPUs móveis. | Nenhum. |
| [God of War: Recomp](https://github.com/CodeAbhi826/God-Of-War-Recomp) | Documentação do fluxo Android, boot e subsistemas PS2. | Nenhum; submódulo removido. |
| [KIexster / Waffle565 / PS2Recomp / PCSX2](https://github.com/KIexster/god-of-war-recomp) | Documentação e comportamento de referência de God of War/PS2. | Nenhum. |

## Referências de engenharia

| Projeto / autores | Referência utilizada | Licença / condição |
|---|---|---|
| [God of War: Recomp — CodeAbhi826](https://github.com/CodeAbhi826/God-Of-War-Recomp) | Fonte da base Android/ARM64 fixada como submódulo; análise técnica da SAF, recompilador, runtime e GS/Vulkan. | GPL-3.0; manter LICENSE e THIRD_PARTY_NOTICES do submódulo. |
| [PS2Recomp — ran-j](https://github.com/ran-j/PS2Recomp) | Recompilador estático MIPS R5900 para C++ e runtime de referência. | GPL-3.0 conforme os avisos do projeto Android acima; fixar commit e preservar licença ao integrar. |
| [god-of-war-recomp — KIexster](https://github.com/KIexster/god-of-war-recomp) | Mapa de funções, configuração de recompilação, overrides e referência de testes/gameplay NTSC-U. | O README declara GPL-3.0. Confirmar commit e licença dos arquivos no momento da integração. |
| [God-Of-War-Native-Port — Waffle565](https://github.com/Waffle565/God-Of-War-Native-Port) | Referência desktop para GS/OpenGL, VU1, IOP, DMA, áudio e saves. | A auditoria não encontrou LICENSE na raiz de `main`. Não copiar arquivos diretamente até confirmar licença e proveniência do arquivo exato. |
| [PCSX2](https://github.com/PCSX2/pcsx2) | Referência comportamental para EE, VU, GS, DMA e IOP; não é o runtime final desejado. | Os avisos de CodeAbhi identificam componentes/referências PCSX2 como GPL-3.0-or-later. Validar cada arquivo antes de eventual incorporação. |

## Dependências e APIs a avaliar

| Componente | Finalidade | Ação antes da integração |
|---|---|---|
| [Vulkan](https://developer.android.com/games/develop/vulkan/native-engine-support) | Backend gráfico Android do GS. | Registrar API/headers e extensões realmente usadas. |
| [Oboe](https://github.com/google/oboe) | Saída de áudio Android de baixa latência. | Fixar versão e preservar licença/avisos. |
| [Android Frame Pacing / Swappy](https://developer.android.com/games/sdk/frame-pacing) | Sincronização do loop e apresentação de frames. | Fixar versão e preservar licença/avisos. |

## Regras de atribuição

1. Fixar o repositório e o commit/tag exato antes de importar.
2. Manter cabeçalhos de copyright e arquivos de licença originais.
3. Registrar os caminhos incorporados e as mudanças realizadas.
4. Preservar avisos de bibliotecas transitivas e dependências vendorizadas.
5. Não apresentar código de terceiros como autoria deste projeto.
6. Revisar compatibilidade entre licenças antes de combinar ou distribuir componentes.

A licença deste repositório cobre somente o código que seus autores podem licenciar. Ela não concede direitos sobre o código original, marcas, personagens ou conteúdo de God of War.
