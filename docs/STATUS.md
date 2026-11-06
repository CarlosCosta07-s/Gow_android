# Estado do projeto

**Data da auditoria:** 2026-11-06  
**Branch de desenvolvimento:** `main`  
**Objetivo:** port nativo de God of War (PS2, 2005) para Android ARM64, usando recompilação estática e runtime de serviços PS2 nativo; não é um emulador.

## [DECISÃO ARQUITETURAL] Instalação BT3

- O fluxo final escolhido é a extração seletiva para o armazenamento privado do aplicativo, inspirado no instalador BT3 do Dragon Rage.
- A ISO será fonte de importação; o runtime deverá ler do `cdRoot` local depois que a instalação estiver validada.
- Arquivos/pacotes de dados grandes serão preservados nos formatos originais. Não haverá preload de toda a instalação na RAM.
- O manifesto de God of War ainda precisa ser derivado da árvore real da imagem e do rastreamento de boot. Os caminhos do Dragon Rage não serão presumidos.
- O leitor ISO e a ponte híbrida já compilada serão reaproveitados onde fizer sentido, mas o provider que mantém a ISO aberta durante o jogo é transitório e não define a arquitetura final.
- O objetivo de desempenho em aparelhos modestos será avaliado por frame time, 1% low, memória, I/O e temperatura em hardware real. A instalação BT3, isoladamente, não comprova aumento de FPS.

## Prioridade atual

**Fazer God of War funcionar vem antes de otimização fina.** O projeto ainda não comprovou boot real nem gameplay. O foco é gerar e integrar o código do jogo, completar runtime/IOP/VU1, GS/Vulkan, serviços de boot e alcançar primeira imagem, menu e primeiro combate. O tuning para aparelhos modestos fica para depois dessa linha de base.

**Referência futura de otimização:** [Black for Android](https://github.com/RanggaCasper/Black-Android), documentada em [ANDROID_LOW_END_OPTIMIZATION.md](ANDROID_LOW_END_OPTIMIZATION.md). Não estamos adotando agora suas opções nem assumindo equivalência entre Xbox e PS2.

## [VERIFICADO] Enumeração de diretórios ISO9660

- `Iso9660Reader::listDirectory()` lista os filhos imediatos de um caminho de diretório, preservando a ordem dos registros da imagem e retornando nome normalizado, tipo, offset, tamanho e marcador de multi-extent.
- Limites: 64 MiB por diretório e 65.536 entradas; caminhos com traversal e subdiretórios inválidos são rejeitados.
- A fixture sintética cobre listagem da raiz e de `DATA/`, metadados do arquivo `TEST.BIN`, rejeição de traversal e rejeição de caminho de arquivo como diretório.
- A etapa host dos testes passou no Android workflow run `42163257138`. A compilação completa do APK desse run ainda estava em andamento no último polling; não declarar o APK concluído com base apenas no teste host.

## [VERIFICADO] Shell FORGE

- O aplicativo FORGE foi aberto em pelo menos um aparelho Android real, conforme relato do mantenedor.
- A CI compilou o APK Debug ARM64 no run `42142814087`, commit `18839524eb1061c5331b5762213dadaa51d55dd3`.
- O teste Vulkan isolado foi removido do launcher após fechar o aplicativo no aparelho. A validação gráfica será feita com o GS e o jogo real.
- O launcher usa Storage Access Framework para escolher a imagem do usuário.
- A implementação inicial lê ISO9660 e `SYSTEM.CNF`, interpreta `BOOT2` e reconhece inicialmente `SCUS_973.99` (NTSC-U) e `SCES_531.33` (PAL). Ela compilou na CI; ainda falta validação com imagens reais em aparelho.

## [VERIFICADO] Componentes KIexter importados

Fonte: [KIexster/god-of-war-recomp](https://github.com/KIexster/god-of-war-recomp), commit `a226151feb6ed22937e42be485aaf28a83acd166`, licença declarada GPL-3.0.

- `recompiler/config/funcmap.csv`: mapa das 6.414 funções de `SCUS_973.99`. Verificação estrutural: intervalos ordenados, tamanhos coerentes e sem sobreposições.
- `recompiler/config/recomp.template.toml`: configuração de stubs, entry points, skips e patches do executável NTSC-U; marcadores `@ELF@`, `@MAP@`, `@OUT@` presentes.
- `port/kiexter/gow_overrides.cpp`: overrides específicos de God of War, em staging.
- `port/kiexter/tools/vu1n_gen.py`: gerador AOT VU1, em staging.
- `port/kiexter/tools/iopn_gen.py`: gerador AOT dos módulos IRX/IOP, em staging.
- `port/kiexter/tools/vu1n_heads.txt` e `port/kiexter/gow_pad2_packet.h`: arquivos auxiliares importados.

**Importante:** os arquivos de staging não estão no CMake/Gradle e ainda não são compilados pelo APK. O override depende do runtime Waffle/KIexster e de headers auxiliares; os geradores exigem dumps privados produzidos a partir da cópia do usuário.

## [VERIFICADO] Proveniência das bases

- O projeto KIexster usa como base o runtime de [Waffle565/God-Of-War-Native-Port](https://github.com/Waffle565/God-Of-War-Native-Port), branch `runtime`, licença GPL-3.0 nessa branch.
- Os cabeçalhos dos overrides e dos geradores VU1/IOP apontam para código originalmente derivado de Waffle565, commit `34f30d5e432751369177af115104f4d19af6d080`.
- O build KIexster fixa [PS2Recomp](https://github.com/ran-j/PS2Recomp) no commit `c5a9d02573410a2085a4b4b831b0b68ba3515440`, licença GPL-3.0.
- Nenhum runtime Waffle completo, PS2Recomp ou renderer desktop foi importado ainda. O antigo submódulo CodeAbhi foi removido.

## [VERIFICADO] Bloqueios identificados na configuração Waffle

Auditoria estática do CMake e dos fontes na revisão Waffle de referência 49ca62c1d20b4659c61df6286c54e86edf411012:

- O CMake upstream inclui fontes ee_ref e vu_pcsx2 para teste diferencial/diagnóstico PCSX2. O perfil PS2X_VU1_NATIVE_ONLY agora as retira de ps2_runtime; no IOP, PS2X_IOP_NATIVE_ONLY também exclui core/iop_cpu.cpp. Builds de diagnóstico não estritos devem preservar os avisos PCSX2 GPL-3.0-or-later.
- ps2_vu1_core.cpp contém tanto o dispatcher AOT quanto rotinas da engine exata. No perfil estrito, o dispatcher não chama stepInterpreted quando um bloco não existe: registra diagnóstico e interrompe a VU1. O arquivo continua compilado porque abriga o próprio backend AOT e outros contratos; isso não equivale à remoção integral de todo código exato do mesmo translation unit.
- gs_gl.cpp é backend OpenGL e ps2_audio_device.cpp usa miniaudio/Raylib do host; substituir por GS/Vulkan Android e saída de áudio Android.
- ps2_pad_rumble_win.cpp é específico de Windows. ps2_android_runtime.cpp contém apenas um TODO de controles touch.
- ps2_vif1_interpreter.cpp decodifica comandos VIF1/UNPACK; não é o interpretador de instruções MIPS do EE. A decisão sobre sua permanência é por função de hardware, separada da regra contra fallback de CPU.

Essa revisão é referência para identificar fronteiras. O runtime Waffle ainda não está integrado ao target Android do aplicativo; sua build estrita host é uma verificação privada de compilação.

## [VERIFICADO] Ferramenta PS2Recomp no host

- O script tools/build_recompiler_host.py passou em py_compile e no CLI --help.
- O modo --build-only baixou as revisões fixadas, aplicou ps2recomp-waffle-runtime.patch e compilou o alvo ps2_recomp com sucesso na execução GitHub Actions 42146201136, commit a8e373fda6d4f7ab88980dcb18d7f06d818e51ad.
- O log confirmou a compilação das dependências, de ps2_recomp_lib e do executável ps2_recomp.
- A execução não acessou ELF, ISO ou outro arquivo do jogo e não gerou C++ derivado do jogo.

## [VERIFICADO] IOP com fallback interpretado bloqueado na CI host

- O script aplica uma proteção em ps2xIOP/src/emulator/iop_emulator.cpp: após as rotas de import/HLE, overrides nativos e blocos IRX AOT, a ausência de uma faixa nativa gera diagnóstico [IOP:native-required], registra PC/RA, marca a execução como parada e retorna falha em vez de chamar iop_cpu.executeInstruction.
- O CMake introduz PS2X_IOP_NATIVE_ONLY; a opção é forçada para ON quando ANDROID está ativo. No modo estrito, o arquivo core/iop_cpu.cpp é removido da lista de fontes; IopCpuCore deixa de ser construído e a rotina de exceções é fornecida por helper independente.
- A execução GitHub Actions 42148409363 compilou com sucesso ps2_recomp e a biblioteca ps2_iop. O script analisou compile_commands.json e confirmou que nenhuma unidade iop_cpu.cpp foi compilada.
- O build utilizou iop_native_none.cpp, sem módulos IRX do jogo. Isso confirma a exclusão da unidade de interpretação e a compilação da proteção, não a cobertura AOT dos módulos reais nem uma execução do runtime no aparelho.
- O teste foi feito com GNU C++ no runner Linux; ainda falta compilar com Android NDK/ARM64 e validar com IRX privados.

## [VERIFICADO] Runtime Waffle estrito compilado no host

- O perfil PS2X_VU1_NATIVE_ONLY força o dispatcher nativo VU1 e bloqueia o caminho para stepInterpreted quando runNative não encontra um bloco registrado. O diagnóstico contém PC, instruções lower/upper, ciclo e flags de parada; a execução é interrompida.
- O perfil desativa gravação/replay e verificação A/B que usam a engine exata como oráculo.
- A configuração CMake estrita remove de ps2_runtime as fontes de diagnóstico diferencial ee_ref, ee_watch e vu_pcsx2; elas são mantidas somente em builds de diagnóstico não estritos.
- A execução GitHub Actions 42150329023 compilou ps2_runtime, ps2_iop e ps2_recomp com GNU C++ no runner Linux. compile_commands.json confirmou PS2X_VU1_NATIVE_ONLY=1 em ps2_vu1_core.cpp, nenhuma unidade iop_cpu.cpp e nenhuma fonte de diagnóstico PCSX2 no alvo estrito.
- Nenhum ELF, IRX ou microcódigo do jogo foi fornecido. Isso comprova a compilação das políticas e do runtime host; ainda não comprova cobertura AOT real ou execução no Android.

## [PREPARADO, COM TESTE SINTÉTICO] VU1 AOT estrito

- port/kiexter/tools/vu1n_gen.py agora aceita --strict: falha se nenhuma imagem válida for processada, nenhum bloco for gerado, qualquer bloco descoberto não tiver implementação nativa ou um bloco gerado contiver callLower/callUpper (chamadas para a engine exata/interpreter).
- A CI inclui dois microcódigos sintéticos: um vazio e outro com instrução lower não suportada que gera callLower. Ambos são rejeitados pelo gate; nenhum usa microcódigo do jogo.
- A trava PS2X_VU1_NATIVE_ONLY já compilou no runtime host (run 42150329023). A opção --strict ainda não foi executada com capturas reais; o runtime interromperá a execução no primeiro bloco não coberto. Ainda falta ligar os arquivos AOT gerados ao target Android.

## [VERIFICADO] Runtime Waffle ARM64 e camada SIMD NEON

- Foi criado port/kiexter/vu1/ps2_vu1_neon_compat.h para mapear a API SSE usada pela VU1 para NEON e para manter o controle de arredondamento usando FPCR/FPSR no Android.
- No runtime Waffle, o shim tem dois modos: smoke isolado usa as implementações NEON próprias; o runtime completo usa sse2neon para a API SSE2 mais ampla de EE/VU0, e o shim sobrescreve MAX/MIN e o mapeamento de CSR para FPCR/FPSR.
- O worker GS e ps2_runtime.cpp também selecionam o shim em ARM64, evitando includes diretos x86-only de xmmintrin.h.
- A execução Android shell 42152175783 compilou ps2_runtime e suas dependências estáticas com Android NDK 30.0.16248370, ABI arm64-v8a e API 28. O compile database confirmou PS2X_VU1_NATIVE_ONLY=1 e excluiu iop_cpu.cpp e as fontes diferenciais PCSX2. O runner NativeActivity do Waffle não foi construído.
- A mesma execução concluiu BUILD SUCCESSFUL para o APK Debug FORGE. O arquivo de smoke NEON continua incluído no alvo android-game, mas não é executado em runtime.
- Isso comprova compilação ARM64 do runtime e do shell, não equivalência numérica bit-exata, execução em aparelho, ligação do runtime ao APK, chamada JNI ou gameplay. O runtime permanece como biblioteca estática produzida em workspace privado e não está integrado à GowGameActivity.

## [PREPARADO, NÃO TESTADO] Geração do C++ do jogo

- O mesmo script aceita --elf e usa funcmap.csv + recomp.template.toml para gerar C++ em diretório privado.
- Esta etapa ainda não foi executada com o ELF SCUS_973.99 do usuário.
- A triagem dos patches e as instruções estão em docs/PIPELINE_RECOMPILADOR.md e a seleção do runtime está em docs/BASE_KIEXTER.md.
- Não há ainda evidência de geração C++ do jogo, compilação ARM64 ou integração ao APK.

## [NÃO TESTADO] Runtime e jogo

- Geração do C++ do ELF privado com PS2Recomp e compilação do código gerado para Android ARM64; nenhum ELF de God of War foi fornecido.
- Cobertura VU1 AOT para todos os microprogramas reais e geração AOT dos módulos IRX necessários.
- Envio efetivo do FD selecionado em MainActivity para GowGameActivity; a recepção JNI/EXTRA_DISC_FD está preparada, mas o launcher ainda não inicia a Activity.
- Acesso de uma imagem real pelo provider ISO, enumeração de diretórios e chamadas reais de CD/DVD/IOP.
- Cache privado do ELF e dos arquivos pequenos de boot realmente necessários.
- Backend GS/Vulkan Android, presenter e saída de áudio Android.
- Boot, primeira imagem, menus, controles touch visuais, áudio, FMVs, saves, desempenho e compatibilidade em aparelhos reais.

## Próximos bloqueios reais — ordem de prioridade

1. Fornecer o ELF privado ao PS2Recomp, gerar C++ e ligar a tabela real para ARM64.
2. Gerar e integrar VU1/IRX AOT com cobertura comprovada para os módulos realmente usados.
3. Integrar CD/DVD, SIF/DMA e demais serviços necessários à cadeia de boot.
4. Completar a enumeração ISO9660 e definir o manifesto BT3 específico da revisão NTSC-U suportada.
5. Implementar a extração privada com estimativa de espaço, staging, progresso, cancelamento, integridade e publicação atômica.
6. Apontar o loader ELF e o VFS para a instalação local; substituir o provider híbrido de FD no caminho normal de execução.
7. Implementar e integrar o GS/Vulkan Android e presenter, de modo que a primeira imagem real possa ser apresentada.
8. Validar instalação, boot, menu, novo jogo e primeiro combate com uma ISO do usuário em aparelho real.
9. Integrar saída de áudio Android e concluir controles, saves e demais componentes de gameplay.

**Adiado:** medir e otimizar FPS/1% low, cache/prefetch, resolução e comportamento térmico em aparelhos modestos. Essa etapa começa depois do primeiro gameplay funcional; a referência definida é Black for Android.

**Nenhum gameplay foi testado ou declarado funcional.** Nenhum ELF, ISO, IRX, microcódigo, save ou dado proprietário foi enviado ao repositório ou à CI pública.
