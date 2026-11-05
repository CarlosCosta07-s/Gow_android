# Créditos e avisos de terceiros

Este projeto reconhece os autores dos projetos e bibliotecas consultados. **Referência técnica não significa código incorporado.**

## Estado de integração

O shell Android usa a dependência Maven `androidx.games:games-activity:4.4.2`. O artefato é consumido pelo Gradle; seu código não foi copiado manualmente para este repositório. Manter a licença e os avisos distribuídos com a versão efetivamente empacotada.

Nenhum código-fonte do runtime God of War, PS2Recomp ou de outro port foi importado neste estado. As fontes abaixo foram auditadas como referências. Para cada futura integração, registrar componente, caminhos, autor(es), URL, commit/tag, licença e modificações.

## Dependência já integrada

| Componente | Uso | Versão / licença |
|---|---|---|
| [AndroidX GameActivity](https://developer.android.com/games/agdk/game-activity) | Activity de jogo e ponte de eventos/lifecycle para C++. | `androidx.games:games-activity:4.4.2`; biblioteca Jetpack distribuída sob Apache-2.0. |
| [Android NDK](https://developer.android.com/ndk) | Compilação nativa ARM64 e APIs Android/Vulkan. | NDK `30.0.16248370`, toolchain Android. |

## Referências de engenharia

| Projeto / autores | Referência utilizada | Licença / condição |
|---|---|---|
| [God of War: Recomp — CodeAbhi826](https://github.com/CodeAbhi826/God-Of-War-Recomp) | Referência Android ARM64, fluxo SAF/ISO9660, NDK/CMake, diagnóstico Vulkan e runtime PS2. Branch auditada: `android`, commit `ea5f61bf61f3faf972a816cf9ba2309b0f9876ef`. | O repositório declara GPL-3.0 e inclui LICENSE e THIRD_PARTY_NOTICES. |
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
