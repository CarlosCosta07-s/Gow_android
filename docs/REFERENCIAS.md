# Referências de engenharia — Gow_android

**Política do projeto:** referências externas serão usadas para estudar arquitetura, comportamento, métricas, organização de builds e compatibilidade. Não vamos copiar nem incorporar código-fonte dos ports, recompiladores, emuladores, runtimes, renderizadores, shaders, hooks ou sistemas de controles desses projetos. O código do FORGE e do port de God of War será desenvolvido neste repositório.

## 1. Black para Android — RanggaCasper

Repositório: https://github.com/RanggaCasper/Black-Android

O README descreve um port nativo da versão Xbox original de Black. O projeto faz recompilação estática do código x86 para C++/ARM64, usa Vulkan para desenho e SDL3 para janela, entrada e áudio. Também documenta controles touch, giroscópio, controles físicos, configurações gráficas, opções de resolução e pausa em background.

**O que estudar:** separação entre recompilador, runtime, camada Android, renderer, áudio e entrada; instalação dos dados do usuário; escalabilidade de resolução; perfis de GPU e controles configuráveis.

**O que não fazer:** não importar XboxXBE-Recomp, runtime Xbox, renderer Vulkan, SDL glue, código de controles ou shaders do projeto. Xbox Direct3D 8 e PS2 GS são arquiteturas diferentes; o renderer de Black não é uma implementação do GS.

## 2. Budokai Tenkaichi 3 para Android — Dragon Rage

Projeto: https://github.com/dfdx047/Tenkaichi3DecompAndroid

O README descreve uma aplicação Android separada da engine nativa, baseada em Tenkaichi3Decomp e no port para PC. A engine é compilada para ARM64; o launcher instala os dados do ISO do usuário e organiza arquivos, mods e saves em diretórios locais. O projeto informa execução de menus e batalhas a 30 FPS em dispositivos Snapdragon e também testes em GPUs Mali, com ajustes específicos para reduzir tráfego de memória e contornar diferenças de driver.

**O que estudar:** separação entre app e engine; verificação/checksum da imagem; cópia/organização eficiente dos dados; layout local previsível; relatórios de diagnóstico; validação em mais de uma família de GPU; otimização de memória para GPUs tile-based.

**O que não fazer:** não copiar código de Tenkaichi3Decomp, do port de PC, da engine BT3, do renderer, do sistema de áudio, dos controles, dos patches LLVM ou do app Dragon Rage. O código-fonte do jogo e sua decompilação têm situação de direitos própria, distinta da licença MIT declarada para partes do port.

## 3. God of War: Recomp — CodeAbhi826

Repositório: https://github.com/CodeAbhi826/God-Of-War-Recomp  
Branch consultada: `android`  
Commit analisado: `ea5f61bf61f3faf972a816cf9ba2309b0f9876ef`  
Licença declarada pelo projeto: GPL-3.0.

A documentação descreve o fluxo Android/ARM64, seleção SAF, leitura de ISO, extração de ELF, configuração de build e estado dos subsistemas PS2. Também informa limitações atuais do backend GS Android e a dependência de insumos privados para uma build completa.

**Uso permitido neste projeto:** referência documental para entender desafios específicos de God of War/PS2 e comparar critérios de boot. Não é a base de código do FORGE e não será importado como submódulo.

## 4. Referências desktop para God of War

- [god-of-war-recomp — KIexster](https://github.com/KIexster/god-of-war-recomp): documentação sobre mapa de funções, configuração NTSC-U e progresso de recompilação/gameplay no desktop.
- [God-Of-War-Native-Port — Waffle565](https://github.com/Waffle565/God-Of-War-Native-Port): material descritivo sobre GS/OpenGL, VU1, IOP, DMA, áudio e saves. A documentação é voltada ao PC; não prova compatibilidade Android.
- [PS2Recomp — ran-j](https://github.com/ran-j/PS2Recomp): referência conceitual para a tarefa de recompilação estática MIPS; o código do recompilador não será importado.
- [PCSX2](https://github.com/PCSX2/pcsx2): documentação e testes comportamentais sobre EE, VU, GS, DMA e IOP. Não será usado como runtime nem como fonte de código.

## 5. APIs e documentação de plataforma

- [Android NDK](https://developer.android.com/ndk): toolchain e APIs nativas.
- [Vulkan no Android](https://developer.android.com/games/develop/vulkan/native-engine-support): integração com GPU e surface Android.
- [Android NativeActivity](https://developer.android.com/reference/android/app/NativeActivity): lifecycle da atividade nativa.
- [Android GameActivity](https://developer.android.com/games/agdk/game-activity): opção de integração para jogos C/C++; a dependência atual será revista na estratégia de minimizar bibliotecas externas.
- [AAudio](https://developer.android.com/ndk/guides/audio/aaudio/aaudio): API de áudio nativa Android.
- [ACTION_OPEN_DOCUMENT](https://developer.android.com/reference/android/content/Intent): escolha de arquivos e permissões de acesso.

## Decisão de engenharia

1. Usar Black para estudar a arquitetura de recompilação estática, interface móvel, renderer e suporte a controles.
2. Usar Dragon Rage para estudar integração de engine PS2 em ARM64, instalação do disco, estrutura de dados e validação em GPUs móveis.
3. Usar CodeAbhi, KIexster e Waffle565 apenas para estudar problemas específicos de God of War e PS2.
4. Implementar nosso próprio decoder/lifter MIPS R5900, runtime de serviços PS2, frontend GS e backend Vulkan.
5. Gerar/compilar microprogramas VU1 antecipadamente, sem interpretador no caminho final.
6. Fazer testes próprios com checkpoints, logs, imagens de referência e medições em aparelho físico.

**Regra de proveniência:** nenhum trecho de código dos projetos de referência será copiado para este repositório. Dependências genéricas de plataforma serão avaliadas separadamente e documentadas com versão e licença antes de serem adicionadas.
