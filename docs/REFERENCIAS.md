# Referências e proveniência de código — Gow_android

**Estratégia atual:** reutilizar seletivamente o port nativo de PC do KIexster e seus componentes-base quando forem necessários para God of War; adaptar a plataforma para Android ARM64. Não vamos importar repositórios inteiros nem transplantar a camada de PC indiscriminadamente.

## 1. Base principal — KIexster/god-of-war-recomp

Repositório: https://github.com/KIexster/god-of-war-recomp  
Commit auditado: `a226151feb6ed22937e42be485aaf28a83acd166`  
Licença declarada: GPL-3.0.  
Jogo-alvo: God of War PS2 NTSC-U, `SCUS_973.99`.

O README e a documentação do projeto descrevem recompilação estática de 6.414 funções MIPS R5900 para C++, boot, menus, novo jogo e combate no barco no PC. O projeto é baseado em PS2Recomp e, desde outubro de 2026, usa como base de runtime o port de Waffle565.

### Arquivos já importados

| Destino neste projeto | Arquivo de origem | SHA-1/SHA do blob de origem | Estado |
|---|---|---|---|
| `recompiler/config/funcmap.csv` | `config/funcmap.csv` | `b1253abb001fbf1ac0622b751aa1ca95b35f634c` | Importado; 6.414 intervalos e sem sobreposição na verificação estrutural. |
| `recompiler/config/recomp.template.toml` | `config/recomp.template.toml` | `8296991acfd428ab4c2702917725682ba67d26c7` | Importado; contém stubs, entry points, skips, patches e marcadores de caminho. |
| `port/kiexter/gow_overrides.cpp` | `src/gow_overrides.cpp` | `6f73d16bc59d34f780317bf09c69c3a1f863fc0e` | Importado para staging; ainda não compilado no Android. |
| `port/kiexter/tools/vu1n_gen.py` | `tools/vu1/vu1n_gen.py` | `4ddb2c1d81b1717240d9259af8457cc860d95eea` | Importado para staging; exige dumps privados de microcódigo e runtime compatível. |
| `port/kiexter/tools/iopn_gen.py` | `tools/iop/iopn_gen.py` | `f1e540f6bac22435855ff0b22661167fe3f953da` | Importado para staging; exige dumps privados dos módulos IRX e runtime compatível. |

Os arquivos foram trazidos do commit acima sem alteração de conteúdo. Os cabeçalhos dos três arquivos de código indicam que eles foram originalmente copiados/adaptados de Waffle565; por isso, a origem Waffle também é registrada abaixo. Nenhum deles está conectado ao CMake/Gradle neste momento.

A verificação estrutural do mapa encontrou 6.414 funções, intervalos ordenados, tamanhos consistentes e nenhuma sobreposição. O template contém exatamente os marcadores `@ELF@`, `@MAP@` e `@OUT@`. Isso não substitui a execução do recompilador nem a validação do código gerado.

## 2. Runtime-base — Waffle565/God-Of-War-Native-Port

Repositório: https://github.com/Waffle565/God-Of-War-Native-Port  
Branch de runtime auditada: `runtime`.  
Commit citado pelo pipeline KIexster para o código de overrides/geradores: `34f30d5e432751369177af115104f4d19af6d080`.  
A documentação KIexster descreve a migração para o diff `c5a9d02..runtime`, associado a `49ca62c`.  
A branch `runtime` consultada possui arquivo `LICENSE` GPL-3.0.

O runtime Waffle substitui partes utilizadas do hardware PS2 por serviços nativos: GS OpenGL, serviços EE, VU1 AOT, IOP/IRX, SPU2/LIBSD, pad, memory card e integração DMA/VIF/GIF. É a base sobre a qual o KIexster construiu seu port de PC.

**Importante para Android:** a base Waffle é orientada ao desktop. Usa Raylib, renderer GS OpenGL e integração específica de host. O arquivo `ps2_android_runtime.cpp` consultado contém apenas um TODO para overlay de toque. O CMake reconhece Android e cria uma shared library, mas isso não significa que exista um port Android completo.

O que vamos avaliar/reutilizar:
- Estado/memória/scheduler do EE e serviços PS2 que não dependem do host.
- Frontend de comandos GS/GIF, formatos, VRAM/CLUT e swizzle que possam ser separados do backend OpenGL.
- Serviços de áudio, pad, memory card e disco, depois de substituir APIs de host.
- VU1 e IOP AOT, com pipeline de geração executado fora do APK.

O que deve ser refeito/adaptado:
- Runner e lifecycle Android, mantendo o FORGE.
- Backend gráfico GS para Vulkan Android; não ligar diretamente o backend WGL/OpenGL do PC.
- Entrada por toque/gamepad Android.
- Saída de áudio Android e sincronização.
- Leitura/extração/cache do disco a partir do ISO escolhido pelo usuário.

O repositório Waffle na branch `main` não fornece licença na raiz; a branch `runtime` auditada possui licença GPL-3.0. Qualquer arquivo selecionado deve ser conferido quanto à origem e aos avisos antes de importar.

## 3. Recompilador — ran-j/PS2Recomp

Repositório: https://github.com/ran-j/PS2Recomp  
Commit fixado pelo script KIexter: `c5a9d02573410a2085a4b4b831b0b68ba3515440`.  
Licença declarada: GPL-3.0.

O projeto fornece analyzer, recompilador MIPS R5900 → C++ e runtime genérico de suporte. A proposta é usá-lo como ferramenta de build para o ELF privado do usuário, com a configuração e o mapa KIexter. O código C++ gerado deve ser compilado para `arm64-v8a`; o recompilador não executa dentro do APK nem durante a partida.

**Estado:** ainda não importado nem integrado ao pipeline Android. O próximo trabalho é fixar e obter essa revisão em um ambiente de build host e verificar a saída gerada para ARM64.

## 4. Outras referências de engenharia

### Black para Android — RanggaCasper
https://github.com/RanggaCasper/Black-Android

Referência de arquitetura para recompilação estática x86 → C++/ARM64, Vulkan, camada Android, controles touch e gamepad. O Xbox e o PS2 têm arquiteturas diferentes; o código de Black não é renderer GS e não será usado como base direta de runtime.

### Dragon Rage / Budokai Tenkaichi 3 para Android — dfdx047
https://github.com/dfdx047/Tenkaichi3DecompAndroid

Referência PS2/ARM64 para separar launcher e engine, instalar dados de ISO, organizar arquivos locais e testar Snapdragon/Mali. Não é o runtime do nosso jogo.

### God of War: Recomp — CodeAbhi826
https://github.com/CodeAbhi826/God-Of-War-Recomp

Referência documental para fluxo Android/ARM64, boot e diagnóstico. O submódulo que havia sido adicionado anteriormente foi removido; não há código desse projeto no repositório atual.

### PCSX2
https://github.com/PCSX2/pcsx2

Referência comportamental para EE, VU, GS, DMA e IOP. Componentes PCSX2 já usados por upstreams devem manter seus cabeçalhos e licença GPL-3.0-or-later; não vamos usar PCSX2 como runtime genérico do produto final.

## 5. Plano de integração seletiva

1. Usar mapa/configuração do KIexster para o alvo `SCUS_973.99`.
2. Fixar PS2Recomp e gerar C++ a partir do ELF privado em ferramenta de build host.
3. Selecionar do runtime Waffle/KIexster apenas módulos necessários ao jogo e que possam ser isolados das APIs desktop.
4. Integrar overrides e serviços específicos de God of War, removendo diagnósticos e chamadas de host não necessários.
5. Construir/compilar microprogramas VU1 e módulos IRX de forma antecipada. O produto final não deve recorrer a interpretadores de EE/VU1/IOP como caminho normal.
6. Manter o frontend de comandos GS quando portável e desenvolver/adaptar backend Vulkan Android.
7. Conectar o runner ao FORGE e validar em dispositivo real por etapas.

**Não importar em bloco:** o patch de runtime Waffle/KIexster inclui o renderer OpenGL e dependências desktop; sua aplicação inteira ao app Android não está aprovada. A seleção deve acontecer por módulo, com build e testes depois de cada integração.

Consulte [docs/BASE_KIEXTER.md](BASE_KIEXTER.md) para os arquivos e hashes importados, dependências conhecidas e lacunas Android.
