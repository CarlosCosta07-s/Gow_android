# Arquitetura técnica — Gow_android

**Estado:** shell Android ARM64, launcher SAF/ISO9660, ícone adaptativo e interface FORGE compilados pela CI e abertos em aparelho Android real segundo o mantenedor; ainda não executa o jogo.

## Objetivo

Criar uma aplicação nativa para Android que execute *God of War* (PS2, 2005) por recompilação estática do código MIPS R5900 para C++/ARM64 e substitua os serviços de hardware do PS2 usados pelo jogo por implementações nativas Android.

O produto final não será um emulador genérico nem dependerá de interpretação/JIT do Emotion Engine como caminho de execução do jogo. A aplicação não incluirá a ISO ou os dados proprietários do jogo.

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
- `android/app/src/main/cpp/main.cpp`: ciclo nativo e smoke test Vulkan.
- `.github/workflows/android-shell.yml`: build de APK Debug ARM64.

O código nativo inicial em `main.cpp` ainda contém um bring-up Vulkan de clear/present. Embora compile na CI, o mantenedor observou que iniciar esse teste separado fecha o aplicativo no aparelho. Por decisão de escopo, o botão foi removido; esse teste não será tratado como critério de progresso nem como prova de compatibilidade gráfica. A atividade nativa será reaproveitada para receber o runtime real. A validação Vulkan acontecerá por meio do GS e de frames reais do jogo.

A interface FORGE e o seletor SAF/ISO9660 estão implementados. O parser de `SYSTEM.CNF` e a identificação inicial de `SCUS_973.99`/`SCES_531.33` foram adicionados ao código, mas ainda aguardam compilação CI e validação com imagens de teste em aparelho. Não há frontend GS, código recompilado do jogo ou runtime de God of War integrado.

## Camadas futuras do port

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

### 2. Disco e dados do usuário

Fluxo previsto:

1. Receber URI pelo seletor Android (`MainActivity` já implementa esta etapa).
2. Abrir o fluxo de leitura e tentar preservar a permissão concedida.
3. Validar ISO9660, descritor primário no setor 16.
4. Ler a entrada de diretório raiz e `SYSTEM.CNF`; o parser inicial já está no código e reconhece as revisões NTSC-U/PAL conhecidas, mas ainda aguarda build e validação física.
5. Ler diretamente do descritor ou preparar somente elementos de boot estritamente necessários no diretório privado.
6. Nunca enviar dados do usuário para repositório, Issues, releases ou CI pública.

A ISO não será copiada para o APK nem armazenada no Git.

### 3. Recompilador estático

Ferramenta do pipeline de desenvolvimento, não parte do loop de jogo:

- Analisar ELF de entrada do usuário.
- Identificar funções e instruções MIPS R5900.
- Gerar C++ e compilar para `arm64-v8a`.
- Registrar versão de ferramentas, mapa de funções, opções e hashes dos insumos privados.
- Manter ELF, mapas privados sensíveis e código gerado a partir do executável original fora do repositório público.

O objetivo final é execução nativa do código recompilado, sem intérprete/JIT de EE no caminho final.

### 4. Runtime EE e unidades vetoriais

Responsabilidades previstas:

- Memória, registradores, exceções, syscalls, threads, timers e interrupções do EE.
- Instruções MIPS R5900 e operações MMI necessárias ao jogo.
- VU0 em modo macro.
- VU1 e microprogramas convertidos antecipadamente, com testes de equivalência.
- Tabelas de ligação entre código recompilado e serviços nativos.

Falhas devem registrar PC, RA, SP, thread e último subsistema confirmado.

### 5. Subsistemas PS2 utilizados

Implementar somente os contratos necessários ao título:

- IOP/R3000A e módulos IRX.
- SIF, DMA, VIF e GIF.
- CD/DVD e leitura dos arquivos do disco.
- Cartão de memória virtual e operações save/load.
- SPU2/LIBSD e temporização de áudio.
- Entrada equivalente ao DualShock 2.

Cada subsistema terá logs e contadores próprios.

### 6. Renderizador Graphics Synthesizer (GS) via Vulkan

O código experimental de clear/present não equivale a modelar o Graphics Synthesizer e falhou ao ser iniciado no aparelho do mantenedor. Não é uma dependência para começar o trabalho no runtime; o backend GS será integrado e validado com comandos e frames reais do jogo.

Próximas etapas:

1. Criar um backend GS com interface independente do Android.
2. Implementar recursos de renderização usados pelo jogo.
3. Validar texturas, CLUT/paletas, swizzle, framebuffer, depth, blend e alpha test.
4. Reproduzir passagens e efeitos específicos do jogo.
5. Comparar capturas e checkpoints com referências conhecidas.
6. Medir sincronização, filas e memória em GPUs móveis reais.

**Critério do renderer:** primeira imagem real do jogo apresentada na tela Android. Um triângulo, clear pass ou probe não conclui esta fase.

### 7. Áudio

O SPU2/LIBSD deverá alimentar um mixer próprio e saída de baixa latência pela API nativa AAudio do Android. Buffers e sincronização permanecem responsabilidade do runtime. O callback não deve bloquear, alocar memória imprevisivelmente nem ler disco.

### 8. Entrada e interface

- Overlay de toque configurável.
- Mapeamento de botões para a semântica DualShock 2.
- Controles Bluetooth e USB pelas APIs de entrada Android.
- Tratamento de pausa, perda de foco, reconexão e retorno.
- Feedback tátil opcional, sem alterar a lógica original.

### 9. Frame pacing e desempenho

Separar relógio da lógica, submissão GPU e apresentação. Usar inicialmente AChoreographer/Choreographer e os mecanismos de sincronização Vulkan do sistema; soluções externas de frame pacing só serão avaliadas se medições demonstrarem necessidade. Prioridade: estabilidade e velocidade correta; metas de FPS só serão publicadas após medições reproduzíveis.

## Política de implementação própria

**Não será importado código de outros ports ou emuladores.** Black para Android, Dragon Rage/Budokai Tenkaichi 3, CodeAbhi, KIexster, Waffle565, PS2Recomp e PCSX2 serão consultados somente como referências de engenharia. Não serão incorporados seus launchers, recompiladores, runtimes, renderizadores, intérpretes, shaders, hooks ou sistemas de controle.

O FORGE continuará sendo a interface e o inicializador. O código responsável por executar God of War será construído neste repositório, em módulos separados e com testes próprios. O fluxo inicial será:

1. **Imagem:** validar o ISO, ler `SYSTEM.CNF`, identificar a região/revisão e localizar o ELF de boot.
2. **Recompilador próprio:** implementar decoder/analisador MIPS R5900, resolução de funções e geração de C++ para ARM64. O compilador do jogo será uma ferramenta de build; não fará interpretação/JIT durante a partida.
3. **Runtime próprio:** implementar memória, registradores, exceções, threads e serviços do EE utilizados pelo jogo; construir as funções necessárias de IOP, CD/DVD, SIF, DMA, VIF e GIF em código nativo. As microprogramas VU1 serão traduzidos/gerados antecipadamente, sem fallback de interpretador no produto final.
4. **Gráficos próprios:** implementar o frontend do Graphics Synthesizer e seu backend Vulkan Android, incluindo VRAM, transferências, texturas, CLUT, depth, alpha test, blending e sincronização.
5. **Áudio e entrada próprios:** ligar SPU2/LIBSD à saída de áudio Android de baixa latência e mapear toque/gamepad para os comandos do DualShock 2.
6. **Validação:** testar cada marco com checkpoints do boot, menus e cenas do próprio jogo; comparar imagens, estado e temporização em aparelho real.

As referências ajudam a formular critérios e identificar problemas típicos de ARM64, GPUs tile-based, memória, frame pacing, lifecycle, controles e instalação de dados. **Não são bases de código nem dependências do projeto.**

### Dependências e APIs

A prioridade será usar Android SDK/NDK, CMake, APIs nativas do Android, Vulkan e APIs de áudio/entrada fornecidas pelo sistema. Bibliotecas externas genéricas só serão consideradas quando houver necessidade técnica demonstrada, licença compatível e justificativa documentada. Isso é distinto de importar o código de um port de jogo; qualquer dependência nova será avaliada individualmente.

O repositório não receberá ISO, ELF, IRX, arquivos extraídos, dados proprietários nem C++ gerado a partir do executável original. Insumos do usuário e artefatos derivados necessários à geração ficarão fora da CI pública e do Git.

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
