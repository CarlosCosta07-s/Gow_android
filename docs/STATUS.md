# Estado do projeto

**Data da auditoria:** 2026-11-05  
**Branch de desenvolvimento:** `main`  
**Estado global:** FORGE abre em aparelho Android real (confirmado pelo mantenedor). O shell, a retirada do teste Vulkan isolado e o parser inicial SYSTEM.CNF foram compilados com sucesso pela CI. O parser ainda aguarda validação com imagens reais no aparelho. Nenhum runtime ou gameplay de God of War está integrado.

## Gow_android

**[VERIFICADO — build CI anterior]**

- Repositório público `CarlosCosta07-s/Gow_android`, branch padrão `main`.
- Projeto Gradle Android com AndroidX AppCompat 1.8.0, Core 1.19.1 e GameActivity 4.4.2.
- Configurado para compileSdk 37, targetSdk 36, minSdk 28 e ABI `arm64-v8a`.
- Launcher FORGE com ícone adaptativo próprio, navegação, cartões de status, seletor Storage Access Framework e validação inicial do descritor primário ISO9660.
- GitHub Actions `42125956300`, commit `993c1a9ec010a00db688da2affddad7dab09f07c`, concluiu com `success` para o launcher SAF/ISO9660.
- GitHub Actions `42128232489`, commit `e2094dc922cff86313a1e0f46a92bdffae74fdea`, concluiu com `success`: compilou o launcher temático, o ícone adaptativo e o APK Debug ARM64.
- O APK daquela build continha `lib/arm64-v8a/libandroid-game.so` e `lib/arm64-v8a/libc++_shared.so`.

**[VERIFICADO — relato do mantenedor]**

- O FORGE foi aberto em pelo menos um aparelho Android real.
- Ao tocar no antigo botão de teste Vulkan isolado, o aplicativo fechava. O botão foi removido da interface. A validação gráfica será feita junto do renderer GS e do jogo, não por aquele teste separado.
- O submódulo que apontava para God of War: Recomp foi removido; `.gitmodules` também foi removido. Não há runtime de terceiros vinculado ao projeto.

**[CÓDIGO ADICIONADO — AINDA NÃO VALIDADO]**

- O launcher agora abre o arquivo escolhido pelo SAF com `ParcelFileDescriptor` e lê por offset usando `pread`.
- O parser valida o descritor primário ISO9660, lê o diretório raiz, procura `SYSTEM.CNF`, interpreta a entrada `BOOT2` e reconhece inicialmente `SCUS_973.99` (NTSC-U) e `SCES_531.33` (PAL).
- A implementação foi enviada à branch `main` e compilou com sucesso no run `42142814087` (commit `18839524eb1061c5331b5762213dadaa51d55dd3`). Ainda não foi validada com imagem real no aparelho.
- A ação JOGAR ainda não inicia uma sessão. O runtime de boot não foi integrado.

## [NÃO TESTADO — aparelho físico e jogo]

- Validação da leitura de SYSTEM.CNF com imagens válidas, inválidas, PAL/NTSC-U e provedores SAF diferentes no aparelho Android.
- Execução do ELF recompilado e serviços nativos de EE/IOP/VU.
- Backend GS/Vulkan apresentando comandos reais e primeira imagem do jogo.
- Menus, gameplay, áudio, FMVs, controles, saves e desempenho térmico.

## Referências de engenharia — sem código importado

### Black para Android — RanggaCasper

- O README descreve recompilação estática x86 → C++/ARM64, Vulkan, SDL3, controles touch, giroscópio e gamepad.
- Usaremos a separação de camadas e os critérios de compatibilidade móvel como referências.
- Nenhum código, runtime, renderer, recompiler, shader ou controle será copiado.

### Dragon Rage / Budokai Tenkaichi 3 — dfdx047

- O README descreve launcher Android separado de engine ARM64, instalação de ISO e testes em GPUs Snapdragon e Mali.
- Usaremos organização de dados, validação da imagem, diagnóstico, perfis de GPU e estratégia de teste como referências.
- Nenhum código da decompilação, port de PC, engine, renderer, app ou sistema de controle será copiado.

### God of War: Recomp, KIexster, Waffle565, PS2Recomp e PCSX2

- Foram consultados para entender documentação, boot e subsistemas PS2.
- Permanecem fontes de leitura e comparação; não são submódulos, dependências ou runtime deste projeto.

## Próximos bloqueios reais

1. Validar o parser SYSTEM.CNF com imagens de teste do usuário no aparelho, sem enviar dados do disco à CI.
3. Implementar nosso próprio parser ELF, decoder MIPS R5900, analisador de funções e gerador C++/ARM64.
4. Implementar runtime nativo próprio para memória/EE e serviços PS2 necessários ao jogo.
5. Implementar frontend GS e backend Vulkan próprios e apresentar a primeira imagem real.
6. Compilar antecipadamente os microprogramas VU1; não manter interpretador no caminho final.
7. Completar entrada, áudio, vídeos, saves e otimização.

**Nenhum gameplay foi testado ou declarado funcional.** As referências de outros ports não foram incorporadas ao código.
