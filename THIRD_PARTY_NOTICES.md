# Créditos e avisos de terceiros

Este projeto reconhece os autores dos projetos, bibliotecas e bases de runtime utilizadas. Referências técnicas não significam que os componentes estejam prontos ou funcionais no nosso aplicativo.

## Estado de integração

O shell Android utiliza AndroidX AppCompat, AndroidX Core e AndroidX GameActivity como dependências Maven; seus códigos não foram copiados manualmente para este repositório. Manter os avisos das versões efetivamente empacotadas.

A base Android God of War: Recomp está fixada como submódulo Git em `third_party/gow-recomp-android`, no commit `ea5f61bf61f3faf972a816cf9ba2309b0f9876ef` da branch `android`. Neste estágio o ponteiro da fonte foi adicionado, mas o runtime ainda não foi ligado ao CMake/Gradle do FORGE e não compõe o APK. Para integrar arquivos, preservar avisos, origem, commits, licenças e modificações.

## Dependências integradas

| Componente | Uso | Versão / licença |
|---|---|---|
| [AndroidX AppCompat](https://developer.android.com/jetpack/androidx/releases/appcompat) | Classe-base de compatibilidade exigida pela GameActivity. | `androidx.appcompat:appcompat:1.8.0`; Apache-2.0. |
| [AndroidX Core](https://developer.android.com/jetpack/androidx/releases/core) | APIs AndroidX usadas pela camada de Activity/insets. | `androidx.core:core:1.19.1`; Apache-2.0. |
| [AndroidX GameActivity](https://developer.android.com/games/agdk/game-activity) | Activity de jogo e ponte de eventos/lifecycle para C++. | `androidx.games:games-activity:4.4.2`; Apache-2.0. |
| [Android NDK](https://developer.android.com/ndk) | Compilação nativa ARM64 e APIs Android/Vulkan. | NDK `30.0.16248370`, toolchain Android. |

## Código versionado como submódulo

| Componente | Uso previsto | Revisão / licença |
|---|---|---|
| [God of War: Recomp — CodeAbhi826](https://github.com/CodeAbhi826/God-Of-War-Recomp) | Base candidata do runtime Android, PS2Recomp, EE/IOP, hooks do jogo e GS/Vulkan. | Submódulo `third_party/gow-recomp-android`; commit `ea5f61bf61f3faf972a816cf9ba2309b0f9876ef`; GPL-3.0. Ainda não vinculado ao build do app. |

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
