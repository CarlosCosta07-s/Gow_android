# God of War — Android Native Port

**Projeto independente para criar um port nativo de *God of War* (PlayStation 2, 2005), exclusivo para Android ARM64.**

> Estado atual: shell Android com smoke test Vulkan implementado. O build ainda precisa ser confirmado pela CI e por aparelho real. O jogo ainda não está integrado ou jogável.

[![Android shell](https://github.com/CarlosCosta07-s/Gow_android/actions/workflows/android-shell.yml/badge.svg)](https://github.com/CarlosCosta07-s/Gow_android/actions/workflows/android-shell.yml)

## Objetivo

Construir uma aplicação Android que execute o código original de God of War por **recompilação estática MIPS R5900 → C++/ARM64**, com runtime nativo para os subsistemas de PS2 usados pelo jogo e renderização do Graphics Synthesizer por Vulkan.

Não queremos um emulador genérico, uma ISO dentro do APK ou um caminho final baseado em interpretação/JIT do Emotion Engine. O usuário selecionará seus próprios dados de jogo.

## Escopo

- **Plataforma única:** Android.
- **Arquitetura inicial:** `arm64-v8a`.
- **Gráficos:** Vulkan nativo, com surface e swapchain Android reais.
- **Código nativo:** C++20, CMake e Android NDK `30.0.16248370`.
- **Aplicativo:** AndroidX GameActivity 4.4.2.
- **Build:** Android Gradle Plugin 9.5.0 e Gradle 9.6.0.
- **SDK:** compile API 37 / target API 36; API mínima 28.
- **Primeira revisão-alvo:** NTSC-U, serial `SCUS-97399`; outras regiões serão avaliadas depois.
- **Desenvolvimento:** somente branch `main`.

## Primeiro marco implementado

O aplicativo nativo usa GameActivity e inicializa Vulkan a partir da janela Android. O código cria surface, seleciona um dispositivo e filas compatíveis, cria swapchain e render pass, limpa uma imagem e a apresenta na tela. Também trata encerramento da janela e recriação do swapchain.

Isso é um **smoke test de apresentação**, não o renderer do PS2. Ainda não executa código de God of War, não lê ISO, não possui GS, não possui controles de jogo e não é um APK jogável.

## Referências técnicas

1. [God of War: Recomp — CodeAbhi826](https://github.com/CodeAbhi826/God-Of-War-Recomp): referência para Android ARM64, SAF, ISO9660 e NDK. O projeto original informa que seu renderer GS Android ainda não apresenta frames e que não há release jogável.
2. [god-of-war-recomp — KIexster](https://github.com/KIexster/god-of-war-recomp): mapa de funções, configuração e progresso desktop de recompilação/gameplay.
3. [God-Of-War-Native-Port — Waffle565](https://github.com/Waffle565/God-Of-War-Native-Port): referência para subsistemas GS/OpenGL, VU1, IOP, DMA, áudio e saves no PC.
4. [PS2Recomp — ran-j](https://github.com/ran-j/PS2Recomp): base de recompilação estática usada nas referências.
5. [PCSX2](https://github.com/PCSX2/pcsx2): comparação comportamental do hardware PS2, não runtime final.

Esses projetos são referências, não resultados nossos. **Nenhum código de runtime desses ports foi copiado neste estado.** Toda futura integração deverá registrar commit, autoria, licença e modificações em [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Roadmap

1. Confirmar build e testar shell Vulkan em dispositivo físico.
2. Implementar seleção SAF e validação de ISO/revisão.
3. Integrar recompilação estática e runtime ARM64.
4. Integrar frontend GS e chegar à primeira imagem real do jogo.
5. Completar VU1, IOP, controles, áudio, FMVs e saves.
6. Validar gameplay e otimizar para GPUs móveis.

Veja o [roadmap detalhado](docs/ROADMAP.md).

## Construção local

Requisitos fixados no projeto: JDK 17, Android SDK Platform 37, Build Tools 37.0.0, Android NDK 30.0.16248370, CMake 3.22.1 e Gradle 9.6.0.

Com o SDK/NDK instalados e aceitos:

```bash
cd android
gradle --no-daemon assembleDebug
```

APK esperado: `android/app/build/outputs/apk/debug/app-debug.apk`.

O workflow [Android shell](.github/workflows/android-shell.yml) executa a mesma tarefa e publica o APK Debug temporariamente como artefato de CI. Esse artefato contém apenas o shell; não contém o jogo.

## Direitos e dados

Este repositório não fornece ISO, ELF, IRX, PAK, texturas, modelos, áudio, vídeos, saves nem código gerado a partir do executável original. O usuário deverá fornecer sua própria cópia legitimamente obtida e cumprir as leis aplicáveis.

A licença GPL-3.0 deste repositório cobre somente o código que seus autores podem licenciar; ela não altera os direitos sobre God of War. Consulte a [política de direitos e dados](docs/LEGAL_E_DADOS.md).

## Documentação

- [Arquitetura técnica](docs/ARQUITETURA.md)
- [Roadmap e critérios de conclusão](docs/ROADMAP.md)
- [Auditoria das referências](docs/REFERENCIAS.md)
- [Créditos e avisos de terceiros](THIRD_PARTY_NOTICES.md)
- [Política de direitos e dados](docs/LEGAL_E_DADOS.md)
- [Testes e diagnóstico](docs/TESTES_E_DIAGNOSTICO.md)
- [Estado verificado](docs/STATUS.md)

## Licença

O código original deste projeto é distribuído sob **GNU GPL-3.0-only**. Dependências e trechos reutilizados permanecem sujeitos às suas próprias licenças e avisos. O conteúdo proprietário de God of War não está incluído.

*God of War* é propriedade de seus respectivos titulares. Este é um projeto de fãs independente, sem afiliação ou endosso da Sony Interactive Entertainment ou Santa Monica Studio.
