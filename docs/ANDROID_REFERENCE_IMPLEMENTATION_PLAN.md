# Referências de ports Android e plano de execução — God of War

**Revisão:** 2026-11-06  
**Escopo do produto:** exclusivamente Android, ABI inicial `arm64-v8a`.  
**Objetivo imediato:** obter o primeiro boot real de God of War I (PS2), depois validar menu e gameplay.

Este documento registra conclusões de engenharia obtidas pela inspeção dos repositórios públicos Dragon Rage e Black for Android. Eles são referências de arquitetura, não bases de código para transplantar.

## 1. O que Dragon Rage demonstra

Referência: [dfdx047/Tenkaichi3DecompAndroid](https://github.com/dfdx047/Tenkaichi3DecompAndroid), baseado no [Tenkaichi3Decomp](https://github.com/z3xox/Tenkaichi3Decomp).

- O aplicativo Android e o motor do jogo são camadas distintas. O app Kotlin/Compose cuida de seleção/instalação, estado, configurações e Activity; o motor nativo é compilado para ARM64.
- O instalador Android executa a leitura e extração em contexto de I/O, mostra progresso e mantém os arquivos de dados no armazenamento privado. O código específico do jogo continua fora do APK/repositório.
- A variante BT4 tem um passo host explícito: `port/bt4/build_bt4.py` recebe dados privados do jogo, prepara revisões fixadas do recompilador, gera o código e só então cruza para Android ARM64 com NDK/CMake.
- O motor Android recebe a execução por uma Activity nativa e encaminha logs para Logcat/arquivo local. Isso dá um padrão útil para diagnósticos de boot em aparelho.
- O renderer de BT3/BT4 é específico daquele jogo e não é o backend GS de God of War.

**Lição aplicável:** manter a recompilação como ferramenta de build no host, manter os dados do usuário privados e fazer o APK Android carregar somente o runtime nativo e o código C++ ARM64 gerado para a revisão correta.

**Limite:** os arquivos, hashes, símbolos, overlays, patches, microprogramas, arquivos de dados e renderer de Budokai Tenkaichi não são compatíveis com God of War. A lista de arquivos extraídos por Dragon Rage não deve ser copiada.

## 2. O que Black for Android demonstra

Referência: [RanggaCasper/Black-Android](https://github.com/RanggaCasper/Black-Android).

- O projeto descreve a recompilação estática do executável Xbox no PC, seguida pela compilação da biblioteca ARM64 com Android NDK.
- O README exige um computador Windows com Visual Studio para a etapa host e informa que a primeira geração/compilação integral pode demorar bastante. O aparelho Android executa a biblioteca final; não é o ambiente recomendado para gerar o código do jogo.
- A Activity Android hospeda o motor SDL3; o renderer, a janela, a entrada e o áudio são adaptados ao ambiente Android.
- O app importa os dados do usuário, preserva logs locais e oferece controles touch/físicos e opções gráficas.
- O renderer Vulkan implementa o contrato gráfico do Xbox/Direct3D 8 daquele port; não pode substituir o Graphics Synthesizer do PS2.

**Lição aplicável:** separar claramente geração AOT, build Android e execução; usar uma Activity nativa estável, logs úteis e dados privados; só oferecer opções gráficas que o backend realmente implementa.

**Limite:** XboxXBE-Recomp, kernel/XDK, Direct3D 8, formatos e código de Black não executam o EE/IOP/VU/GS do PS2. Não transplantar seu runtime ou renderer.

## 3. Decisões para Gow_android

### Arquitetura final

1. **FORGE / Android:** seleção de imagem do usuário, validação da revisão, instalação privada, progresso, cancelamento, integridade e diagnóstico.
2. **Build host privado:** recebe somente a ISO original; prepara PS2Recomp/KIexster em commits fixados, extrai automaticamente SYSTEM.CNF, ELF NTSC-U, IRX, pacotes e layout por setor, e gera C++ de EE/VU1/IOP em workspace privado. Quando o aparelho Debug está conectado por ADB, as capturas VU1/IOP são sincronizadas diretamente do armazenamento privado do aplicativo, sem exportação manual.
3. **Build Android:** Android NDK/CMake compila o runtime PS2 adaptado, as unidades C++ AOT e o backend Android para `arm64-v8a`.
4. **Execução no aparelho:** GameActivity hospeda o runtime nativo; VFS/CDVD lê a instalação privada; o EE/IOP/VU executa somente o caminho nativo/AOT exigido pelo produto; GS é apresentado por Vulkan.
5. **Validação:** primeiro boot e primeiro frame reais, capturados localmente e comparados com a mesma cena/revisão no renderer de referência. Capturas e dados proprietários nunca são enviados à CI.

O computador host é apenas ferramenta de desenvolvimento/recompilação. O produto e a execução do jogo permanecem Android-only.

### Por que a extração atual não deve ser simplificada por suposição

God of War usa leituras por setor/LSN em partes do caminho de I/O. A auditoria local do projeto já confirmou dependências de layout e setores não pertencentes a arquivos, preservados em `.disc_layout` e `.disc_meta`. Dragon Rage consegue selecionar arquivos específicos para BT3 porque conhece os acessos do seu jogo. Para God of War, só podemos remover dados/setores depois de rastrear as chamadas reais e provar que o runtime continuará atendendo cada LSN. Não trocar a extração completa por uma lista copiada de outro título.

## 4. Bloqueios reais antes do primeiro boot

| Bloqueio | Estado verificado | Próxima evidência necessária |
|---|---|---|
| APK Android ARM64 | Shell compila na CI | Instalar/abrir a revisão atual no aparelho |
| Importação ISO e instalação privada | Implementadas e cobertas por fixtures | Exercitar novamente com a imagem do usuário e medir tempo |
| Exportação de ELF NTSC-U | Fluxo implementado e compila | Exportar `SCUS_973.99` no aparelho e usar o arquivo fora do telefone |
| Geração do código EE AOT | Ferramenta host compila; tabela pública segue vazia | Gerar C++ a partir do ELF privado e confirmar entry point/funções |
| Compilação do C++ gerado para ARM64 | Pipeline preparado, ainda não validado com o código do jogo | Compilar e ligar o conjunto AOT real no APK |
| VU1 AOT | Gerador e gate estrito preparados | Extrair microprogramas da cópia local, gerar blocos nativos e comprovar cobertura |
| IOP/IRX AOT | Runtime estrito preparado; módulos específicos não gerados | Extrair IRX necessários, gerar C++ nativo e validar chamadas de boot |
| Cadeia de boot PS2 | CDVD/VFS têm smoke sintético; boot real não testado | Exercitar CDVD, IRX, SIF, DMA e entry point em sessão real |
| GS/Vulkan | Presenter e rasterização experimental existem; backend GS completo não | Renderizar primeira cena real com texturas, depth, alpha/blending e composição corretos |
| Áudio Android | Não integrado ao ciclo de jogo | Implementar saída Android ou um modo silencioso explícito para diagnóstico, se o contrato do runtime permitir |

**Não remover os gates de segurança para aparentar progresso.** Uma tabela AOT vazia, uma função guest sem tradução ou uma VU/IRX sem bloco nativo precisa continuar gerando erro visível, não tela preta nem fallback interpretado silencioso.

## 5. Sequência mínima de execução

### Marco A — receber somente a ISO
- O usuário seleciona a própria ISO NTSC-U no FORGE.
- O build host também recebe somente o caminho dessa ISO via `--iso`; o extrator KIexster localiza SYSTEM.CNF e extrai automaticamente o ELF `SCUS_973.99`, os IRX, pacotes e layout por setor em workspace privado.
- O primeiro APK é bootstrap estrito. Depois de abrir Verificar runtime no aparelho, as capturas VU1/IOP ficam no armazenamento privado do app e são sincronizadas por ADB durante a próxima execução do mesmo comando.
- Não solicitar exportação de ELF, IRX, microcódigo, ZIP ou log. Não enviar dados derivados do jogo a Issues, Releases, Actions, commits ou serviços públicos.

### Marco B — produzir a build AOT Android
- Executar `tools/build_android_game.py` em Windows/Linux com o ELF privado e workspace ASCII fora do repositório.
- Validar que `register_functions.cpp`, cabeçalhos e unidades de função existem, que a tabela não está vazia e que o entry point está registrado.
- Gerar e compilar separadamente VU1/IRX AOT; o script de ELF sozinho não substitui essas etapas.
- Compilar o APK ARM64 com o C++ gerado em `android/app/build/gow-generated-game`, diretório ignorado pelo Git.

### Marco C — ligar o boot real
- Só iniciar o worker EE após núcleo, AOT, serviços CDVD/IOP/VU, backend GS/apresentação e encerramento seguro estarem válidos.
- Usar diagnósticos por subsistema, PC/RA e último estágio confirmado.
- Primeiro alvo: alcançar uma imagem estável do jogo e o menu; som, FMVs, saves e otimização podem ser etapas posteriores se o boot puder operar sem eles.

### Marco D — validar o jogo
- Capturar menu e primeira cena 3D no aparelho.
- Comparar imagens pareadas com a mesma região, resolução e estado do jogo.
- Só depois liberar novo jogo, controles, primeiro combate e perfis de desempenho.

## 6. O que pode ser feito hoje e o que depende do usuário

**Pode ser feito no repositório:** manter o fluxo de entrada ISO-only, preparação automática dos compiladores fixados, extração privada, sincronização ADB das capturas, correção de CI e integração Android.

**Depende de insumo local:** a única mídia fornecida é a ISO original. A geração AOT específica do jogo ainda é executada por uma ferramenta host Windows/Linux com CMake, compilador C++ e Android NDK; os intermediários são extraídos automaticamente e não são exportados pelo usuário.

**Depende de aparelho:** mesmo com APK AOT, somente um dispositivo Android ARM64 pode confirmar inicialização, estabilidade, driver Vulkan e a primeira cena real.

Portanto, não é tecnicamente honesto prometer gameplay hoje apenas com mudanças no GitHub. O alvo possível é encurtar o caminho até o primeiro boot e chegar ao teste real assim que ELF, VU1/IRX AOT e a build local estiverem disponíveis.

## 7. Referências inspecionadas

- [Dragon Rage Android — README em português](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/README.pt.md)
- [Dragon Rage — instalador de dados Android](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/android/app/src/main/java/com/dfdx047/dragonrage/data/GameDataInstaller.kt)
- [Dragon Rage — pipeline Android ARM64](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/port/tools/android.py)
- [Dragon Rage — build AOT Android BT4](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/port/bt4/build_bt4.py)
- [Black for Android — README e requisitos de build](https://github.com/RanggaCasper/Black-Android/blob/main/README.md)
- [Black for Android — importador Android](https://github.com/RanggaCasper/Black-Android/blob/main/app/src/main/java/com/racas/black/GameImporter.java)
- [Black for Android — host CMake/NDK/Vulkan](https://github.com/RanggaCasper/Black-Android/blob/main/CMakeLists.txt)
