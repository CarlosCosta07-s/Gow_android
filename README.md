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

A tela inicial permite selecionar uma imagem pelo Storage Access Framework e preservar a permissão de leitura quando o provedor Android permite. O código agora valida o descritor ISO9660, lê o diretório raiz e identifica o executável em `SYSTEM.CNF`, reconhecendo inicialmente `SCUS_973.99` (NTSC-U) e `SCES_531.33` (PAL). **A nova leitura compilou com sucesso na CI (run `42142814087`); ainda falta validação em aparelho com imagens de teste.** A imagem não é copiada para o APK nem enviada ao GitHub.

O launcher mantém as ações de seleção de disco e início do jogo. A ação **JOGAR** ainda não inicia uma sessão: o runtime nativo precisa ser integrado primeiro. O teste Vulkan isolado foi retirado da interface após encerrar o aplicativo no aparelho do mantenedor; a validação gráfica será feita com o renderizador GS e o próprio jogo.

## Estado do código nativo

A atividade nativa usa AndroidX GameActivity. O código Vulkan atual é apenas um bring-up experimental de surface/swapchain e não é o renderizador Graphics Synthesizer do PS2. Como o teste isolado encerrou o aplicativo no aparelho do mantenedor, ele foi removido do launcher e não será usado como critério de progresso. A validação gráfica será feita com o backend GS integrado e a primeira imagem real do jogo. Ainda não há execução do código de God of War nem APK jogável.

## Referências de engenharia

1. [Black para Android — RanggaCasper](https://github.com/RanggaCasper/Black-Android): referência de arquitetura para recompilação estática x86 → C++/ARM64, renderer Vulkan, camada Android, controles touch, giroscópio e gamepad.
2. [Budokai Tenkaichi 3 para Android — Dragon Rage](https://github.com/dfdx047/Tenkaichi3DecompAndroid): referência PS2/ARM64 para separar launcher e engine, validar a imagem do usuário, organizar dados locais, portar o motor gráfico e testar em GPUs móveis Snapdragon e Mali.
3. [God of War: Recomp — CodeAbhi826](https://github.com/CodeAbhi826/God-Of-War-Recomp): leitura de documentação sobre fluxo Android/ARM64, boot da imagem e os subsistemas específicos do PS2.
4. [god-of-war-recomp — KIexster](https://github.com/KIexster/god-of-war-recomp) e [God-Of-War-Native-Port — Waffle565](https://github.com/Waffle565/God-Of-War-Native-Port): material de estudo sobre comportamento do jogo, GS, VU1, IOP, DMA, áudio e saves no desktop.
5. [PS2Recomp](https://github.com/ran-j/PS2Recomp) e [PCSX2](https://github.com/PCSX2/pcsx2): documentação e comportamento de referência para pesquisa do hardware; não serão usados como runtime ou fonte de código.

**Política de implementação:** esses projetos são referências de engenharia, não dependências. Não vamos copiar, importar ou vincular código de seus launchers, recompiladores, runtimes, renderizadores, intérpretes, shaders ou hooks. O código do FORGE, do recompilador MIPS R5900, do runtime de serviços PS2 e do backend GS/Vulkan será desenvolvido neste repositório. Cada comportamento será implementado e validado por testes próprios com uma imagem fornecida pelo usuário.



## Roadmap

1. Concluir a identificação da imagem e da revisão do jogo por SYSTEM.CNF.
2. Especificar e implementar nosso próprio recompilador estático MIPS R5900 → C++/ARM64.
3. Desenvolver o runtime nativo próprio para EE, VU0/VU1, IOP, DMA, SIF, VIF e GIF, sem caminho de interpretação do jogo.
4. Desenvolver o frontend GS e o backend Vulkan próprios para Android.
5. Chegar ao boot e à primeira imagem real, com diagnóstico por etapa.
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
