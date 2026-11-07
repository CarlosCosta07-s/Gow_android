# Otimização para celulares modestos

**Estado:** otimizações de base já estão sendo integradas antes do primeiro menu; tuning fino e perfis adaptativos continuam posteriores ao primeiro gameplay funcional. Este documento separa as mudanças indispensáveis à arquitetura de runtime/renderização das otimizações que exigem medições térmicas reais.

## Objetivo

Priorizar compatibilidade, estabilidade térmica e frame time consistente em aparelhos Android ARM64 com CPU, GPU, RAM e armazenamento limitados. A otimização deve ser demonstrada por medições reproduzíveis, não por estimativas.

A instalação no estilo Dragon Rage BT3 reduz a dependência do SAF/ISO durante a partida e permite ao runtime ler arquivos locais. Ela não resolve, por si só, o custo de CPU do runtime PS2 nem o custo de renderização do GS.

## Referência de otimização: Black for Android

Projeto de referência: [RanggaCasper/Black-Android](https://github.com/RanggaCasper/Black-Android).

O README do projeto descreve:
- recompilação estática antecipada do código x86 para C++/ARM64 com XboxXBE-Recomp;
- renderer Vulkan e SDL3 para janela, entrada e áudio;
- opções de resolução interna, V-Sync, limite de FPS, contador de FPS, filtragem anisotrópica e distância de desenho;
- controles touch configuráveis, giroscópio e suporte a controles físicos.

Isso serve como referência de arquitetura móvel e de opções de usuário, não como receita pronta para God of War. Black é a versão original de Xbox, com runtime e contratos gráficos diferentes do PS2; nosso renderer precisa implementar o Graphics Synthesizer (GS), não reutilizar o renderer Direct3D 8 desse projeto.

O README do Black lista Android 10+, ARM64, GPU com Vulkan 1.1 e 4 GB de memória. Portanto, ele é uma referência técnica, mas essa lista não demonstra compatibilidade com aparelhos abaixo desses requisitos nem comprova que Gow_android alcançará o mesmo desempenho.

### Quando esta etapa começa

Somente depois que Gow_android tiver um caminho funcional demonstrado: boot real, primeira imagem, menu, novo jogo e primeiro combate. Antes disso, faremos apenas o trabalho de desempenho indispensável à correção da arquitetura — recompilação estática/AOT e backend GS/Vulkan — sem iniciar tuning fino, perfis de qualidade, limites de cache ou metas de FPS.

A prioridade atual é fazer o jogo funcionar. Este documento não deve desviar esforço das fases de recompilação, runtime, GS, boot e gameplay.

## Otimizações de base já integradas

As seguintes mudanças foram colocadas no caminho principal antes do primeiro gameplay, pois reduzem trabalho redundante e fazem parte do contrato do presenter/runtime:

- A GameActivity espera por eventos e apresenta um frame Vulkan somente quando o VSync do PS2 avança.
- Buffers de frame são reutilizados; o runtime não cria um vetor grande novo para cada apresentação.
- Ao perder foco/superfície, o scheduler EE pausa cooperativamente e desloca os prazos de VBlank/timers na retomada.
- Os targets nativos EE/runtime, IOP e jogo AOT usam otimização O2 no APK Debug, sem fast-math.
- O caminho VIF1 SIMD é preparado com sse2neon/NEON em ARM64.
- O rasterizador GS parcial evita divisões por pixel no cálculo baricêntrico de triângulos, usa coordenadas incrementais em sprites e não repete clipping por pixel nos spans já limitados.
- Quando o formato da swapchain oferece BLIT_SRC/BLIT_DST, a escala do frame é feita por blit Vulkan a partir de uma imagem intermediária do tamanho nativo do GS; nos outros casos, o escalonador CPU permanece como fallback. O upload evita um vetor temporário e faz a conversão RGBA/BGRA diretamente na memória Vulkan mapeada. Quando o frame já tem o mesmo tamanho da tela, o fallback também evita o passo de redimensionamento.

- O presenter Vulkan mantém vetores scratch persistentes para lotes de vértices/desenhos GS. A thread de produção e a de apresentação alternam a propriedade dos buffers sob mutex; `clear()` preserva a capacidade e evita destruir/alocar os dois vetores a cada VBlank. Essa disciplina de recursos por quadro foi inspirada pelo [Black for Android](https://github.com/RanggaCasper/Black-Android), sem copiar seu renderer Direct3D/Xbox.
- O backend GS mantém também um `decodedTextureScratch` reutilizado em todos os lotes texturizados. `GSTextureStore::DecodeTextureRgba` usa `vector::resize()`, então a capacidade fica disponível para o próximo lote; a decodificação continua ocorrendo, mas evita criar/destruir o vetor temporário a cada primitiva.
- O módulo `android_performance_hint.cpp` adapta o padrão de [ADPF do Dragon Rage](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/port/src/gs/gs_adpf.c): em Android 13+ resolve `APerformanceHint_*` por `dlsym`, cria uma sessão para a thread de apresentação e informa o trabalho por frame. A duração relatada desconta a espera do `ALooper` e as esperas de `vkWaitForFences`/`vkAcquireNextImageKHR`; o prazo deriva do delta de VBlank do PS2 e é limitado a quatro períodos NTSC. Android anterior ou API indisponível vira no-op; `GOW_ANDROID_ADPF=0` desativa a dica em builds de diagnóstico. Não ativa modo sustentado nem fixa frequência de CPU.

**Validação atual:** os testes host verificam o cálculo do prazo ADPF e o caminho sem API no host; a CI Android compila o módulo e verifica os contratos de timing/buffers. Isso ainda não é uma medição de desempenho no telefone: não há FPS, consumo, temperatura ou estabilidade térmica medidos. O ADPF é uma dica ao escalonador, não uma garantia de menor aquecimento. A rasterização GS continua parcialmente em CPU e não substitui um renderer GS/Vulkan completo.

## 1. Instalação e acesso a dados

- Validar a ISO e a revisão antes de extrair.
- Criar um manifesto específico de God of War. Não reutilizar cegamente os caminhos de Dragon Rage.
- Estimar espaço livre e tamanho da instalação antes de iniciar.
- Extrair em blocos com buffer limitado, progresso e cancelamento seguro.
- Escrever primeiro em staging; validar tamanhos e hashes disponíveis; só então publicar a instalação.
- Preservar arquivos/pacotes grandes nos formatos originais. Não descompactar archives em milhares de arquivos pequenos sem necessidade demonstrada.
- Não carregar toda a ISO, todos os archives ou toda a árvore do jogo na RAM.
- Na partida, usar o VFS apontado para a instalação privada e fazer leituras sob demanda.
- Implementar cache LRU/buffer somente com orçamento máximo explícito e ajustar o tamanho com base em perfis de aparelhos reais.
- Considerar prefetch assíncrono apenas quando o padrão de leitura justificar; não bloquear a thread de apresentação esperando dados que possam ser antecipados.

## 2. CPU e runtime

- Priorizar geração e cobertura AOT das funções EE, microprogramas VU1 e módulos IRX realmente usados pelo jogo.
- O caminho normal não deve depender de interpretação/JIT ou fallback interpretado para os blocos necessários.
- Evitar alocações repetidas no frame loop; reutilizar buffers e estruturas quando a semântica permitir.
- Reduzir locks e cópias entre EE, VU, GIF e GS sem quebrar ordenação, DMA ou sincronização do PS2.
- Usar NEON onde houver ganho medido e equivalência numérica validada; não trocar precisão por velocidade sem testes diferenciais.
- Fazer profiling para localizar gargalos antes de otimizar rotinas isoladas.

## 3. GPU e apresentação

- Implementar o GS/Vulkan real; o teste de clear/present não representa desempenho de gameplay.
- Priorizar render targets e formatos compatíveis com GPUs móveis, evitando passagens e cópias intermediárias desnecessárias.
- Agrupar trabalho quando isso preservar a ordem e a semântica dos comandos GS.
- Evitar sincronizações CPU/GPU que forcem espera a cada frame.
- Oferecer escala de resolução interna e perfis gráficos graduais quando houver evidência de que o gargalo é GPU.
- Manter frame pacing estável. Não usar descarte arbitrário de frames ou alterações de temporização do jogo para mascarar gargalos.
- Testar Vulkan em diferentes GPUs/drivers ARM64; uma implementação correta em um aparelho não comprova desempenho uniforme em todos.

## 4. Memória, temperatura e energia

- Definir orçamentos para cache de disco, texturas, render targets, buffers e estruturas do runtime.
- Evitar manter cópias duplicadas de arquivos grandes em RAM.
- Monitorar picos de memória, pressão do sistema e encerramentos por falta de memória.
- Testar sessões prolongadas para identificar thermal throttling e queda sustentada de desempenho.
- Permitir perfis conservadores de resolução/qualidade quando temperatura ou capacidade gráfica exigirem; qualquer ajuste automático precisa ser observável e configurável.
- Não manter workers ocupados quando o jogo estiver pausado ou sem surface válida.

## 5. Perfis de qualidade

A interface poderá oferecer perfis como **Econômico**, **Equilibrado** e **Qualidade**. Eles devem controlar apenas parâmetros seguros, como escala de resolução, qualidade de efeitos e orçamento de cache. Não devem alterar a lógica, velocidade ou temporização do jogo.

Os valores padrão serão definidos depois de medir aparelhos reais. Não fixar agora metas universais de FPS, cache ou resolução.

## 6. Matriz de testes e critérios

Para cada aparelho de referência, registrar:

- modelo do SoC, GPU, versão Android e driver Vulkan;
- resolução da tela e configuração gráfica usada;
- FPS médio, 1% low e distribuição de frame time;
- tempo de CPU/GPU e gargalo predominante;
- memória máxima/PSS e tamanho efetivo dos caches;
- throughput e latência de leitura durante carregamento e gameplay;
- temperatura e evidência de throttling após sessão prolongada;
- tempo de instalação, espaço ocupado e tempo de carregamento.

Testar ao menos inicialização, menu, cena com carga gráfica, combate, transição e uma sessão prolongada. Comparar sempre a mesma cena/configuração e registrar os logs sem dados proprietários.

**Aceitação:** só declarar um perfil ou aparelho como otimizado depois de medições repetíveis, sem regressões de precisão, estabilidade, áudio ou sincronização. Não publicar promessas de FPS com base apenas na compilação do APK.

## 7. Regra de engenharia

Toda otimização deve ter: gargalo identificado, hipótese explícita, alteração pequena, medição antes/depois e teste de regressão. Priorizar a melhoria que reduz o gargalo real do aparelho mais limitado, não a que apenas reduz uma métrica isolada.
