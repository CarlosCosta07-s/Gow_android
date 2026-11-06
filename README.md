# God of War — Android Native Port

**Projeto independente para criar um port nativo de *God of War* (PlayStation 2, 2005), exclusivo para Android ARM64.**

> Estado atual: shell Android ARM64 com interface FORGE, ícone vetorial Ômega branco sobre vermelho e seleção SAF/ISO9660. O mantenedor confirmou a abertura do app em um aparelho Android. O runtime do jogo ainda não está integrado e não há versão jogável.

[![Android shell](https://github.com/CarlosCosta07-s/Gow_android/actions/workflows/android-shell.yml/badge.svg)](https://github.com/CarlosCosta07-s/Gow_android/actions/workflows/android-shell.yml) [![Host recompiler](https://github.com/CarlosCosta07-s/Gow_android/actions/workflows/recompiler-tool-check.yml/badge.svg)](https://github.com/CarlosCosta07-s/Gow_android/actions/workflows/recompiler-tool-check.yml)

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

O ícone adaptativo instalado usa um **Ômega branco sobre fundo vermelho sólido**, desenhado como vetor original, com camada monocromática para ícones temáticos do Android. O recurso do ícone do aplicativo é separado do emblema usado dentro do launcher FORGE, então a atualização do ícone não altera o layout nem os elementos visuais da biblioteca. Não são utilizados retratos de Kratos, capturas do jogo ou artes promocionais.

## Controles do port dentro do jogo

A configuração de controles — gamepad físico, remapeamento, deadzone, sensibilidade e layout touch — ficará em uma página das **opções do próprio God of War**, acessível também a partir do menu de pausa quando possível. Não será criada uma interface separada de controles no FORGE. O overlay touch será exibido durante a partida, mas sua configuração será conduzida pelo menu do jogo. Consulte [as referências Android e decisões de integração](docs/REFERENCIAS_ANDROID.md).

## Launcher e seleção de disco

A tela inicial permite selecionar uma imagem pelo Storage Access Framework e preservar a permissão de leitura quando o provedor Android permite. O código agora valida o descritor ISO9660, lê o diretório raiz e identifica o executável em `SYSTEM.CNF`, reconhecendo inicialmente `SCUS_973.99` (NTSC-U) e `SCES_531.33` (PAL). **A nova leitura compilou com sucesso na CI (run `42142814087`); ainda falta validação em aparelho com imagens de teste.** A imagem não é copiada para o APK nem enviada ao GitHub.

O launcher mantém as ações de seleção de disco e início do jogo. A ação **JOGAR** ainda não inicia uma sessão: o runtime nativo precisa ser integrado primeiro. O teste Vulkan isolado foi retirado da interface após encerrar o aplicativo no aparelho do mantenedor; a validação gráfica será feita com o renderizador GS e o próprio jogo.

## Estado do código nativo

A atividade nativa usa AndroidX GameActivity. O código Vulkan atual é apenas um bring-up experimental de surface/swapchain e não é o renderizador Graphics Synthesizer do PS2. Como o teste isolado encerrou o aplicativo no aparelho do mantenedor, ele foi removido do launcher e não será usado como critério de progresso. A validação gráfica será feita com o backend GS integrado e a primeira imagem real do jogo. Ainda não há execução do código de God of War nem APK jogável.

## Base do port e referências de engenharia

A base principal do código específico de God of War será [KIexster/god-of-war-recomp](https://github.com/KIexster/god-of-war-recomp), commit `a226151feb6ed22937e42be485aaf28a83acd166`. Já trouxemos o mapa de 6.414 funções, o template de recompilação, os overrides do jogo e os geradores AOT de VU1/IOP. Os arquivos importados estão separados e ainda não foram conectados ao build Android.

A CI já compilou `ps2_recomp`, `ps2_iop` e `ps2_runtime` no host com os perfis estritos IOP/VU1 ([42150329023](https://github.com/CarlosCosta07-s/Gow_android/actions/runs/42150329023)). O runtime Waffle completo também passou pelo build cruzado Android `arm64-v8a` com NDK `30.0.16248370` e API 28; o mesmo workflow compilou o APK Debug FORGE ([42152175783](https://github.com/CarlosCosta07-s/Gow_android/actions/runs/42152175783)). A CI confirmou que o alvo estrito não inclui `iop_cpu.cpp` nem as fontes diferenciais PCSX2 e que `PS2X_VU1_NATIVE_ONLY=1` foi aplicado. O runner NativeActivity do Waffle foi excluído. Ainda falta ligar a biblioteca estática ao `android-game`, criar a ponte JNI para `GowGameActivity`, processar o ELF privado e validar IRX/microcódigo reais; não há gameplay.

O port do KIexster utiliza o runtime de [Waffle565/God-Of-War-Native-Port](https://github.com/Waffle565/God-Of-War-Native-Port) e o [PS2Recomp](https://github.com/ran-j/PS2Recomp). Vamos selecionar as partes portáveis do runtime e dos serviços do jogo. O runner desktop, a camada Raylib/Windows, o backend OpenGL e os dispositivos de entrada/áudio de PC não serão transplantados diretamente.

Referências adicionais:
1. [Black para Android](https://github.com/RanggaCasper/Black-Android): arquitetura ARM64, Vulkan, controles e integração Android.
2. [Dragon Rage / Budokai Tenkaichi 3 para Android](https://github.com/dfdx047/Tenkaichi3DecompAndroid): separação launcher/engine, instalação de dados e validação em GPUs móveis.
3. [God of War: Recomp — CodeAbhi826](https://github.com/CodeAbhi826/God-Of-War-Recomp): comparação documental de boot e Android ARM64.
4. [PCSX2](https://github.com/PCSX2/pcsx2): referência comportamental para investigar o hardware, não o runtime final.

**Estratégia:** reutilizar o que já está pronto e é necessário para God of War; adaptar a camada de plataforma para Android e criar/substituir somente o que não serve ao aparelho, principalmente o backend GS/Vulkan e a ponte de entrada/áudio. Veja [a auditoria da base KIexster](docs/BASE_KIEXSTER.md) para a origem, licença e status de cada componente.



## Roadmap

1. Validar em aparelho a identificação ISO9660/SYSTEM.CNF e preparar a leitura dos dados do disco no layout exigido pelo runtime.
2. Executar PS2Recomp com a configuração e o mapa de funções do KIexter para gerar C++ do ELF privado (o executável host já compila na CI).
3. Adaptar os módulos portáveis do runtime Waffle/KIexter e os overrides específicos do jogo para ARM64.
4. Integrar a geração AOT de VU1 e dos módulos IRX necessários, sem fallback interpretado no produto final.
5. Adaptar a entrada nativa do jogo ao Android e desenvolver o backend GS/Vulkan.
6. Chegar ao boot, primeira imagem, menus e gameplay; completar áudio, FMVs, saves e otimização móvel.

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

- [Referências de ports Android e integração dos controles no jogo](docs/REFERENCIAS_ANDROID.md)
- [Arquitetura técnica](docs/ARQUITETURA.md)
- [Base KIexter: arquivos, origem e adaptação Android](docs/BASE_KIEXTER.md)
- [Pipeline host do recompilador](docs/PIPELINE_RECOMPILADOR.md)
- [Roadmap e critérios de conclusão](docs/ROADMAP.md)
- [Auditoria das referências](docs/REFERENCIAS.md)
- [Créditos e avisos de terceiros](THIRD_PARTY_NOTICES.md)
- [Política de direitos e dados](docs/LEGAL_E_DADOS.md)
- [Testes e diagnóstico](docs/TESTES_E_DIAGNOSTICO.md)
- [Estado verificado](docs/STATUS.md)

## Licença

O código original deste projeto é distribuído sob **GNU GPL-3.0-only**. Dependências e trechos reutilizados permanecem sujeitos às suas próprias licenças e avisos. O conteúdo proprietário de God of War não está incluído.

*God of War* é propriedade de seus respectivos titulares. Este é um projeto de fãs independente, sem afiliação ou endosso da Sony Interactive Entertainment ou Santa Monica Studio.
