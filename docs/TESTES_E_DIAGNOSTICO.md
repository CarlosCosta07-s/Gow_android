# Testes e diagnóstico

Nenhuma build ou teste em aparelho foi executado por este repositório neste commit. Este documento define a evidência necessária para evitar confundir um teste de shell com um teste do jogo.

## Ficha por aparelho

Registrar:

- Fabricante e modelo.
- SoC e GPU.
- Versão Android/API.
- Versão Vulkan reportada pelo driver.
- ABI e RAM disponível.
- Espaço livre antes do teste.
- Versão/commit do APK.
- Região/serial do jogo, sem anexar a ISO.
- Temperatura inicial/final quando disponível.

## Portas de validação

| Porta | O que precisa ser comprovado | Evidência |
|---|---|---|
| A — Instalação | APK instala, abre e fecha sem crash. | Logcat e identificação da build. |
| B — Seleção | SAF abre o arquivo e mantém a permissão. | Resultado da permissão e estado do seletor. |
| C — Identificação | ISO9660, serial e revisão são reconhecidos. | Log de validação sem caminho privado. |
| D — Vulkan | Surface, swapchain, filas e primeira apresentação de teste funcionam. | Logs de criação, resize e present. |
| E — Runtime | EE progride por estágios conhecidos. | PC/RA/SP, thread e última etapa. |
| F — Primeiro frame | GS apresenta imagem real do jogo. | Captura de tela e log correlacionado. |
| G — Gameplay | Menu, novo jogo, combate e transições funcionam. | Lista de cenários e resultados. |
| H — Áudio/save | Som, FMV e save/load funcionam. | Testes e integridade do save. |

## Categorias de log

- `android`: lifecycle, janela, SAF e permissões.
- `disc`: validação ISO, serial e leitura.
- `recomp`: ELF, função, instrução e geração.
- `ee`: execução Emotion Engine.
- `vu`: VU0/VU1.
- `iop`: módulos e RPC.
- `dma`: cadeias e transferências.
- `gs`: comandos, estado e framebuffer.
- `vk`: surface, device, swapchain, sincronização e apresentação.
- `audio`: SPU2, mixer e saída Android.
- `input`: toque e controles físicos.
- `save`: cartão virtual.

## Desempenho

- Não usar apenas o menu para representar desempenho do jogo.
- Registrar cena, resolução interna, FPS médio, 1% low e frame time.
- Separar lógica, CPU, submissão GPU e apresentação.
- Medir temperatura e throttling em sessões prolongadas.
- Não anunciar 30/60 FPS sem evidência reproduzível.

## Relatórios

- **[VERIFICADO]**: reproduzido com versão, aparelho e evidência identificáveis.
- **[HIPÓTESE]**: explicação provável ainda não reproduzida.
- **[NÃO TESTADO]**: sem evidência suficiente.

Remover caminhos absolutos, nomes de usuário e dados pessoais antes de publicar logs.
