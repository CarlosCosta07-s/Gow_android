# Referências de ports Android e decisões de integração

**Revisão:** 2026-11-06  
**Escopo:** aproveitar padrões de engenharia para o port nativo de God of War (PS2), sem misturar runtimes de plataformas diferentes.

## 1. Dragon Rage — Budokai Tenkaichi 3 Android

Repositório: [dfdx047/Tenkaichi3DecompAndroid](https://github.com/dfdx047/Tenkaichi3DecompAndroid)  
Base: [Tenkaichi3Decomp](https://github.com/z3xox/Tenkaichi3Decomp)  
Documentação útil: [Input](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/docs/systems/input.md), [Menu overlay](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/docs/systems/menu_overlay.md) e [Menu support](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/docs/systems/menu_support.md).

### Padrões que valem estudar

- Separação explícita entre aplicativo Android, engine nativa e camada de plataforma.
- Importação local dos dados do disco do usuário, validação da identidade e organização privada dos arquivos para o formato esperado pela engine.
- Construção de uma biblioteca ARM64 a partir do código decompilado do jogo e das camadas do port.
- Ponte de entrada que converte eventos Android, controle físico e toque para um estado de controle estável, consumido pelo código do jogo.
- Overlay de pausa e controles de toque que convivem com a partida, sem substituir a engine do jogo.
- Diagnóstico e validação por GPU/dispositivo, em vez de presumir compatibilidade só porque o APK compilou.

O README do projeto informa que o port executa nativamente em Android ARM64 e descreve a composição da engine com o código decompilado, renderer, áudio e camada de plataforma. Isso é uma referência de implementação e estado declarado pelo mantenedor, não uma medição reproduzida neste repositório.

### O que não será transplantado

O caminho de BT3 parte de uma decompilação C de outro jogo e de uma camada de port própria. Não substitui o pipeline MIPS R5900 → C++/ARM64 de God of War baseado em PS2Recomp, nem fornece o estado de hardware, os serviços ou o código específico de God of War. Os fontes do jogo BT3 não serão copiados para este repositório.

## 2. Black — port nativo da versão Xbox original para Android

Repositório: [RanggaCasper/Black-Android](https://github.com/RanggaCasper/Black-Android)  
Arquivos especialmente relevantes: [input.cpp](https://github.com/RanggaCasper/Black-Android/blob/main/native/input.cpp), [touch.cpp](https://github.com/RanggaCasper/Black-Android/blob/main/native/touch.cpp), [menu.cpp](https://github.com/RanggaCasper/Black-Android/blob/main/src/common/menu.cpp) e [options_menu.cpp](https://github.com/RanggaCasper/Black-Android/blob/main/src/game/options_menu.cpp).

### Padrões que valem estudar

- Recompilação estática antecipada do código x86 do Xbox para C++, compilado para ARM64.
- Vulkan para apresentação gráfica e SDL3 para integração multiplataforma de janela, entrada e áudio.
- Separação entre estado de jogo, entrada física e controles de toque.
- Menu de configurações integrado ao menu de opções e de pausa do próprio jogo. O código options_menu.cpp adapta as páginas existentes e insere linhas nas opções do jogo; não depende de uma segunda tela de configurações do launcher.
- Páginas de configuração para gráficos, controles de toque, opções gerais e mouse/teclado, com persistência das preferências.

O padrão de integração de opções é especialmente relevante para a decisão de interface deste projeto.

### Limites de compatibilidade

Black é um jogo de Xbox original: sua recompilação é x86/XBE e suas interfaces gráficas pertencem ao caminho Direct3D 8/Xbox. Isso não é intercambiável com MIPS R5900, EE/VU1/IOP, GIF e GS do PlayStation 2. Não vamos reutilizar o recompilador XboxXBE-Recomp, a implementação Xbox do jogo, nem substituir o runtime PS2 por SDL/Vulkan de Black sem uma necessidade técnica demonstrada.

## 3. Decisões específicas de Gow_android

### Interface e navegação

1. **FORGE permanece como biblioteca/launcher.** Não será redesenhado por causa da integração de controles.
2. **Os controles do port serão configurados dentro das opções do próprio God of War.** A página de controles será alcançada pelo menu de opções do jogo e, quando aplicável, pelo menu de pausa.
3. Não será criada uma Activity, tela independente ou submenu de configurações do FORGE para remapeamento de botões ou configuração do controle touch.
4. O overlay de toque é uma camada de jogo durante a partida; abrir suas opções deve levar à página de controles dentro do menu do jogo, não a uma interface Android separada.

### Arquitetura de entrada planejada

- Android GameActivity recebe eventos de gamepad, teclado e toque.
- Uma camada Android converte esses eventos em um estado normalizado de controle virtual compatível com o DualShock 2.
- A camada de mapeamento aplica binds, inversão de eixos, deadzone e sensibilidade antes de entregar o estado ao subsistema de pad do runtime PS2.
- O overlay touch gera os mesmos estados virtuais que um controle físico; ele não deve escrever diretamente em estruturas internas do jogo.
- A página de opções do jogo altera os perfis e valores ativos. A persistência ficará no armazenamento privado do aplicativo, separada dos arquivos distribuídos e dos dados originais do disco.
- Reconexão de gamepad, foco/background, pausa e liberação de botões precisam gerar estados neutros consistentes para evitar botões presos.

Os detalhes de nomes de botões e limites de deadzone ainda serão definidos a partir da auditoria do caminho de entrada do runtime KIexter/Waffle e dos controles que God of War realmente consulta. Não assumir o mapa do BT3 ou Xbox como se fosse o mapa do DualShock 2 deste jogo.

### Critérios de aceitação dos controles

- Controles físicos e touch chegam ao mesmo estado de pad virtual.
- Remapeamento e ajustes podem ser alterados na página de controles das opções do jogo.
- A configuração persiste após fechar e reabrir o aplicativo.
- Pausar/retomar e desconectar/reconectar gamepad não deixam entradas presas.
- O layout de toque pode ser ajustado dentro da experiência do jogo, sem abrir um configurador externo ao jogo.
- A funcionalidade só será marcada como concluída após teste em aparelho Android real; compilar o APK não basta.

## 4. Estado atual

**[VERIFICADO]** O shell FORGE compila em ARM64 na CI. O runtime estático Waffle/KIexter foi preparado e ligado ao target Android como scaffold de integração.

**[NÃO IMPLEMENTADO]** O runtime ainda não executa God of War, o código C++ gerado a partir do ELF do usuário ainda não está presente, e não existe menu nativo do jogo integrado neste estágio.

**[DECISÃO FIXADA]** A configuração de controles do port pertencerá às opções do próprio jogo. Esta decisão deverá ser preservada quando o menu, o sistema de pad virtual e o overlay touch forem implementados.
