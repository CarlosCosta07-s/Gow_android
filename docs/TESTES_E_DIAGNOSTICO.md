# Testes e diagnóstico

**Build CI:** o APK Debug ARM64 com ícone adaptativo e launcher temático compilou com sucesso no run `42128232489` (commit `e2094dc9`). A build anterior do seletor SAF/ISO9660 também passou no run `42125956300`. Nenhuma execução em aparelho físico foi registrada. Este documento define a evidência necessária para evitar confundir teste de shell com teste do jogo.

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

A compilação da interface e do ícone está verificada por CI; visualização e uso em tela física continuam pendentes.

| Porta | O que precisa ser comprovado | Evidência |
|---|---|---|
| A — Instalação | APK instala, abre e fecha sem crash. | Logcat e identificação da build. |
| B — Seleção | SAF abre o arquivo e mantém a permissão. | Resultado da permissão e estado do seletor. |
| C — ISO9660 | O descritor primário do setor 16 é validado (código presente; execução em aparelho pendente). | Estado da tela e log sem caminho privado. |
| D — Identificação | `SYSTEM.CNF`, serial e revisão são reconhecidos. | Log de validação sem caminho privado. |
| E — Vulkan | Surface, swapchain, filas e primeira apresentação de teste funcionam. | Logs de criação, resize e present. |
| E1 — Diagnóstico nativo | A Activity exibe preparação, estado do núcleo e erro sem depender apenas do Logcat. | Captura do painel e Logcat com `GowAndroid`; testar mensagem precoce antes da criação da view. |
| F — Runtime | EE progride por estágios conhecidos. | PC/RA/SP, thread e última etapa. |
| G — Primeiro frame | GS apresenta imagem real do jogo. | Captura de tela e log correlacionado. |
| H — Gameplay | Menu, novo jogo, combate e transições funcionam. | Lista de cenários e resultados. |
| I — Áudio/save | Som, FMV e save/load funcionam. | Testes e integridade do save. |

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

## Diagnóstico de inicialização da GameActivity

O callback JNI `onNativeRuntimeStatus(String, boolean)` entrega mensagens do `android_main()` à interface Java pela thread principal. O painel diferencia estado informativo e falha, e a Activity preserva a última mensagem recebida antes de o painel estar pronto.

**Estado da verificação em 06/11/2026:** a primeira tentativa de build (CI `42246181055`) falhou porque `GameActivity` não possui `activity->clazz`, campo próprio de outra API Android. O bridge foi refeito para registrar a Activity por referência JNI global, usar referências locais no callback e liberar apenas a referência da Activity correspondente. A CI `42246883029` passou pelo checker e pelos testes host/VFS; `assembleDebug` estava em andamento na última consulta. A apresentação visual e o fluxo de falha precisam ser reproduzidos em aparelho antes de marcar esta porta como [VERIFICADO].