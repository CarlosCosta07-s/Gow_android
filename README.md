# God of War — Android Native Port

**Projeto independente para criar um port nativo de *God of War* (PlayStation 2, 2005), exclusivo para Android ARM64.**

> Estado atual: shell FORGE Android ARM64 e runtime Waffle/KIexter compilados. O instalador completo ISO9660 extrai as duas camadas e gera `.disc_layout`/`.disc_meta` antes da publicação. Na CI `42225710064`, a instalação sintética foi validada com a implementação real de `DiscSource`. A CI `42226830317` também aprovou um teste host que atravessa `IopCdvd::dispatchImport()`, testando `sceCdSearchFile` e `sceCdRead` nas duas camadas. Um probe JNI para repetir a busca/leitura do BOOT2 pela implementação CDVD do IOP foi compilado e ligado no APK do run `42227233881`; ainda falta confirmar seu resultado completo em aparelho. A CI `42232085986` também aprovou o smoke sintético do `PS2Vfs` pela ponte `cdRoot` Android. A CI `42235192717` aprovou os testes de remoção segura/reparo da instalação. A CI `42237906079` aprovou o manifesto SHA-256 persistente, os testes de corrupção/arquivo ausente/symlink, o smoke VFS, a compilação ARM64 e publicou novo APK Debug. Ainda não existe boot real, C++ recompilado do ELF do usuário, execução do CDVD pelo IOP de uma sessão ou backend GS/Vulkan Android.

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

O ícone adaptativo instalado é uma **bigorna com uma faísca** sobre um fundo grafite com brilho de brasa, desenhada como vetor original, com camada monocromática para ícones temáticos do Android. O recurso do ícone do aplicativo é separado do emblema usado dentro do launcher FORGE, então a atualização do ícone não altera os elementos visuais da biblioteca. O cabeçalho do launcher mostra a hora e a bateria do aparelho. Não são utilizados retratos de Kratos, capturas do jogo ou artes promocionais.

## Controles do port dentro do jogo

A configuração de controles — gamepad físico, remapeamento, deadzone, sensibilidade e layout touch — ficará em uma página das **opções do próprio God of War**, acessível também a partir do menu de pausa quando possível. Não será criada uma interface separada de controles no FORGE. O overlay touch será exibido durante a partida, mas sua configuração será conduzida pelo menu do jogo. Consulte [as referências Android e decisões de integração](docs/REFERENCIAS_ANDROID.md).

## Launcher e seleção de disco

A tela inicial permite selecionar uma imagem pelo Storage Access Framework e preservar a permissão de leitura quando o provedor Android permite. O FORGE valida o PVD ISO9660 e identifica o executável em `SYSTEM.CNF`, reconhecendo inicialmente `SCUS_973.99` (NTSC-U) e `SCES_531.33` (PAL). A ação **INSTALAR JOGO** enumera os arquivos das duas camadas, calcula o espaço, extrai para staging privado, gera `.disc_layout`/`.disc_meta` e o manifesto `.gow_disc_integrity` com SHA-256, e publica somente após concluir. Depois, o app verifica o ELF local, configura `PS2Runtime::IoPaths.cdRoot` e testa a leitura por LSN com `DiscSource`. O host test também executa `sceCdSearchFile` e `sceCdRead` pela implementação CDVD do IOP. O smoke `42232085986` percorre o `PS2Vfs` real da revisão Waffle via provider Android, verificando abertura, leitura sequencial, `seek`, leitura aleatória, `stat`, somente leitura e fechamento com fixture sintética. O novo probe JNI que repete essa validação com a pasta privada foi compilado no APK do run `42227233881`; ainda falta executá-lo em aparelho com a ISO do usuário. A imagem não é copiada para o APK nem enviada ao GitHub.

O launcher verifica os hashes dos arquivos extraídos ao revalidar uma instalação e oferece reinstalação explicitamente confirmada quando um arquivo está ausente, alterado ou substituído por symlink. A rotina nativa fecha o provider e remove somente o diretório privado daquela instalação, rejeitando symlinks e traversal. Os testes passaram na CI `42235192717` e a verificação SHA-256 na `42237906079`. Os dois fluxos ainda precisam ser exercitados no aparelho. A ação **JOGAR** ainda não inicia uma sessão: o runtime nativo precisa ser integrado primeiro. O teste Vulkan isolado foi retirado da interface após encerrar o aplicativo no aparelho do mantenedor; a validação gráfica será feita com o renderizador GS e o próprio jogo.

## Estado do código nativo

A atividade nativa usa AndroidX GameActivity. O alvo `android-game` liga o runtime Waffle/KIexter. O FORGE passa o descritor SAF ao JNI para instalar os arquivos em armazenamento privado; em seguida, a ponte JNI configura o `IoPaths` que o `loadELF()` usará e valida a implementação real de `DiscSource::openExtracted()`. A CI `42225710064` aprovou os testes host e o APK Debug ARM64. O runner Android ainda não chama `PS2Runtime::initialize()`, `loadELF()` ou `run()`; a base ainda usa a inicialização host Raylib para gráficos/áudio. O renderer Vulkan de clear/present permanece apenas diagnóstico opcional (`GOW_ANDROID_ENABLE_VULKAN_SMOKE=OFF` por padrão) e não é o Graphics Synthesizer do PS2. O backend GS atual da base é OpenGL e a apresentação desktop depende de Raylib; o backend GS/Vulkan e presenter Android ainda não existem. Não há execução do código de God of War nem APK jogável.

## Base do port e referências de engenharia

A base principal do código específico de God of War é [KIexster/god-of-war-recomp](https://github.com/KIexster/god-of-war-recomp), commit `a226151feb6ed22937e42be485aaf28a83acd166`. Já trouxemos o mapa de 6.414 funções, o template de recompilação, os overrides do jogo e os geradores AOT de VU1/IOP. O mapa/template e as ferramentas/overrides permanecem em staging e não constituem código executável recompilado do jogo.

A CI compilou `ps2_recomp`, `ps2_iop` e `ps2_runtime` no host com os perfis estritos; no commit `836da9a`, o host [run 42161298950](https://github.com/CarlosCosta07-s/Gow_android/actions/runs/42161298950) aprovou a preparação e a compilação do recompiler/runtime, e o Android [run 42161298942](https://github.com/CarlosCosta07-s/Gow_android/actions/runs/42161298942) aprovou os testes ISO/DualShock, a preparação do runtime privado e `assembleDebug`. A tabela de funções recompiladas do APK continua vazia até o usuário gerar o C++ com seu próprio ELF e exportá-lo para `android/app/build/gow-generated-game`. O pipeline CMake encontra essas fontes recursivamente e substitui o scaffold vazio quando a tabela real existir; ainda não foi testado com um ELF de jogo. O provider ISO também compilou, mas o FORGE ainda não envia o FD e não há teste com imagem real ou gameplay.

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
- [Auditoria do GS e plano de backend Vulkan Android](docs/GS_VULKAN_ANDROID.md)
- [Leitor ISO9660, cdRoot e integração do VFS Android](docs/DISC_VFS_ANDROID.md)
- [Auditoria da fonte de setores PS2 e layout completo do disco](docs/DISC_RUNTIME_AUDIT.md)
- [Roadmap e critérios de conclusão](docs/ROADMAP.md)
- [Auditoria das referências](docs/REFERENCIAS.md)
- [Créditos e avisos de terceiros](THIRD_PARTY_NOTICES.md)
- [Política de direitos e dados](docs/LEGAL_E_DADOS.md)
- [Testes e diagnóstico](docs/TESTES_E_DIAGNOSTICO.md)
- [Estado verificado](docs/STATUS.md)

## Licença

O código original deste projeto é distribuído sob **GNU GPL-3.0-only**. Dependências e trechos reutilizados permanecem sujeitos às suas próprias licenças e avisos. O conteúdo proprietário de God of War não está incluído.

*God of War* é propriedade de seus respectivos titulares. Este é um projeto de fãs independente, sem afiliação ou endosso da Sony Interactive Entertainment ou Santa Monica Studio.
