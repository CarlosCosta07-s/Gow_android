# Otimização para celulares modestos

**Estado:** diretrizes de arquitetura e testes; nenhuma meta de FPS foi declarada como atingida.

## Objetivo

Priorizar compatibilidade, estabilidade térmica e frame time consistente em aparelhos Android ARM64 com CPU, GPU, RAM e armazenamento limitados. A otimização deve ser demonstrada por medições reproduzíveis, não por estimativas.

A instalação no estilo Dragon Rage BT3 reduz a dependência do SAF/ISO durante a partida e permite ao runtime ler arquivos locais. Ela não resolve, por si só, o custo de CPU do runtime PS2 nem o custo de renderização do GS.

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
