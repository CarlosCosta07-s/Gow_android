# Créditos e avisos de terceiros

Este documento registra as bibliotecas atuais e os componentes de código reutilizados seletivamente para acelerar o port nativo. O runtime ainda está em integração; os arquivos descritos como staging não fazem parte do APK neste momento.

## Dependências atuais do shell Android

| Componente | Uso atual | Versão / licença |
|---|---|---|
| [AndroidX AppCompat](https://developer.android.com/jetpack/androidx/releases/appcompat) | Activity e diálogos do launcher FORGE. | `androidx.appcompat:appcompat:1.8.0`; Apache-2.0. |
| [AndroidX Core](https://developer.android.com/jetpack/androidx/releases/core) | APIs de compatibilidade Android. | `androidx.core:core:1.19.1`; Apache-2.0. |
| [AndroidX GameActivity](https://developer.android.com/games/agdk/game-activity) | Activity nativa e integração de lifecycle/eventos com C++. | `androidx.games:games-activity:4.4.2`; Apache-2.0. |
| [Android NDK](https://developer.android.com/ndk) | Toolchain, headers e APIs nativas Android. | NDK `30.0.16248370`; toolchain Android. |
| Vulkan | API gráfica do sistema/driver. | API e loader do Android NDK/sistema. |

## Código reutilizado — KIexster/god-of-war-recomp

Origem: https://github.com/KIexster/god-of-war-recomp  
Commit de origem: `a226151feb6ed22937e42be485aaf28a83acd166`  
Licença declarada: **GNU GPL-3.0**.

| Arquivo neste repositório | Origem no KIexster | SHA do blob de origem | Estado |
|---|---|---|---|
| `recompiler/config/funcmap.csv` | `config/funcmap.csv` | `b1253abb001fbf1ac0622b751aa1ca95b35f634c` | Importado. |
| `recompiler/config/recomp.template.toml` | `config/recomp.template.toml` | `8296991acfd428ab4c2702917725682ba67d26c7` | Importado. |
| `port/kiexter/gow_overrides.cpp` | `src/gow_overrides.cpp` | `6f73d16bc59d34f780317bf09c69c3a1f863fc0e` | Importado para staging; contém código com proveniência Waffle abaixo. |
| `port/kiexter/tools/vu1n_gen.py` | `tools/vu1/vu1n_gen.py` | `4ddb2c1d81b1717240d9259af8457cc860d95eea` | Importado para staging; cabeçalho identifica Waffle como origem. |
| `port/kiexter/tools/iopn_gen.py` | `tools/iop/iopn_gen.py` | `f1e540f6bac22435855ff0b22661167fe3f953da` | Importado para staging; cabeçalho identifica Waffle como origem. |
| `port/kiexter/tools/vu1n_heads.txt` | `tools/vu1/vu1n_heads.txt` | `a50466e46bb27e232be8f058e305ed118c4f88ce` | Importado. |
| `port/kiexter/gow_pad2_packet.h` | `src/gow_pad2_packet.h` | `ca1c082b6793e54baf789e3d45122ac8d748eec8` | Importado. |

Os cabeçalhos originais foram preservados. Os arquivos em staging ainda não estão conectados ao build Android.

## Proveniência adicional — Waffle565/God-Of-War-Native-Port

Origem: https://github.com/Waffle565/God-Of-War-Native-Port  
Branch de runtime: `runtime`  
Licença presente nessa branch: **GNU GPL-3.0**.

O port KIexster documenta o runtime Waffle como sua base. Os cabeçalhos de `gow_overrides.cpp`, `vu1n_gen.py` e `iopn_gen.py` identificam a origem Waffle no commit `34f30d5e432751369177af115104f4d19af6d080`. A referência de migração do runtime no projeto KIexster é o diff `c5a9d02..runtime`, associado ao commit `49ca62c`.

**Atenção:** ainda não copiamos o runtime Waffle inteiro nem aplicamos o patch de migração completo. A seleção de módulos portáveis e a adaptação do host/GS Android serão registradas antes de cada integração.

## Ferramenta de build prevista — PS2Recomp

Origem: https://github.com/ran-j/PS2Recomp  
Commit fixado pelo pipeline KIexster: `c5a9d02573410a2085a4b4b831b0b68ba3515440`  
Licença declarada: **GNU GPL-3.0**.

PS2Recomp ainda não foi importado neste repositório. A intenção é usá-lo como ferramenta de build host para gerar C++ do ELF privado do usuário e compilar esse código para ARM64. A ferramenta não fará parte do APK nem executará durante a partida.

## Referências sem código incorporado

| Projeto | Uso |
|---|---|
| [Black para Android](https://github.com/RanggaCasper/Black-Android) | Referência de arquitetura ARM64/Vulkan e controles Android. |
| [Dragon Rage / Budokai Tenkaichi 3](https://github.com/dfdx047/Tenkaichi3DecompAndroid) | Referência de integração de uma engine PS2 e validação em GPUs móveis. |
| [God of War: Recomp — CodeAbhi826](https://github.com/CodeAbhi826/God-Of-War-Recomp) | Referência documental Android; nenhum código/submódulo permanece no projeto. |
| [PCSX2](https://github.com/PCSX2/pcsx2) | Referência comportamental; componentes PCSX2 dentro de futuras importações devem preservar GPL-3.0-or-later e seus avisos. |

## Regras de preservação

1. Preservar LICENSE, cabeçalhos de copyright, SPDX e atribuições de cada arquivo reutilizado.
2. Registrar repositório, commit, caminho de origem, hash do arquivo, modificações e destino local.
3. Marcar arquivos modificados como modificados; nunca atribuir ao Gow_android autoria de código upstream.
4. Auditar a licença de bibliotecas transitivas, especialmente componentes derivados de PCSX2.
5. Não copiar runner desktop, OpenGL/WGL, Raylib host ou controles de Windows para o Android sem adaptação e revisão específica.
6. Não incluir ISO, ELF original, IRX, PAK, dumps de microcódigo ou C++ gerado a partir dos dados do jogo no Git ou na CI pública.

O projeto Gow_android permanece sob GPL-3.0 para o código que pode licenciar. Essa licença não concede direitos sobre o código original, marcas, personagens ou conteúdo de God of War.
