# Arquitetura técnica — Gow_android

**Estado:** proposta inicial. Os componentes descritos ainda não estão implementados neste repositório.

## Objetivo

Criar uma aplicação nativa para Android que execute *God of War* (PS2, 2005) por recompilação estática do código MIPS R5900 para C++/ARM64 e substitua os serviços de hardware do PS2 usados pelo jogo por implementações nativas Android.

O produto final não será um emulador genérico nem dependerá de interpretação/JIT do Emotion Engine como caminho de execução do jogo. A aplicação não incluirá a ISO ou os dados proprietários do jogo.

## Alvo inicial

- Plataforma exclusiva: Android.
- ABI inicial: `arm64-v8a`.
- Linguagem nativa: C++20.
- Build nativo: CMake + Android NDK.
- Aplicativo: Gradle + AndroidX GameActivity.
- Gráficos: Vulkan nativo, com surface e swapchain Android reais.
- Primeiro alvo de compatibilidade: NTSC-U, serial `SCUS-97399`, por ser a revisão mais documentada nas referências auditadas.
- PAL e outras revisões ficam para uma etapa posterior.

A versão mínima do Android e o requisito Vulkan definitivo serão confirmados com dispositivos reais. Android 9/API 28 e Vulkan 1.1 são apenas referências iniciais observadas em outro projeto, não uma garantia nossa.

## Camadas do projeto

### 1. Aplicativo Android

Responsabilidades:

- Tela inicial, seleção do disco e apresentação do estado de compatibilidade.
- Storage Access Framework para abrir o arquivo escolhido pelo usuário.
- Permissão persistente de leitura quando o provedor permitir.
- Identificação da região/revisão e diagnóstico de arquivos inválidos.
- Orientação horizontal, ciclo de vida, foco, pausa e recuperação da superfície.
- Configuração de controles, interface de toque e controles físicos.
- Gerenciamento de saves e exportação/importação por seletor Android.

**Decisão:** usar AndroidX GameActivity como base do novo shell C/C++. A referência CodeAbhi usa NativeActivity; sua lógica de seleção e validação é útil, mas não será copiada sem adaptação. A documentação Android recomenda GameActivity para jogos novos.

### 2. Disco e dados do usuário

Fluxo previsto:

1. Receber a URI do arquivo pelo seletor Android.
2. Abrir o descritor de leitura e preservar a permissão concedida.
3. Validar ISO9660, descritor primário, tamanho e estrutura esperada.
4. Identificar serial e revisão; recusar arquivos incompatíveis com diagnóstico explícito.
5. Ler diretamente do descritor ou preparar somente os elementos de boot estritamente necessários em diretório privado do app.
6. Nunca enviar dados do usuário para repositório, Issues, releases ou CI pública.

A ISO não será copiada para o APK nem armazenada no Git.

### 3. Recompilador estático

Ferramenta do pipeline de desenvolvimento, não parte do loop de jogo:

- Analisar o ELF de entrada do usuário.
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

### 5. Subsistemas PS2 utilizados pelo jogo

Implementar somente os contratos necessários ao título:

- IOP/R3000A e módulos IRX.
- SIF, DMA, VIF e GIF.
- CD/DVD e leitura dos arquivos do disco.
- Cartão de memória virtual e operações de save/load.
- SPU2/LIBSD e temporização de áudio.
- Entrada equivalente ao DualShock 2.

Cada subsistema terá logs e contadores próprios. Uma falha de leitura ou DMA não deve aparecer como travamento gráfico sem diagnóstico.

### 6. Renderizador Graphics Synthesizer (GS) via Vulkan

Este é um dos maiores riscos técnicos para Android.

A implementação não pode ser confundida com um probe Vulkan. Enumerar uma GPU ou criar uma instância não apresenta os frames do GS.

Etapas obrigatórias:

1. Criar instância Vulkan e habilitar extensões necessárias.
2. Criar surface usando a janela Android.
3. Selecionar dispositivo físico e filas de gráficos/apresentação.
4. Criar swapchain, imagens, views, sincronização e recuperação de resize/orientação.
5. Desenhar e apresentar um frame de teste, confirmando o caminho de apresentação.
6. Integrar o frontend GS ao backend Vulkan.
7. Implementar e validar texturas, CLUT/paletas, swizzle, framebuffer, depth, blend e alpha test usados pelo jogo.
8. Comparar capturas e checkpoints com referências conhecidas.

**Critério do renderer:** primeira imagem real do jogo apresentada na tela Android. Uma janela vazia, probe Vulkan ou triângulo de teste não conclui esta fase.

### 7. Áudio

O SPU2/LIBSD deverá alimentar um mixer nativo e uma saída Android de baixa latência. Oboe é a opção inicial para a saída, com buffers e sincronização sob responsabilidade do runtime.

O callback de áudio não deve bloquear, alocar memória imprevisivelmente nem fazer leitura de disco.

### 8. Entrada e interface

- Overlay de toque configurável.
- Mapeamento de botões para a semântica DualShock 2.
- Controles Bluetooth e USB pelas APIs de entrada Android.
- Tratamento de pausa, perda de foco, reconexão e retorno ao app.
- Feedback tátil opcional, sem alterar a lógica original do jogo.

### 9. Frame pacing e desempenho

Separar relógio da lógica do jogo, submissão GPU e apresentação. Avaliar Frame Pacing/Swappy do Android Game Development Kit. Prioridade inicial: estabilidade e velocidade correta do jogo; metas de FPS só serão publicadas após medições em dispositivos e cenas identificados.

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

Os diretórios serão criados quando houver implementação real, testes e documentação para eles. Não criaremos módulos vazios apenas para aparentar progresso.
