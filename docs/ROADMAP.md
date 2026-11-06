# Roadmap — Gow_android

Uma fase só será considerada concluída quando houver código, build e evidência que satisfaçam os critérios de aceitação. O projeto será desenvolvido na branch `main`, com alterações pequenas e testáveis.

## Fase 0 — Fundação e auditoria

- [x] Criar repositório público.
- [x] Confirmar estado inicial e branch `main`.
- [x] Auditar referências Android e desktop para God of War PS2.
- [x] Registrar arquitetura, créditos e política de dados.
- [x] Auditar KIexster, Waffle565 e PS2Recomp como base de código do port nativo de PC.
- [x] Importar seletivamente mapa/configuração, overrides de God of War e geradores AOT para staging, com origem registrada.

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
- [x] Implementar leitor nativo ISO9660 com busca de arquivos por caminho e leitura aleatória por descritor; teste de imagem sintética na CI. A ponte do leitor para o VFS IOP permanece pendente.
- [x] Compilar o parser SYSTEM.CNF pela CI (run `42142814087`).
- [ ] Validar essa leitura com imagens de teste do usuário em aparelho Android.
- [ ] Diagnosticar permissão, formato e região.
- [ ] Exportar logs sem dados pessoais ou do jogo.

**Aceitação:** APK instala e abre em aparelho real, seleciona a imagem do usuário e informa claramente arquivos inválidos ou incompatíveis.

## Fase 2 — Recompilação estática ARM64 com base KIexster

- [x] Importar `funcmap.csv` (6.414 funções) e `recomp.template.toml` para `recompiler/config/`.
- [x] Fixar PS2Recomp no commit `c5a9d02573410a2085a4b4b831b0b68ba3515440` e integrar/buildar a ferramenta host (run `42159359104`).
- [x] Preparar o pipeline privado que recebe o ELF NTSC-U sem armazená-lo no repositório; falta executá-lo com o ELF real do usuário.
- [ ] Gerar C++ usando a configuração específica de `SCUS_973.99`.
- [ ] Compilar código gerado para `arm64-v8a`; corrigir ABI, MMI/intrinsics e particularidades de ARM64.
- [ ] Comparar checkpoints/estados e produzir relatório de funções traduzidas, stubs e pendências.
- [ ] Manter ISO, ELF e C++ gerado fora do repositório público e da CI pública.

**Aceitação:** pipeline reproduzível que gera e compila código nativo ARM64 do ELF privado. O recompilador executa no ambiente de build, não no celular nem durante a partida; o runtime final não deve usar interpretação/JIT do EE.

## Fase 3 — GS no Android: reaproveitar frontend e substituir backend

- [x] Auditar o frontend GS/GIF e identificar as fronteiras OpenGL/Raylib; documentar o contrato do backend em `docs/GS_VULKAN_ANDROID.md`.
- [ ] Reaproveitar apenas a camada de comandos, formatos PSM, CLUT, swizzle e estado que não dependa de OpenGL/host desktop.
- [ ] Isolar completamente o backend OpenGL/WGL/Raylib do runner Android.
- [ ] Implementar backend GS/Vulkan Android: render targets, depth, alpha test, destination-alpha, blending e sincronização (contrato e dependências auditados; veja `docs/GS_VULKAN_ANDROID.md`).
- [ ] Adaptar shaders e operações para GPUs móveis; usar passes intermediários quando necessário.
- [ ] Validar comandos reais do jogo, começando pelo logo/menu e primeira cena.

**Aceitação:** primeira imagem real do jogo apresentada em Android e comparada com referência visual. Um clear/present isolado não conta como conclusão.

## Fase 4 — Integrar runtime e boot ao FORGE

- [ ] Selecionar e importar os módulos portáveis do runtime Waffle/KIexter (EE, memória, scheduler, stubs e serviços usados pelo título).
- [x] Adaptar o runner nativo para AndroidX GameActivity/NDK, ligar `ps2_runtime` ao alvo `android-game` e preservar o MainActivity FORGE (smoke apenas de construção/destruição; sem iniciar o runtime).
- [ ] Implementar passagem segura do arquivo/URI do usuário para o runtime.
- [ ] Criar cache privado ou VFS para os arquivos do disco no layout e setores que o runtime espera.
- [ ] Ligar código recompilado, overrides de God of War e serviços mínimos de boot.
- [ ] Resolver CD/DVD, IRX, SIF, DMA e cadeia inicial de execução.
- [ ] Registrar diagnóstico por etapa, PC/RA e último subsistema confirmado.
- [ ] Apresentar a primeira imagem real do jogo.

**Aceitação:** app apresenta a imagem do jogo sem tela vazia indefinida; timeout mostra a última etapa confirmada e o erro correspondente.

## Fase 5 — VU1/IOP AOT e gameplay

- [x] Importar os geradores KIexter/Waffle de VU1 e IOP para staging.
- [ ] Capturar/obter localmente os microprogramas VU1 e módulos IRX da cópia do usuário, sem publicá-los.
- [ ] Rodar `vu1n_gen.py` como ferramenta host e compilar os blocos C++ gerados para ARM64.
- [ ] Garantir cobertura dos microprogramas usados pelo jogo; o caminho final não pode depender do fallback interpretado.
- [ ] Rodar `iopn_gen.py` sobre os módulos IRX necessários e compilar as unidades geradas para ARM64.
- [ ] Garantir que os IRX necessários ao jogo não caiam em caminho interpretado no produto final.
- [ ] Adaptar comandos VIF/GIF/DMA e sincronização do GS.
- [x] Implementar a ponte de eventos Android para estado virtual DualShock 2 (porta 0/slot 0), deadzone/sensibilidade/inversão e API compartilhada para entrada física/touch. A CI compila a ponte; gamepads físicos ainda não foram testados no aparelho.
- [ ] Implementar overlay visual de toque e ligar a configuração dos controles às opções do próprio jogo; não criar tela independente no FORGE.
- [ ] Validar mapeamento, gatilhos analógicos, desconexão e reconexão com controles reais em aparelho Android.
- [ ] Validar movimento, câmera, combate, HUD, partículas e efeitos.

**Aceitação:** menu, novo jogo e primeiro combate reproduzidos em Android ARM64 por código recompilado/AOT, sem interpretadores no caminho normal do jogo.

## Fase 6 — Áudio, vídeos e saves

- [ ] Reaproveitar o motor de som SPU2/LIBSD do runtime quando for independente do host.
- [ ] Substituir a saída desktop por adaptador AAudio Android.
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
- Reutilizar seletivamente KIexter/Waffle/PS2Recomp quando isso reduzir trabalho e for necessário ao jogo; documentar cada arquivo, commit e licença.
- Não importar o runner desktop, o renderer OpenGL/WGL ou a camada de janela/áudio/entrada de PC diretamente para Android.
- As opções de remapeamento, gamepad e controle touch do port devem ser integradas às opções/menu de pausa do próprio jogo; o FORGE não terá uma interface separada de configuração de controles.
- O caminho final não usará emulador, interpretador de EE, interpretador de VU1 ou fallback interpretado dos IRX necessários.
- O jogo será recompilado estaticamente; os serviços PS2 serão nativos e adaptados para o host Android.
- Não declarar FPS sem medições reproduzíveis.
- Usar **[VERIFICADO]**, **[HIPÓTESE]** e **[NÃO TESTADO]** em auditorias.
