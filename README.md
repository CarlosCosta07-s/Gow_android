# God of War — Android Native Port

**Projeto independente para criar um port nativo de *God of War* (PlayStation 2, 2005), exclusivo para Android ARM64.**

> Estado atual: shell Android ARM64 com interface FORGE, ícone vetorial Ômega branco sobre vermelho e seleção SAF/ISO9660. O mantenedor confirmou a abertura do app em um aparelho Android. O runtime do jogo ainda não está integrado e não há versão jogável.

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

## FORGE — interface e ícone

**FORGE** é o nome da interface de biblioteca do projeto. Sua organização moderna inclui navegação lateral, biblioteca do jogo, ações rápidas e cartões de status. A direção visual combina tons grafite, tipografia limpa e detalhes vermelhos, com referência sutil à identidade grega de God of War I.

O ícone adaptativo é um **Ômega branco sobre fundo vermelho sólido**, desenhado como vetor original do projeto. Não são utilizados retratos de Kratos, capturas do jogo ou artes promocionais.

## Launcher e seleção de disco

A tela inicial permite selecionar uma imagem pelo Storage Access Framework, manter a permissão quando o provedor Android permite e validar o descritor primário ISO9660 no setor 16. Essa verificação **não identifica o serial nem confirma que o arquivo é God of War**; a leitura de `SYSTEM.CNF` e a identificação da revisão ainda serão implementadas. A imagem não é copiada para o aplicativo nem enviada ao GitHub.

O launcher mantém as ações de seleção de disco e início do jogo. A ação **JOGAR** ainda não inicia uma sessão: o runtime nativo precisa ser integrado primeiro. O teste Vulkan isolado foi retirado da interface após encerrar o aplicativo no aparelho do mantenedor; a validação gráfica será feita com o renderizador GS e o próprio jogo.

## Estado do código nativo

A atividade nativa usa AndroidX GameActivity. O código Vulkan atual é apenas um bring-up experimental de surface/swapchain e não é o renderizador Graphics Synthesizer do PS2. Como o teste isolado encerrou o aplicativo no aparelho do mantenedor, ele foi removido do launcher e não será usado como critério de progresso. A validação gráfica será feita com o backend GS integrado e a primeira imagem real do jogo. Ainda não há execução do código de God of War nem APK jogável.

## Referências técnicas

1. [God of War: Recomp — CodeAbhi826](https://github.com/CodeAbhi826/God-Of-War-Recomp): referência para Android ARM64, SAF, ISO9660 e NDK. O projeto original informa que seu renderer GS Android ainda não apresenta frames e que não há release jogável.
2. [god-of-war-recomp — KIexster](https://github.com/KIexster/god-of-war-recomp): mapa de funções, configuração e progresso desktop de recompilação/gameplay.
3. [God-Of-War-Native-Port — Waffle565](https://github.com/Waffle565/God-Of-War-Native-Port): referência para subsistemas GS/OpenGL, VU1, IOP, DMA, áudio e saves no PC.
4. [PS2Recomp — ran-j](https://github.com/ran-j/PS2Recomp): base de recompilação estática usada nas referências.
5. [PCSX2](https://github.com/PCSX2/pcsx2): comparação comportamental do hardware PS2, não runtime final.

A base CodeAbhi está fixada como submódulo em `third_party/gow-recomp-android`, commit `ea5f61bf61f3faf972a816cf9ba2309b0f9876ef`, sob GPL-3.0. Ela ainda não está ligada ao build do FORGE. KIexster, Waffle565, PS2Recomp e PCSX2 continuam como referências técnicas; qualquer código integrado deverá preservar autoria, licença e modificações em [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Roadmap

1. Integrar o runtime de recompilação estática e os subsistemas PS2 para Android ARM64, preservando o FORGE.
2. Completar a identificação da imagem e da revisão do jogo por SYSTEM.CNF.
3. Integrar o código recompilado ao runtime e estabelecer o fluxo de boot.
4. Desenvolver o backend GS/Vulkan Android e apresentar a primeira imagem real do jogo.
5. Substituir qualquer execução interpretada de VU1 por geração/compilação antecipada de microprogramas nativos.
6. Completar controles, áudio, FMVs, saves, validar gameplay e otimizar para GPUs móveis.

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
