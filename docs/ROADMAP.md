# Roadmap — Gow_android

Uma fase só será considerada concluída quando houver código, build e evidência que satisfaçam os critérios de aceitação. O projeto será desenvolvido na branch `main`, com alterações pequenas e testáveis.

## Fase 0 — Fundação e auditoria

- [x] Criar repositório público.
- [x] Confirmar estado inicial e branch `main`.
- [x] Auditar referências Android e desktop para God of War PS2.
- [x] Registrar arquitetura, créditos e política de dados.
- [x] Registrar Black para Android e Dragon Rage/BT3 como referências de engenharia, sem importar código.

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
- [x] Implementar no launcher a leitura de SYSTEM.CNF e a identificação inicial de SCUS_973.99/SCES_531.33.
- [ ] Compilar e validar essa leitura com imagens de teste do usuário e em aparelho Android.
- [ ] Diagnosticar permissão, formato e região.
- [ ] Exportar logs sem dados pessoais ou do jogo.

**Aceitação:** APK instala e abre em aparelho real, seleciona a imagem do usuário e informa claramente arquivos inválidos ou incompatíveis.

## Fase 2 — Recompilador estático próprio (MIPS R5900 → C++/ARM64)

- [ ] Documentar o formato ELF da revisão NTSC-U e o fluxo de boot.
- [ ] Implementar parser próprio de ELF, segmentos, símbolos, relocations e entry point.
- [ ] Implementar decoder MIPS R5900 e instruções MMI necessárias ao jogo.
- [ ] Construir análise de funções, fluxo de controle e tabela de endereços.
- [ ] Implementar lifter que gere C++ determinístico para ARM64.
- [ ] Implementar tratamento próprio de delay slots, exceções e chamadas indiretas.
- [ ] Compilar o código gerado com Android NDK para arm64-v8a.
- [ ] Criar testes unitários com instruções, blocos e funções pequenas.
- [ ] Manter ELF, ISO e código gerado fora do repositório público e da CI pública.

**Aceitação:** pipeline próprio reproduzível em ambiente privado, com relatório de funções/instruções traduzidas e testes de equivalência de estado. Não pode haver interpretador/JIT do EE no caminho de execução.

## Fase 3 — Renderizador GS/Vulkan próprio

- [ ] Definir a API interna entre o frontend GIF/GS e o backend Vulkan.
- [ ] Implementar VRAM, formatos PSM, CLUT/paletas e swizzle.
- [ ] Implementar transferências locais e host↔GS, registradores e estados do GS.
- [ ] Implementar render targets, depth, alpha test, destination-alpha e modos de blending.
- [ ] Criar shaders próprios para as operações do GS usadas por God of War.
- [ ] Tratar feedback loops e efeitos com passes intermediários/ping-pong quando necessário.
- [ ] Implementar sincronização de filas, buffers e ciclo de vida da surface Android.
- [ ] Validar comandos reais do jogo, começando pelo logo/menu e primeira cena.

**Aceitação:** primeira imagem real do jogo apresentada em Android e comparada com referência visual. Um clear/present isolado não conta como conclusão.

## Fase 4 — Boot e primeira imagem do jogo

- [ ] Inicializar contexto e memória EE.
- [ ] Executar código recompilado e serviços mínimos de kernel.
- [ ] Resolver CD/DVD, IRX, SIF e cadeia inicial DMA.
- [ ] Registrar progresso por etapas.
- [ ] Apresentar a primeira imagem real do jogo.

**Aceitação:** app apresenta imagem real do jogo, sem tela vazia indefinida. Timeout mostra a última etapa confirmada.

## Fase 5 — Unidades vetoriais e gameplay

- [ ] Implementar VU0 macro necessário ao fluxo do jogo.
- [ ] Analisar microprogramas VU1 usados pelo jogo e gerar código nativo antecipadamente.
- [ ] Compilar microprogramas VU1 para ARM64; nenhum interpretador no build final.
- [ ] Implementar contratos próprios de VIF/GIF/DMA e sincronização com o GS.
- [ ] Implementar controle físico e overlay de toque.
- [ ] Validar movimento, câmera, combate, HUD, partículas e efeitos.

**Aceitação:** menu, novo jogo e primeiro combate reproduzidos em Android ARM64, com execução nativa do código recompilado e sem diferenças críticas de lógica.

## Fase 6 — Áudio, vídeos e saves

- [ ] Implementar saída de áudio usando a API nativa Android AAudio; considerar biblioteca externa somente se houver limitação comprovada.
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
- O projeto não importará código de outros ports ou emuladores.
- O caminho final não usará emulador, interpretador de EE nem interpretador de microprogramas VU1.
- A execução do jogo será recompilação estática própria; os serviços PS2 serão implementados em C++ nativo específico para o título.
- Não declarar FPS sem medições reproduzíveis.
- Usar **[VERIFICADO]**, **[HIPÓTESE]** e **[NÃO TESTADO]** em auditorias.
