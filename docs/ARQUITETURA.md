# Arquitetura técnica — Gow_android

**Estado:** shell Android ARM64 com build CI confirmada, launcher SAF e validação inicial ISO9660 adicionados; ainda não executa o jogo. A CI da alteração mais recente do launcher deve ser confirmada.

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
- `android/app/src/main/java/org/gowandroid/app/MainActivity.java`: launcher AppCompat, seletor SAF e validação ISO9660 do setor 16.
- `android/app/src/main/java/org/gowandroid/app/MainActivity.java`: launcher AppCompat, seletor SAF e validação ISO9660 do setor 16.
- `android/app/src/main/java/org/gowandroid/app/GowGameActivity.java`: Activity baseada em AndroidX GameActivity.
- `android/app/src/main/cpp/main.cpp`: ciclo nativo e smoke test Vulkan.
- `.github/workflows/android-shell.yml`: build de APK Debug ARM64.

O smoke test atual cria instância Vulkan, surface Android, seleciona GPU e filas de gráficos/apresentação, cria device, swapchain, image views, render pass, framebuffers, command buffers e semáforos. Envia um pass que limpa a imagem e chama apresentação. Trata eventos de criação/encerramento da janela e recriação do swapchain. A build ARM64 foi confirmada na CI, incluindo a versão com launcher SAF.

**Limite importante:** esse clear/present é apenas um teste de integração Vulkan. A validação ISO9660 já está implementada no launcher, mas ainda não lê `SYSTEM.CNF` nem identifica serial/revisão. Ainda não há frontend GS, shaders do jogo, recursos gráficos de God of War, recompilação de ELF ou runtime de jogo. O APK foi compilado pela CI, mas ainda precisa ser executado em aparelho real.

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
3. Validar ISO9660, descritor primário no setor 16 (implementado; CI ainda pendente para a última alteração).
4. Ler `SYSTEM.CNF` para identificar serial e revisão; esta etapa ainda não está implementada.
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

O smoke test já prova o caminho Android surface/swapchain/clear/present quando a CI e os dispositivos o validarem. Isso não equivale a modelar o Graphics Synthesizer.

Próximas etapas:

1. Criar um backend GS com interface independente do Android.
2. Implementar recursos de renderização usados pelo jogo.
3. Validar texturas, CLUT/paletas, swizzle, framebuffer, depth, blend e alpha test.
4. Reproduzir passagens e efeitos específicos do jogo.
5. Comparar capturas e checkpoints com referências conhecidas.
6. Medir sincronização, filas e memória em GPUs móveis reais.

**Critério do renderer:** primeira imagem real do jogo apresentada na tela Android. Um triângulo, clear pass ou probe não conclui esta fase.

### 7. Áudio

O SPU2/LIBSD deverá alimentar um mixer nativo e saída Android de baixa latência. Oboe é a opção inicial para a saída; buffers e sincronização permanecem responsabilidade do runtime. O callback não deve bloquear, alocar memória imprevisivelmente nem ler disco.

### 8. Entrada e interface

- Overlay de toque configurável.
- Mapeamento de botões para a semântica DualShock 2.
- Controles Bluetooth e USB pelas APIs de entrada Android.
- Tratamento de pausa, perda de foco, reconexão e retorno.
- Feedback tátil opcional, sem alterar a lógica original.

### 9. Frame pacing e desempenho

Separar relógio da lógica, submissão GPU e apresentação. Avaliar Frame Pacing/Swappy do Android Game Development Kit. Prioridade: estabilidade e velocidade correta; metas de FPS só serão publicadas após medições em dispositivos e cenas identificados.

## Estrutura lógica pretendida

```text
android/       Aplicativo, GameActivity, permissões e interface
native/        Runtime C++ e serviços PS2
renderer/      Frontend GS e backend Vulkan Android
recompiler/    Ferramentas de análise e recompilação estática
port/          Configuração e overrides específicos de God of War
tools/         Validação, diagnóstico e automação
docs/          Arquitetura, estado, testes, roadmap e créditos
third_party/   Dependências externas com versão e licença fixadas
```

Os diretórios de runtime/recompilação serão criados quando houver implementação real, testes e documentação. Não criaremos módulos vazios.
