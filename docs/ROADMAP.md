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
- [x] Compilar APK Debug ARM64 e confirmar build CI no commit `7ed5a254`.
- [x] Confirmar a CI do launcher SAF/ISO9660 no run `42125956300`.
- [x] Criar ícone adaptativo e launcher temático God of War e confirmar a CI no run `42128232489`.
- [x] Abrir o shell em um aparelho Android real (confirmado pelo mantenedor em 05/11/2026).
- [ ] Verificar legibilidade, proporções e ícone em outros aparelhos e dimensões.
- [x] Adicionar tela inicial e seletor SAF para a imagem do usuário.
- [x] Solicitar permissão persistente de leitura e validar o descritor primário ISO9660 do setor 16.
- [ ] Ler `SYSTEM.CNF` e identificar serial/revisão do jogo.
- [ ] Diagnosticar permissão, formato e região.
- [ ] Exportar logs sem dados pessoais ou do jogo.

**Aceitação:** APK instala e abre em aparelho real, seleciona a imagem do usuário e informa claramente arquivos inválidos ou incompatíveis.

## Fase 2 — Recompilação ARM64

- [ ] Fixar uma revisão imutável da base Android de recompilação e registrar sua origem/licença.
- [ ] Integrar PS2Recomp, runtime EE/IOP e hooks específicos de God of War.
- [ ] Recompilar o ELF privado do usuário para C++.
- [ ] Compilar objetos gerados para `arm64-v8a`.
- [ ] Gerar/compilar microprogramas VU1 antecipadamente; não manter interpretador no caminho final.
- [ ] Testar instruções e funções críticas com checkpoints.
- [ ] Separar insumos e artefatos privados da CI pública.

**Aceitação:** pipeline reproduzível em ambiente privado, relatório de funções resolvidas/pendentes e nenhum ELF ou código gerado do jogo publicado no Git.

## Fase 3 — Renderizador Graphics Synthesizer Android (Vulkan)

- [ ] Integrar o frontend GS da base nativa ao backend Vulkan Android.
- [ ] Implementar render targets, texturas, CLUT/paletas e swizzle usados por God of War.
- [ ] Implementar depth, alpha test, destination-alpha e modos de blending do GS.
- [ ] Implementar alternativas two-pass/ping-pong para efeitos sem suporte direto do driver móvel.
- [ ] Implementar shaders e sincronização de recursos sem depender do smoke test isolado.
- [ ] Validar o backend com comandos gráficos reais emitidos pelo jogo em aparelho Android.
- [ ] Medir uso de memória GPU, frame time e estabilidade em diferentes drivers.

**Aceitação:** o backend GS apresenta corretamente a primeira imagem real de God of War em Android. Um clear/present isolado não conta como conclusão.

## Fase 4 — Boot e primeira imagem do jogo

- [ ] Inicializar contexto e memória EE.
- [ ] Executar código recompilado e serviços mínimos de kernel.
- [ ] Resolver CD/DVD, IRX, SIF e cadeia inicial DMA.
- [ ] Registrar progresso por etapas.
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
- Um shell APK não é uma build completa do jogo.
- O teste Vulkan clear/present isolado foi retirado do launcher após encerrar o app no aparelho do mantenedor; não será um bloqueio do projeto.
- A validação de gráficos acontecerá com o GS real e o jogo em execução.
- O caminho final não usará emulador, interpretador de EE nem interpretador de microprogramas VU1.
- Não declarar FPS sem medições reproduzíveis.
- Usar **[VERIFICADO]**, **[HIPÓTESE]** e **[NÃO TESTADO]** em auditorias.
