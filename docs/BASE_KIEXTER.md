# Base de código — KIexster / God of War PC

**Objetivo:** acelerar o port Android reutilizando componentes já desenvolvidos e testados no port nativo de PC, sem importar o repositório inteiro e sem carregar para Android as camadas específicas de Windows/OpenGL.

## Fonte principal

- Repositório: https://github.com/KIexster/god-of-war-recomp
- Commit auditado: `a226151feb6ed22937e42be485aaf28a83acd166`
- Licença declarada: GPL-3.0.
- Jogo-alvo: God of War PS2 NTSC-U, ELF `SCUS_973.99`.

O README do KIexster declara mapa de 6.414 funções, recompilação estática MIPS R5900 → C++, boot, menus e combate no barco no PC. O port não é uma engine independente feita do zero: desde outubro de 2026, sua base de runtime é o projeto de Waffle565, aplicado sobre PS2Recomp por meio de uma pilha de patches.

## Componentes já trazidos para este repositório

| Destino em Gow_android | Origem no KIexster | SHA do arquivo de origem | Uso |
|---|---|---|---|
| `recompiler/config/funcmap.csv` | `config/funcmap.csv` | `b1253abb001fbf1ac0622b751aa1ca95b35f634c` | Mapa de 6.414 funções do ELF. |
| `recompiler/config/recomp.template.toml` | `config/recomp.template.toml` | `8296991acfd428ab4c2702917725682ba67d26c7` | Stubs, entry points, skips e patches de instruções para SCUS_973.99. |
| `port/kiexter/gow_overrides.cpp` | `src/gow_overrides.cpp` | `6f73d16bc59d34f780317bf09c69c3a1f863fc0e` | Overrides e integração específicos de God of War. |
| `port/kiexter/tools/vu1n_gen.py` | `tools/vu1/vu1n_gen.py` | `4ddb2c1d81b1717240d9259af8457cc860d95eea` | Geração antecipada de blocos C++ para microprogramas VU1. |
| `port/kiexter/tools/iopn_gen.py` | `tools/iop/iopn_gen.py` | `f1e540f6bac22435855ff0b22661167fe3f953da` | Geração antecipada de C++ para os módulos IRX do jogo. |
| `port/kiexter/tools/vu1n_heads.txt` | `tools/vu1/vu1n_heads.txt` | `a50466e46bb27e232be8f058e305ed118c4f88ce` | Entradas adicionais de microprogramas/blocks VU1 para o gerador AOT. |
| `port/kiexter/gow_pad2_packet.h` | `src/gow_pad2_packet.h` | `ca1c082b6793e54baf789e3d45122ac8d748eec8` | Conversão do estado de pad para o pacote de 18 bytes esperado por libpad2. |

Os dois arquivos de configuração foram validados estruturalmente: 6.414 intervalos, sem sobreposições, tamanhos coerentes e marcadores `@ELF@`, `@MAP@`, `@OUT@` presentes. Essa checagem não recompila o ELF e não valida o runtime.

Os quatro arquivos de código foram trazidos para uma área de staging. **Ainda não estão no CMake/Gradle e não fazem parte do APK.** O arquivo de overrides depende de tipos e headers do runtime Waffle/KIexster; os geradores VU1/IOP dependem de dumps privados produzidos pelo runtime. O helper de pad é pequeno, mas só se integra quando a camada host de entrada estiver conectada. Não devem ser compilados isoladamente.

## Proveniência adicional: Waffle565 e PS2Recomp

O próprio cabeçalho de `gow_overrides.cpp`, `vu1n_gen.py` e `iopn_gen.py` identifica código derivado de Waffle565:

- Repositório: https://github.com/Waffle565/God-Of-War-Native-Port
- Commit referenciado nos cabeçalhos dos overrides/geradores: `34f30d5e432751369177af115104f4d19af6d080`.
- Branch de runtime consultada: `runtime`; licença GPL-3.0 presente nessa branch.
- O README do KIexster descreve a base Waffle como o diff `c5a9d02..runtime`, associado ao commit `49ca62c`.

O recompilador usado pelo pipeline do KIexster é:

- Repositório: https://github.com/ran-j/PS2Recomp
- Commit fixado pelo script do KIexster: `c5a9d02573410a2085a4b4b831b0b68ba3515440`.
- Licença declarada: GPL-3.0.

O runtime Waffle/KIexster também declara uso de componentes de PCSX2, incluindo código de referência EE/VU sob GPL-3.0-or-later. Os avisos de origem devem ser preservados ao reutilizar esses arquivos.

## O que podemos reutilizar

**Candidatos a reaproveitamento, depois de integrar e validar as dependências:**

1. Mapa de funções e configuração de recompilação específicos do ELF NTSC-U.
2. PS2Recomp como ferramenta de build para gerar C++ a partir do ELF do usuário.
3. Overrides de God of War, após separar chamadas de host/desktop e diagnósticos que não são necessários no Android.
4. Runtime genérico do EE, estado de memória, registradores, scheduler, chamadas de sistema e serviços do jogo que sejam independentes do host.
5. Frontend de comandos GIF/GS, regras de VRAM/formatos, swizzle e partes de textura que não dependam diretamente de OpenGL.
6. Geradores AOT de VU1 e IRX/IOP, executados como ferramentas de build fora do APK.
7. Serviços do jogo para pad, cartão de memória, CD/DVD, áudio e streaming, após substituir a saída/entrada específica do PC.

## O que não será transplantado diretamente

- Runner desktop, janela raylib, painel Dear ImGui e integração de teclado/desktop.
- Backend GS OpenGL/WGL/EGL. O destino gráfico do FORGE é um backend Android Vulkan; a camada de comandos GS pode ser reaproveitada apenas após separação da API gráfica.
- Saída de áudio baseada na configuração desktop/miniaudio do host. A saída Android será adaptada para API nativa de áudio.
- Entrada XInput/Win32 e lógica de janela Windows.
- CMake/PowerShell do build PC como se fossem um build Android pronto.
- Fallback interpretado do EE ou VU1 no caminho final. Os blocos/microprogramas usados pelo jogo precisam estar recompilados/gerados antes da execução; os IRX necessários também devem ter forma nativa.

## Lacunas específicas para Android

1. O runtime Waffle usa raylib como camada de host e o backend gráfico disponível é OpenGL; não é um renderer Vulkan pronto.
2. O arquivo Android consultado em Waffle (`ps2_android_runtime.cpp`) contém somente um TODO para controles touch. O suporte Android dessa base não equivale a um port completo.
3. A configuração CMake consultada exclui a geração VU1 AOT quando `PS2X_IS_ANDROID` está ativo. Vamos separar o gerador host da compilação Android: gerar C++ com ferramenta host e compilar os resultados para `arm64-v8a`.
4. O port do KIexter carrega arquivos extraídos do disco mantendo seus setores originais; o launcher atual seleciona ISO, mas ainda não fornece ao runtime esse layout de arquivos. Precisamos de um extrator/cache privado ou leitor VFS compatível.
5. O `GowGameActivity` e `main.cpp` atuais são apenas o shell nativo/probe anterior. Precisam ser conectados ao runner do jogo, mantendo o FORGE como tela inicial.

## Estado e regra de integração

- [x] Importar mapa de funções e template de recompilação.
- [x] Trazer overrides e geradores VU1/IOP para staging, com proveniência registrada.
- [ ] Integrar PS2Recomp ao pipeline privado de geração de C++.
- [ ] Selecionar e importar o núcleo portátil do runtime, mantendo licença e atribuições.
- [ ] Adaptar o host/lifecycle para Android e preservar o FORGE.
- [ ] Substituir o renderer OpenGL por backend GS/Vulkan Android.
- [ ] Gerar VU1 e os módulos IRX necessários antecipadamente, sem fallback interpretado no produto final.
- [ ] Validar boot, primeira imagem e gameplay real no aparelho.

**Dados do jogo:** ISO, ELF, IRX originais, arquivos PAK, dumps de microcódigo e C++ gerado a partir do ELF continuam privados e fora do Git/CI pública.


## Pipeline host preparado

Foi criado o script tools/build_recompiler_host.py para reproduzir a etapa de geração de C++ em ambiente privado. Ele fixa:

- KIexster a226151feb6ed22937e42be485aaf28a83acd166;
- PS2Recomp c5a9d02573410a2085a4b4b831b0b68ba3515440;
- patch-base ps2recomp-waffle-runtime.patch proveniente do commit KIexster fixado.

O script configura apenas o alvo host ps2_recomp, desativando runtime, analyzer, testes e Studio nessa etapa. ELF, configuração preenchida e C++ gerado são copiados/gravados fora do repositório. A execução ainda não foi realizada: o script está preparado, não validado por build.

Detalhes, dependências e modo de uso: [PIPELINE_RECOMPILADOR.md](PIPELINE_RECOMPILADOR.md).

## Critérios de seleção do runtime Waffle/KIexster

A decisão arquitetural é **reaproveitar o núcleo de serviços PS2 e substituir a camada de host**, não importar o executável desktop inteiro.

| Camada lógica | Reaproveitamento candidato | Adaptação obrigatória |
|---|---|---|
| EE / memória / scheduler | Estado, memória, threads, temporizadores, syscalls, IRQ e ligação às funções recompiladas | Sincronização com lifecycle Android, thread de jogo, relógios e diagnóstico |
| VU0 / VU1 | Operações macro e interface de despacho VU1; geradores AOT como ferramentas host | Cobertura integral dos microprogramas usados; sem fallback interpretado no produto final; instruções ARM64/NEON e estado FP |
| IOP / IRX | Kernel/serviços IOP, RPC e interfaces dos módulos | Gerar antecipadamente os IRX necessários; eliminar fallback interpretado no caminho final |
| DMA / VIF / GIF | Fluxo de pacotes, transferências e frontend GS/GIF | Buffers, ownership, sincronização e backpressure compatíveis com o runtime móvel |
| GS | Regras de comandos, formatos, VRAM/CLUT, swizzle e frontend independente de API | Backend Vulkan Android; não usar gs_gl, WGL ou o renderer Raylib do PC |
| Disco / VFS | Semântica de arquivos e layout esperado pelo runtime | Conectar SAF a cache/VFS privado; validar caminhos, offsets e setores |
| SPU2 / LIBSD | Engine e semântica de áudio do jogo, se separáveis do dispositivo host | Substituir saída de desktop por API Android e sincronizar buffers sem bloquear o loop |
| Pad / saves | Mapeamento de comandos e lógica de cartão de memória | Android GameController, toque, armazenamento privado e importação/exportação Android |
| Runner / janela | Nenhuma dependência de janela desktop deve permanecer no produto | Preservar FORGE e integrar lifecycle/surface em GameActivity |

Esta é uma seleção por responsabilidade, não uma lista de arquivos já importados. A seleção arquivo a arquivo exige conferir includes, símbolos, alocações, dependências CMake e sincronização de cada módulo. A base Waffle será tratada como uma versão de referência fixada pelo patch do KIexter, evitando misturar arquivos de commits diferentes da branch runtime.

A classificação inicial dos patches (correção, otimização, host desktop ou dependência x86) está em [PIPELINE_RECOMPILADOR.md](PIPELINE_RECOMPILADOR.md). Os patches opcionais não foram aplicados automaticamente: cada um precisa de revisão e teste ARM64/Vulkan próprio.


## Auditoria de fontes do runtime Waffle (snapshot de referência)

Foi feita leitura do CMake de ps2xRuntime na branch runtime do Waffle, snapshot ba16c8d70eb5ae73267732e3c0e36cb3b6e90ebc. Esse snapshot serve para identificar dependências e bloqueios; ele **não deve ser misturado automaticamente** com a versão Waffle incorporada no patch-base KIexster. A integração deve manter uma única revisão coerente.

A lista do alvo ps2_runtime mostra candidatos para reaproveitamento, ainda não importados:

- Núcleo/serviços: ps2_memory.cpp, ps2_runtime.cpp, ps2_iop_host.cpp, ps2_rom_device.cpp, ps2_vfs.cpp e os arquivos do diretório Kernel.
- GS/fluxo gráfico: ps2_gif_arbiter.cpp, ps2_gs_memory.cpp, gs_frontend.cpp, gs_stage.cpp, gs_display.cpp, gs_host_present.cpp, gs_swizzle.cpp e gs_texture_store.cpp.
- Áudio e controles: ps2_audio.cpp, ps2_audio_vag.cpp e ps2_pad.cpp, sujeitos à separação do dispositivo físico/host.
- VIF1: ps2_vif1_interpreter.cpp é um decodificador de comandos e operações UNPACK do subsistema VIF1. Isso não é interpretação de instruções MIPS do EE; é um serviço de hardware do runtime. A permanência dele precisa ser avaliada por função e não confundida com o fallback de CPU.

### Fontes que não podem entrar no caminho final do jogo

O CMake desse snapshot adiciona explicitamente ao alvo do runtime arquivos de referência PCSX2 para o teste diferencial do EE:

- ee_ref/ee_diff.cpp, ee_watch.cpp, ee_ref/eeref.cpp;
- ee_ref/R5900OpcodeImpl.cpp, R5900OpcodeTables.cpp, MMI.cpp e FPU.cpp;
- vu_pcsx2/pcsx2_vu0_cop2.cpp, pcsx2_vu1_bridge.cpp, VUops.cpp, VUflags.cpp e VU1microInterp.cpp.

O próprio CMake descreve esse grupo como diferencial/teste EE. Essas fontes não são necessárias para o jogo recompilado e devem ser excluídas da build de release Android. Caso sejam mantidas em um build de diagnóstico separado, preservar os avisos PCSX2 GPL-3.0-or-later.

Há outro bloqueio: ps2_vu1_core.cpp implementa VU1Interpreter, enquanto o CMake habilita a geração/ligação de blocos VU1 nativos apenas sob condições específicas e exclui esse gerador quando PS2X_IS_ANDROID está ativo. O runtime pode, portanto, executar fallback interpretado para microprogramas/blocos sem cobertura AOT. **O produto final deve falhar com diagnóstico claro se faltar código nativo**, em vez de cair nesse fallback. A geração deve ser executada no host e seus arquivos compilados pelo NDK para ARM64.

### Host e APIs de plataforma que precisam ser trocados

- gs_gl.cpp é o backend OpenGL do desktop; não é o backend Vulkan do FORGE.
- ps2_audio_device.cpp usa miniaudio integrado ao Raylib/host; reaproveitar o motor SPU2/LIBSD quando separável, mas substituir a saída pelo adaptador Android.
- ps2_pad_rumble_win.cpp é específico de controles Windows e deve ser substituído pelo GameController Android.
- ps2_android_runtime.cpp no snapshot consultado contém apenas um TODO de controles touch; não constitui uma integração Android pronta.
- O runner Waffle usa sua própria integração NativeActivity/Raylib. O aplicativo atual usa AndroidX GameActivity; preservar a Activity e a interface FORGE existentes, adaptando a ponte de runtime em vez de substituir o launcher por esse runner.

**Consequência:** não vamos importar ou compilar o alvo ps2_runtime integralmente como está. A próxima integração precisa de uma lista CMake própria para Android que inclua somente os serviços portáveis aprovados, exclua o grupo de intérpretes de diagnóstico e impeça fallback interpretado de EE/VU1/IOP no caminho do jogo.
