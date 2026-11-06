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

Os dois arquivos de configuração foram validados estruturalmente: 6.414 intervalos, sem sobreposições, tamanhos coerentes e marcadores `@ELF@`, `@MAP@`, `@OUT@` presentes. Essa checagem não recompila o ELF e não valida o runtime.

Os três arquivos de código foram trazidos para uma área de staging. **Ainda não estão no CMake/Gradle e não fazem parte do APK.** O arquivo de overrides depende de tipos e headers do runtime Waffle/KIexster; os geradores VU1/IOP dependem de dumps privados produzidos pelo runtime. Não devem ser compilados isoladamente.

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
