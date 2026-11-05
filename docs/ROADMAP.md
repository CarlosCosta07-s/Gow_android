# Roadmap — Gow_android

Uma fase só será considerada concluída quando houver código, build e evidência que satisfaçam os critérios de aceitação. O projeto será desenvolvido na branch `main`, com alterações pequenas e testáveis.

## Fase 0 — Fundação e auditoria

- [x] Criar repositório público.
- [x] Confirmar estado inicial e branch `main`.
- [x] Auditar referências Android e desktop para God of War PS2.
- [x] Registrar arquitetura, créditos e política de dados.
- [ ] Fixar commits exatos dos componentes que serão integrados.

## Fase 1 — Shell Android reproduzível

- [x] Configurar Gradle, Android NDK, CMake e ABI `arm64-v8a`.
- [x] Adicionar AndroidX GameActivity e Activity nativa.
- [ ] Compilar APK Debug e confirmar build CI.
- [ ] Instalar e executar em aparelho Android real.
- [ ] Adicionar seletor SAF e permissões persistentes.
- [ ] Validar ISO9660 e identificar serial/revisão.
- [ ] Diagnosticar permissão, formato e região.
- [ ] Exportar logs sem dados pessoais ou do jogo.

**Aceitação:** APK instala e abre em aparelho real, seleciona a imagem do usuário e informa claramente arquivos inválidos ou incompatíveis.

## Fase 2 — Recompilação ARM64

- [ ] Integrar e fixar PS2Recomp e configuração de God of War.
- [ ] Recompilar ELF privado para C++.
- [ ] Compilar objetos gerados para `arm64-v8a`.
- [ ] Testar instruções e funções críticas com checkpoints.
- [ ] Separar insumos e artefatos privados da CI pública.

**Aceitação:** pipeline reproduzível em ambiente privado, relatório de funções resolvidas/pendentes e nenhum ELF ou código gerado do jogo publicado no Git.

## Fase 3 — Apresentação Vulkan Android

- [x] Implementar criação de instância e surface Android.
- [x] Implementar seleção de GPU e filas de gráficos/apresentação.
- [x] Implementar swapchain, render pass, framebuffers e sincronização.
- [x] Implementar smoke test clear/present e recriação do swapchain.
- [ ] Confirmar build nativa e APK pela CI.
- [ ] Executar teste gráfico em aparelho real e registrar logs/captura.
- [ ] Tratar perfis de GPU e recuperação de falha.

**Aceitação:** apresentação estável durante pausa, retomada, resize e recriação de surface nos aparelhos de referência.

## Fase 4 — Boot e primeira imagem do jogo

- [ ] Inicializar contexto e memória EE.
- [ ] Executar código recompilado e serviços mínimos de kernel.
- [ ] Resolver CD/DVD, IRX, SIF e cadeia inicial DMA.
- [ ] Registrar progresso por etapas.
- [ ] Integrar frontend GS e backend Vulkan.
- [ ] Apresentar a primeira imagem real do jogo.

**Aceitação:** app apresenta imagem real do jogo, sem tela vazia indefinida. Timeout mostra a última etapa confirmada.

## Fase 5 — Renderização e gameplay

- [ ] Implementar recursos GS usados pelo menu e primeira área.
- [ ] Validar textura, paleta, blend, alpha test, depth e framebuffer.
- [ ] Implementar VU1 nativo e testar microprogramas necessários.
- [ ] Implementar controle físico e overlay de toque.
- [ ] Validar movimento, câmera, combate, HUD e efeitos.

**Aceitação:** menu, novo jogo e primeiro combate reproduzidos em Android ARM64, sem diferenças críticas de lógica.

## Fase 6 — Áudio, vídeos e saves

- [ ] Integrar saída Oboe.
- [ ] Ligar SPU2/LIBSD ao mixer.
- [ ] Validar efeitos, música e sincronização.
- [ ] Decodificar e apresentar FMVs.
- [ ] Implementar cartão de memória virtual.
- [ ] Testar salvar, carregar, exportar e importar.

**Aceitação:** áudio/vídeo sincronizados e saves persistentes, testados após fechar e reabrir o app.

## Fase 7 — Compatibilidade e otimização

- [ ] Testar áreas, transições, checkpoints, mortes e carregamentos.
- [ ] Medir FPS médio, 1% low, frame time, memória e temperatura.
- [ ] Avaliar resolução interna e limites térmicos.
- [ ] Testar GPUs e drivers ARM64 Vulkan diferentes.
- [ ] Corrigir regressões e preparar instalação/documentação.

**Aceitação:** progresso validado nos aparelhos publicados na matriz de compatibilidade; limitações descritas sem promessas não testadas.

## Regras de progresso

- Abrir a Activity não significa que o jogo está portado.
- Criar uma instância Vulkan não significa que o renderer GS funciona.
- Um shell APK não é uma build completa do jogo.
- Não declarar FPS sem medições reproduzíveis.
- Usar **[VERIFICADO]**, **[HIPÓTESE]** e **[NÃO TESTADO]** em auditorias.
