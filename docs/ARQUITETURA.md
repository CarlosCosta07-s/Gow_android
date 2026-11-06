# Arquitetura técnica — Gow_android

**Estado:** shell Android ARM64, launcher SAF/ISO9660, ícone adaptativo e interface FORGE compilados pela CI e abertos em aparelho Android real segundo o mantenedor; ainda não executa o jogo.

## Objetivo

Criar uma aplicação nativa para Android que execute *God of War* (PS2, 2005) por recompilação estática do código MIPS R5900 para C++/ARM64 e substitua os serviços de hardware do PS2 usados pelo jogo por implementações nativas Android.

O produto final não será um emulador genérico nem dependerá de interpretação/JIT do Emotion Engine como caminho de execução do jogo. A aplicação não incluirá a ISO ou os dados proprietários do jogo.

## Referências externas e independência de implementação

Black for Android e Dragon Rage foram indicados pelo mantenedor exclusivamente como referências de engenharia. Servem para estudar decisões de arquitetura, integração Android, importação de dados e opções expostas ao usuário; não são bases de código para este projeto.

Não importar, copiar ou transplantar código-fonte, trechos, assets, binários, scripts de build ou dependências desses dois ports. Os componentes de Gow_android — incluindo o leitor ISO9660, integração Android, VFS, runtime e backend GS/Vulkan — devem ser implementados e adaptados especificamente para a arquitetura do PlayStation 2. Referências externas não substituem análise dos contratos do PS2.

Isso não altera as dependências de código efetivamente selecionadas e auditadas separadamente para o port (Waffle/KIexster e PS2Recomp), cujas revisões, licenças e créditos permanecem documentados.

## Alvo inicial

- Plataforma exclusiva: Android.
- ABI inicial: `arm64-v8a`.
- Linguagem nativa: C++20.
- Build nativo: CMake + Android NDK `30.0.16248370`.
- Aplicativo: Gradle 9.6.0, Android Gradle Plugin 9.5.0 e AndroidX AppCompat 1.8.0, Core 1.19.1 e GameActivity 4.4.2.
- SDK de compilação: API 37; target SDK: API 36.
- API mínima inicial: Android 9 / API 28.
- Gráficos: Vulkan nativo, com surface e swapchain Android reais.
- Primeiro alvo de compatibilidade: NTSC-U, serial `SCUS-97399`, por ser a revisão mais documentada nas referências auditadas.
- PAL e outras revisões ficam para uma etapa posterior.

Essas versões fixam a primeira build de CI; serão atualizadas deliberadamente, não por dependências flutuantes.

## Estado do shell implementado

O código executável inicial do projeto está em:

- `android/app`: configuração Gradle, manifesto e Activity.
- `android/app/src/main/java/org/gowandroid/app/MainActivity.java`: launcher AppCompat temático, menu nativo, seletor SAF, validação ISO9660 e parser inicial de SYSTEM.CNF/serial.
- `android/app/src/main/res/mipmap-anydpi-v26/ic_launcher.xml`: ícone adaptativo do aplicativo.
- `android/app/src/main/res/drawable-nodpi/ic_launcher_foreground.webp`: arte original do ícone do projeto.
- `android/app/src/main/java/org/gowandroid/app/GowGameActivity.java`: Activity baseada em AndroidX GameActivity.
- `android/app/src/main/cpp/main.cpp`: ciclo nativo GameActivity, dispatch de eventos de entrada e construção/destruição controlada do runtime.
- `android/app/src/main/cpp/android_pad_bridge.cpp`: conversão atômica de buffers GameActivity em estado de controle DualShock 2.
- `.github/workflows/android-shell.yml`: build de APK Debug ARM64.

O renderer Vulkan de clear/present foi mantido apenas como diagnóstico opcional e agora fica desativado por padrão (`GOW_ANDROID_ENABLE_VULKAN_SMOKE=OFF`). Ele não é o GS do PS2 e não será usado como prova de compatibilidade. O construtor/destrutor de `PS2Runtime` está ligado ao loop nativo GameActivity como teste de ciclo de vida; `initialize()`, `loadELF()` e `run()` continuam bloqueados até os adaptadores Android de GS/apresentação, áudio e VFS estarem prontos e existir C++ gerado a partir do ELF privado. A auditoria do backend gráfico está em [GS/Vulkan Android](GS_VULKAN_ANDROID.md).

A interface FORGE e o seletor SAF/ISO9660 estão implementados. O parser de `SYSTEM.CNF` e a identificação inicial de `SCUS_973.99`/`SCES_531.33` foram compilados pela CI; ainda aguardam validação com imagens de teste em aparelho. O runtime estático Waffle/KIexster agora é compilado e ligado ao alvo `android-game`; o loop GameActivity cria/destrói `PS2Runtime`, sem iniciar o runtime nem executar instruções do jogo. Não há ELF recompilado, VFS ligado ao disco, GS Vulkan Android ou gameplay.

## Camadas futuras do port

### Otimização para aparelhos modestos — etapa posterior

A otimização fina não é a prioridade atual. Primeiro precisamos alcançar boot real, primeira imagem, menu, novo jogo e primeiro combate. Depois disso, usaremos [Black for Android](https://github.com/RanggaCasper/Black-Android) como referência de arquitetura móvel e opções gráficas, sem transplantar seu renderer/runtime Xbox para o PS2.

As medições e diretrizes futuras estão em [ANDROID_LOW_END_OPTIMIZATION.md](ANDROID_LOW_END_OPTIMIZATION.md).

### Leitor de disco Android — instalação por extração seletiva (BT3)

`iso9660_reader.cpp` valida o PVD ISO9660 e lê arquivos por extents/offset. Ele será usado pelo importador BT3 para copiar os arquivos do usuário para a área privada do app. O plano é validar a imagem, calcular espaço, extrair para staging, verificar os arquivos e publicar a instalação apenas quando estiver completa. O manifesto de God of War será definido pela estrutura real da revisão suportada e pelo rastreamento dos acessos; não vamos copiar cegamente os caminhos do Dragon Rage.

O runtime deverá usar um `cdRoot` local apontado para a instalação privada. Arquivos/pacotes grandes serão mantidos inteiros nos formatos originais e lidos sob demanda, sem preload indiscriminado na RAM. O tamanho de cache e eventual prefetch serão definidos por profiling em aparelhos reais.

A ponte híbrida `android_disc_bridge.cpp` + `gowdisc:/` no `PS2IopHostAdapter`/`PS2Vfs` já compilou na CI (Android run `42161298942`; host run `42161298950`). Ela é uma implementação transitória anterior à decisão BT3; não representa o fluxo final de execução. Será substituída no caminho normal por leitura da instalação local depois que o importador e o VFS local estiverem validados. O loader Waffle usa `std::ifstream` para o ELF, então esse executável também precisa existir em caminho privado local.

Veja [Leitura de disco e instalação](DISC_VFS_ANDROID.md) e [Otimização para celulares modestos](ANDROID_LOW_END_OPTIMIZATION.md).

### Entrada física e estado virtual de DualShock 2

O shell consome `android_input_buffer` pelos helpers de GameActivity (`android_app_swap_input_buffers`, `android_app_clear_key_events` e `android_app_clear_motion_events`), não por `android_app::onInputEvent`. `GameActivityKeyEvent` e `GameActivityMotionEvent` são convertidos pela `android_pad_bridge.cpp` para o pacote analógico ativo-baixo consumido por `PSPadBackend`. O perfil inicial implementa porta 0/slot 0, deadzone/sensibilidade/inversão por configuração e uma API para o overlay touch alimentar o mesmo estado. Ao perder foco ou pausar, o estado é limpo para evitar botões presos. A CI compila esta integração; ainda falta validação com gamepads físicos reais.

**Limite desta etapa:** a ponte de entrada está ligada ao backend Android, mas ainda não existe camada visual de touch nem página de configuração dentro do menu de God of War. A API de configuração existe para a futura página de opções; não há UI no FORGE para remapeamento.

### 1. Aplicativo Android

Responsabilidades:

- Tela inicial, seleção do disco e compatibilidade.
- Storage Access Framework para abrir arquivo escolhido pelo usuário.
- Permissão persistente de leitura quando o provedor permitir.
- Identificação da região/revisão e diagnóstico.
- Orientação horizontal, ciclo de vida, foco, pausa e recuperação da superfície.
- Configuração de controles, interface de toque e controles físicos.
- Gerenciamento de saves e exportação/importação por seletor Android.

**Decisão:** usar AndroidX GameActivity. A documentação Android recomenda GameActivity para jogos novos; ela fornece uma SurfaceView e entrega eventos/lifecycle ao código C/C++.

### 2. Disco e dados do usuário — instalação BT3

Fluxo previsto:

1. Receber a URI pelo Storage Access Framework e obter acesso de leitura durante a importação.
2. Validar PVD ISO9660, `SYSTEM.CNF`, serial/região, revisão e identidade do ELF.
3. Enumerar a estrutura da imagem e montar um manifesto específico da revisão de God of War. A seleção dos arquivos será baseada na estrutura real e nos acessos necessários ao boot, não em suposições ou na lista de outro jogo.
4. Estimar o espaço necessário e extrair para uma pasta temporária privada, com progresso, cancelamento e tratamento de erros.
5. Preservar arquivos de dados grandes como arquivos originais; não expandir archives em milhares de arquivos menores nem carregar a instalação inteira na memória.
6. Verificar tamanhos e hashes disponíveis; promover a pasta temporária para instalação ativa somente após sucesso.
7. Configurar o VFS para ler do diretório privado instalado. A partida não deve depender de manter a ISO SAF aberta.
8. Manter ISO, ELF original, IRX, dados extraídos e C++ gerado fora do repositório público, Issues, releases e CI pública.

O cache de instalação será versionado por título/revisão, versão do instalador e hash do manifesto. O leitor ISO atual já tem testes sintéticos; enumeração completa, importador BT3, extração e validação com ISO real ainda estão pendentes. Consulte [DISC_VFS_ANDROID.md](DISC_VFS_ANDROID.md).

### 3. Recompilador estático

Ferramenta do pipeline de build, não parte do loop de jogo:

- Usar PS2Recomp, na revisão fixada pelo KIexster, como base do decoder/analyzer/lifter MIPS R5900.
- Usar `recompiler/config/funcmap.csv` e `recomp.template.toml` já importados como configuração específica de `SCUS_973.99`.
- Adaptar geração e compilação do C++ resultante para `arm64-v8a`, corrigindo diferenças de ABI, intrinsics e alinhamento.
- Manter ELF, ISO, mapa derivado e C++ gerado a partir do executável original em ambiente privado, fora do Git e da CI pública.
- Registrar commit do recompilador, parâmetros, hashes dos insumos privados e relatório de funções traduzidas/pendentes.

O compilador será executado no ambiente host de build. O APK recebe apenas código nativo gerado/compilado; não interpreta nem recompila o ELF durante a partida.

### 4. Runtime EE e unidades vetoriais

A base Waffle/KIexster já contém estado de EE, memória, scheduler, stubs e serviços usados pelo executável. Vamos selecionar e adaptar esses módulos, em vez de reimplementar tudo:

- Memória, registradores, exceções, syscalls, threads, timers e interrupções do EE.
- Operações MMI e ligação das funções recompiladas aos serviços nativos.
- VU0 macro e VU1 AOT.
- Checkpoints e diagnóstico de PC, RA, SP, thread e último subsistema confirmado.

As APIs e sincronização que dependem do runner desktop serão substituídas pelo host Android. Não será mantido um caminho de interpretação/JIT do EE no produto final.

### 5. Subsistemas PS2 utilizados

Reutilizar os serviços Waffle/KIexster que já implementam os contratos utilizados pelo jogo e adaptá-los ao host Android:

- IOP/R3000A e módulos IRX; os módulos necessários devem ser gerados/compilados antecipadamente para evitar interpretador no caminho final.
- SIF, DMA, VIF e GIF, preservando o fluxo de pacotes necessário entre EE, VU1 e GS.
- CD/DVD e leitura dos arquivos no layout esperado pelo port.
- Cartão de memória virtual e operações save/load.
- SPU2/LIBSD e temporização de áudio.
- Serviços de controle que reproduzem a semântica DualShock 2.

Cada subsistema terá logs e contadores próprios. A seleção de arquivos pelo SAF precisa alimentar o VFS/cache que o runtime espera; o simples URI do ISO não satisfaz automaticamente esse contrato.

### 6. Renderizador Graphics Synthesizer (GS) via Vulkan

O código experimental de clear/present não equivale a modelar o Graphics Synthesizer e falhou ao ser iniciado no aparelho do mantenedor. Não é uma dependência para começar o trabalho no runtime; o backend GS será integrado e validado com comandos e frames reais do jogo.

Estratégia gráfica:

1. Reaproveitar o frontend de comandos GS/GIF e as rotinas de formatos, memória, CLUT e swizzle do runtime KIexster/Waffle onde não estiverem acopladas ao OpenGL.
2. Não transplantar o backend `gs_gl`, WGL nem o host Raylib do desktop.
3. Implementar o backend GS/Vulkan para Android: render targets, depth, alpha test, destination-alpha, blending, texturas e sincronização.
4. Substituir mecanismos OpenGL específicos por pipelines, descriptors, render passes/dynamic rendering e recursos Vulkan compatíveis com o aparelho.
5. Comparar a primeira imagem do jogo e cenas de referência, não um clear/present isolado.
6. Medir sincronização, filas, memória e frame time em GPUs móveis reais.

**Critério do renderer:** primeira imagem real do jogo apresentada na tela Android. Um triângulo, clear pass ou probe não conclui esta fase.

### 7. Áudio

O motor de som SPU2/LIBSD do runtime Waffle/KIexster é um candidato a reaproveitamento. A saída de áudio específica do PC será substituída por um adaptador AAudio Android. Buffers e sincronização permanecem responsabilidade do runtime; o callback não deve bloquear, alocar memória imprevisivelmente nem ler disco.

### 8. Entrada e interface

- Reaproveitar a tradução dos comandos de pad do jogo quando independente do host.
- Implementar overlay de toque configurável no FORGE/runtime Android.
- Mapear gamepad Bluetooth/USB para a semântica DualShock 2 usando eventos Android.
- Remover XInput/Win32 e chamadas de janela desktop.
- Tratar pausa, perda de foco, reconexão, retorno e feedback tátil.

### 9. Frame pacing e desempenho

Separar relógio da lógica, submissão GPU e apresentação. Usar inicialmente AChoreographer/Choreographer e os mecanismos de sincronização Vulkan do sistema; soluções externas de frame pacing só serão avaliadas se medições demonstrarem necessidade. Prioridade: estabilidade e velocidade correta; metas de FPS só serão publicadas após medições reproduzíveis.

## Estratégia de reaproveitamento seletivo

**A base principal é KIexster/god-of-war-recomp.** O objetivo é aproveitar a recompilação, configuração e comportamento específicos de God of War já trabalhados no PC, não reconstruir todo o software. Os arquivos já importados e seus hashes estão em [BASE_KIEXTER.md](BASE_KIEXTER.md).

A composição da base tem três origens que precisam permanecer identificadas:

1. **KIexster:** mapa de 6.414 funções, template de configuração, overrides específicos de God of War e ajustes/diagnósticos do port de PC.
2. **Waffle565:** runtime nativo usado pelo KIexster, incluindo serviços EE/IOP/VU1/GS, áudio, pad e memória. Os três arquivos de código importados já indicam essa proveniência nos cabeçalhos.
3. **PS2Recomp:** ferramenta de análise e recompilação estática MIPS R5900 → C++. O script KIexster fixa a revisão `c5a9d02573410a2085a4b4b831b0b68ba3515440`.

### Regra de seleção

- Reaproveitar os módulos portáveis e os hooks necessários ao jogo.
- Adaptar ou substituir tudo que dependa de Windows, Raylib desktop, OpenGL/WGL, teclado/mouse ou dispositivos de áudio do PC.
- Preservar créditos, cabeçalhos, GPL-3.0 e avisos de componentes derivados de PCSX2.
- Não importar o runtime Waffle inteiro nem aplicar a pilha de patches desktop sem separar as dependências.
- Não incluir ISO, ELF, IRX original, PAK, dumps de microcódigo ou C++ gerado a partir do ELF no repositório público.

### Caminho Android

O launcher FORGE e a seleção SAF continuam sendo nossa camada Android. O runtime será ligado à atividade nativa e receberá os dados do jogo por um cache/VFS privado compatível com o layout usado pela base KIexster. A camada gráfica usará o frontend GS que puder ser separado do host e um backend Vulkan Android; o backend OpenGL do PC não será usado diretamente.

A geração AOT de VU1 e dos módulos IRX deve ocorrer como ferramenta host, antes da compilação do APK. O CMake de referência exclui a geração VU1 quando `PS2X_IS_ANDROID` está ativo; essa etapa precisa ser separada do build Android e seus resultados compilados para ARM64. O produto final não deve recorrer a interpretadores do EE/VU1/IOP como caminho de execução.

Bibliotecas genéricas Android existentes no launcher podem continuar. Para novos componentes, priorizar APIs do sistema e adicionar dependências externas somente quando necessárias e documentadas.

## Estrutura lógica pretendida

```text
android/       Aplicativo, GameActivity, permissões e interface
native/        Runtime C++ e serviços PS2
renderer/      Frontend GS e backend Vulkan Android
recompiler/    Ferramentas de análise e recompilação estática
port/          Configuração e overrides específicos de God of War
tools/         Validação, diagnóstico e automação
docs/          Arquitetura, estado, testes, roadmap e créditos
third_party/   Somente dependências externas aprovadas e justificadas, se necessárias
```

Os diretórios de runtime/recompilação serão criados quando houver implementação real, testes e documentação. Não criaremos módulos vazios.


## Pipeline executável de geração host

O script `tools/build_recompiler_host.py` prepara o PS2Recomp na revisão fixada pelo KIexster, aplica os patches de runtime e compila a ferramenta host `ps2_recomp`. Quando o usuário fornecer seu próprio ELF, a saída C++ pode ser exportada para `android/app/build/gow-generated-game`, diretório ignorado pelo Git. O CMake Android detecta recursivamente as fontes geradas e as adiciona a `android-game`, substituindo a tabela vazia de shell quando os arquivos de registro completos estiverem presentes. A geração e a compilação efetivas ainda não foram executadas com o ELF privado do usuário.

O procedimento, dependências, versões fixadas e a triagem dos patches estão documentados em [PIPELINE_RECOMPILADOR.md](PIPELINE_RECOMPILADOR.md). A seleção lógica dos módulos do runtime está em [BASE_KIEXTER.md](BASE_KIEXTER.md), e o contrato GS está em [GS/Vulkan Android](GS_VULKAN_ANDROID.md). As execuções Android `42159359100` e `42159363088` compilaram o alvo `android-game` com o runtime Waffle estático ligado; a execução host `42159359104` aprovou a compilação das ferramentas. O loop GameActivity constrói/destrói o objeto `PS2Runtime`, mas não chama `initialize()`, `loadELF()` ou `run()`. O VFS do disco, o GS/Vulkan e a compilação do C++ gerado pelo ELF privado continuam pendentes.


### Revisão Waffle fixada para integração

A seleção do runtime usa como referência o commit Waffle `49ca62c1d20b4659c61df6286c54e86edf411012`, apontado pela documentação do KIexter para o patch-base de runtime. Não devemos misturar fontes de snapshots posteriores da branch runtime sem revalidar os contratos.

O alvo Android próprio já inclui e liga a biblioteca estática `ps2_runtime` e exclui o runner NativeActivity/Raylib. O loop GameActivity faz apenas construção/destruição do objeto. O alvo ainda usa o frontend GS Waffle cujo backend padrão é OpenGL; a camada de apresentação desktop depende de Raylib. O Android precisa de um backend `GSRasterBackend` Vulkan e presenter próprios antes de chamar `PS2Runtime::initialize()`.

A revisão também deixa explícitos dois requisitos adicionais: ps2xIOP combina módulos IRX gerados por AOT com um núcleo CPU IOP interpretado, e o runner Android upstream ainda depende da organização Raylib/NativeActivity. A integração FORGE vai precisar de um alvo Android próprio, sem o runner de desktop e sem os fallbacks de CPU.


### Política nativa obrigatória no IOP

O pipeline host prepara uma variante PS2X_IOP_NATIVE_ONLY. Nessa variante, o caminho de dispatch continua atendendo imports/HLE, overrides nativos e faixas de IRX AOT; se uma instrução não pertencer a nenhuma faixa nativa, o runtime registra PC/RA e interrompe a execução em vez de chamar o intérprete IOP. A rotina de exceções permanece disponível de forma independente, mas core/iop_cpu.cpp e o objeto IopCpuCore são removidos do alvo estrito. A CI verificou essa exclusão por compile_commands.json na execução 42148409363.

Isso é uma validação host/Linux com o registro de módulos IRX vazio. Ainda não comprova cobertura AOT real, integração do alvo ao APK nem execução em Android NDK/ARM64.
