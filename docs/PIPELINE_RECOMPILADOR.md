# Pipeline do recompilador — host privado

**Objetivo desta etapa:** receber somente a ISO original, preparar automaticamente as ferramentas host PS2Recomp/KIexster, extrair em workspace privado o ELF NTSC-U e os dados auxiliares, gerar C++ AOT e compilar o runtime Android ARM64. O recompilador host não é incorporado ao APK e não roda durante a partida.

## Separação de builds

| Etapa | Ambiente | Entrada | Saída |
|---|---|---|---|
| Recompilação estática | Host Windows/Linux com CMake e compilador C++20 | ELF SCUS_973.99, funcmap.csv, template TOML | C++ gerado em diretório privado |
| Runtime e jogo | Android NDK, ABI arm64-v8a | C++ gerado + runtime nativo adaptado | Biblioteca nativa do aplicativo |
| Execução | Android | Biblioteca nativa + arquivos do disco do usuário | Jogo em execução |

O CMake upstream do PS2Recomp desativa PS2X_BUILD_RECOMP quando ANDROID está ativo. Por isso, tentar compilar o gerador dentro do build do APK é o caminho errado. O script abaixo força um build host isolado com runtime, analyzer, testes e Studio desativados.

## Como executar

Pré-requisitos no computador:

- Git.
- Python 3.10 ou superior.
- CMake 3.21 ou superior.
- Compilador C++20 compatível com CMake (MSVC, GCC ou Clang).
- Acesso à internet durante a primeira configuração, pois o CMake baixa as dependências fixadas pelo PS2Recomp.

O único arquivo de jogo fornecido é a ISO original. O orquestrador `tools/build_android_game.py` prepara os commits fixados, executa o extrator KIexster `tools/iso_extract.py --layout`, lê o BOOT2 em `SYSTEM.CNF` e seleciona automaticamente `SCUS_973.99`. O preflight valida ELF32 little-endian, `ET_EXEC`, `EM_MIPS`, tabela de program headers, limites dos segmentos `PT_LOAD` e cobertura do entry point. A seleção não autentica a procedência da mídia; ela apenas rejeita revisões incompatíveis com o mapa NTSC-U.

Na raiz do repositório, execute com o caminho da ISO original:

**python tools/build_android_game.py --iso "D:/Jogos/GOW/God of War.iso" --work-dir "D:/gowport"**

Para que o próprio orquestrador instale o APK recém-compilado e abra o FORGE em um Android autorizado por ADB, acrescente `--install-and-launch`:

**python tools/build_android_game.py --iso "D:/Jogos/GOW/God of War.iso" --work-dir "D:/gowport" --install-and-launch**

Se a ISO original já estiver instalada em armazenamento privado no Android e o app Debug estiver acessível por ADB, é possível usar a própria instalação como origem dos arquivos de build e sincronizar as capturas AOT sem fornecer/extrair arquivos intermediários manualmente:

**python tools/build_android_game.py --installed-android --work-dir "D:/gowport" --install-and-launch**

Se o APK atual tiver outra chave de assinatura Debug, a atualização simples com `adb install -r` será recusada pelo Android. Nesse caso, use `--migrate-installed-debug-signature --install-and-launch`: o script faz backup da instalação privada, valida SYSTEM.CNF/BOOT2 e todos os hashes do manifesto, troca o APK e restaura a instalação antes de abrir o FORGE. Essa migração exige que já exista uma instalação NTSC-U válida no dispositivo e mantém o backup no workspace privado.


O ELF não é exportado pelo usuário. A extração, os módulos IRX, os pacotes e os metadados permanecem no workspace privado. Se um Android Debug autorizado estiver conectado por ADB, o mesmo comando sincroniza as capturas VU1/IOP do armazenamento privado do app; sem capturas, a primeira build é um bootstrap estrito.

Para compilar e validar somente a ferramenta host, sem fornecer ou acessar qualquer arquivo do jogo:

**python tools/build_recompiler_host.py --build-only --work-dir "D:/gowport"**

<badge>CI aprovada — execução 42146201136</badge> O modo build-only compilou com sucesso o alvo ps2_recomp no GitHub Actions, incluindo dependências e aplicação do patch-base. Isso valida a compilação do executável host, mas não prova que o ELF do jogo é analisado corretamente ou que o código C++ gerado compila para ARM64.

Para escolher a pasta privada de trabalho do orquestrador ISO-only:

**python tools/build_recompiler_host.py --elf "D:/Jogos/GOW/SCUS_973.99" --work-dir "D:/gowport"**

Ou defina GOW_ELF e GOW_PRIVATE_WORKDIR no ambiente. O diretório de trabalho precisa ficar fora deste repositório e usar caminho ASCII, pois a revisão fixada do recompilador possui uma limitação conhecida com caminhos não ASCII.

Por segurança, o script:

1. baixa/atualiza cópias de trabalho do KIexter e do PS2Recomp nos commits fixados;
2. aplica o patch-base ps2recomp-waffle-runtime.patch, em uma cópia privada do PS2Recomp;
3. aplica transformações ancoradas no código Waffle para ativar PS2X_IOP_NATIVE_ONLY: se um IRX não tiver bloco AOT, o IOP registra PC/RA e interrompe aquela execução em vez de chamar o interpretador de instruções;
4. no modo IOP estrito, extrai a rotina de exceções para helper independente, condiciona a construção de IopCpuCore e remove core/iop_cpu.cpp da lista do alvo ps2_iop;
5. aplica PS2X_VU1_NATIVE_ONLY ao runtime: força despacho nativo, bloqueia fallback para stepInterpreted, desativa gravação/replay e verificação A/B que usam a engine exata e remove fontes de comparação PCSX2 do alvo estrito;
6. copia o shim NEON do projeto para a árvore privada Waffle e adapta ps2_vu1_native.h para usar intrínsecos NEON sob __aarch64__/__ARM_NEON, mantendo SSE nos builds host x86-64;
7. configura e compila ps2_recomp no host;
8. configura ps2xIOP em um build separado, compila ps2_iop com PS2X_IOP_NATIVE_ONLY=ON e usa compile_commands.json para confirmar que core/iop_cpu.cpp não foi compilado;
9. configura e compila o alvo ps2_runtime host com PS2X_VU1_NATIVE_ONLY=ON, verificando compile_commands.json para confirmar o define VU1 e a exclusão das fontes de diagnóstico PCSX2;
10. no modo --elf, pré-valida a estrutura ELF32/MIPS e os segmentos carregáveis antes de copiar o arquivo; essa checagem não substitui a identificação da revisão do jogo;
11. copia o ELF e o mapa para a pasta privada, preenche @ELF@, @MAP@ e @OUT@ no template, gera os arquivos C++ (inclusive os diretórios aninhados do modo individual) e valida register_functions.cpp e os cabeçalhos de registro.

O script limpa alterações locais dentro das duas cópias Git privadas antes de reaplicar a revisão fixada. Não coloque arquivos próprios dentro de sources/PS2Recomp ou sources/KIexster.

A suíte `tools/test_elf_validation.py` constrói ELF sintéticos em memória temporária e cobre um ELF32/MIPS válido, classe/endian/arquitetura/tipo incompatíveis, tabela de program headers fora dos limites, segmentos `PT_LOAD` inválidos, entry point fora dos segmentos, arquivo curto e magic inválido. Ela não usa executável ou dados do jogo. A CI host executou essa suíte com sucesso nos runs `42186639665` e `42186655605`; a compilação integral do host recompiler continuava em andamento no último polling desses runs.

A CI executa o modo build-only em runner público, sem ISO/ELF, sem geração de código do jogo e sem publicação de artefatos privados. A execução 42150329023 compilou ps2_recomp, ps2_iop e ps2_runtime no host, verificando os perfis estritos. A execução 42150853233 validou a cópia do shim NEON e a seleção condicional no header Waffle no host x86-64. O smoke Android ARM64 separado passou em 42150765223. Em seguida, a execução 42152175783 compilou com sucesso o alvo estático ps2_runtime e suas dependências para arm64-v8a usando NDK 30.0.16248370/API 28; a checagem do compile database confirmou o perfil estrito IOP/VU1 e a ausência de iop_cpu.cpp e das fontes diferenciais PCSX2. O runner NativeActivity não foi construído. A mesma execução concluiu a compilação do APK Debug FORGE. Nenhuma ISO/ELF, IRX ou microcódigo do jogo foi usada.



## Build cruzado do runtime para Android

O comando --android-runtime-only prepara a mesma cópia privada do PS2Recomp/Waffle e aplica as políticas estritas IOP/VU1 e a seleção SSE2NEON/FPCR para ARM64. Em seguida, configura o CMake com o toolchain do NDK e compila apenas ps2_runtime e suas dependências estáticas para arm64-v8a, android-28, c++_shared. O alvo ps2EntryRunner (NativeActivity/Raylib) não é compilado nem vinculado; a aplicação continua usando a própria FORGE/GowGameActivity.

Na CI Android, o workflow chama:

**python tools/build_recompiler_host.py --android-runtime-only --work-dir "$RUNNER_TEMP/gowport-android-runtime"**

A execução 42152175783 usou o NDK 30.0.16248370 e terminou com sucesso para o runtime ARM64 e para o APK Debug. As verificações do builder confirmaram PS2X_VU1_NATIVE_ONLY=1, ausência de iop_cpu.cpp e ausência das fontes diferenciais PCSX2.

**Estado atual:** a preparação privada do Waffle é exportada em `android/app/build/gow-runtime-source` (diretório ignorado pelo Git), consumida pelo CMake do aplicativo e ligada ao alvo `android-game`. O loop nativo GameActivity cria/destrói `PS2Runtime` sem chamar `initialize()`, `loadELF()` ou `run()`. As execuções Android `42159359100` e `42159363088` compilaram essa ligação. Não há VFS conectado ao SAF, C++ do ELF privado nem sessão de jogo; a CI não executa uma Activity num aparelho.

## Dependências do host

A revisão de ps2xRecomp/CMakeLists.txt auditada usa:

| Dependência | Versão/revisão fixada no CMake upstream |
|---|---|
| ELFIO | Release_3.12 |
| toml11 | v4.4.0 |
| fmt | 12.1.0 |
| libdwarf | v2.2.0 |
| Rabbitizer | 1.14.3 |

Essas dependências são obtidas pelo CMake/FetchContent. O script não copia essas bibliotecas para o aplicativo Android. O alvo ps2_recomp é uma ferramenta executável do host; o código C++ que ela gera é que será compilado separadamente pelo Android NDK.

O alvo ps2xRecomp também declara um include privado para ps2xRuntime/include. Por isso, esta etapa mantém a árvore de fontes PS2Recomp coerente com o patch-base de runtime, mas não compila ps2xRuntime nem o runner desktop.

## O que esta etapa não faz

- Não lê nem extrai a ISO.
- Não gera nem compila os módulos IRX do jogo (compila somente o núcleo de serviços ps2_iop e o bloqueio de fallback).
- Não captura nem gera microprogramas VU1.
- Não compila o C++ produzido para ARM64.
- Não gera os arquivos C++ a partir do ELF privado do usuário.
- Não gera automaticamente o C++ do jogo sem que o usuário forneça seu ELF privado.
- Não conecta o VFS aos descritores SAF nem monta a imagem ISO como dispositivo de CD/DVD do runtime.
- Não inicializa o runtime, carrega o ELF em memória guest, executa instruções recompiladas ou inicia a sessão do jogo.
- Não implementa o backend GS/Vulkan, presenter Android, áudio Android, overlay visual de toque ou tela de opções dentro do jogo.
- Não comprova boot ou gameplay.
- Não prova boot ou gameplay.

`--install-and-launch` abre somente o FORGE launcher; não inicia o guest automaticamente nem contorna os gates AOT. Depois da abertura, o operador entra pelo botão INSTALAR JOGO. A opção elimina a etapa manual de localizar e instalar o APK, mas não substitui a validação no aparelho nem declara que o menu foi alcançado.

O primeiro resultado esperado é um conjunto não vazio de fontes C++ gerados em generated/, incluindo register_functions.cpp, ps2_recompiled_functions.h e ps2_recompiled_stubs.h. Com --export-game-cpp-dir, o script copia somente fontes e cabeçalhos para android/app/build/gow-generated-game (diretório ignorado pelo Git). O CMake Android detecta esse conjunto, inclui recursivamente os .cpp gerados no alvo android-game e desativa a tabela vazia de shell; sem código gerado completo, mantém a tabela vazia de zero slots. O build com código gerado ainda depende do ELF privado do usuário e precisa ser validado separadamente, sem publicar o ELF nem o C++ gerado.



## Gate de cobertura para VU1

O gerador port/kiexter/tools/vu1n_gen.py possui a opção --strict. Ao receber capturas privadas de microcódigo, ele enumera os blocos descobertos e falha se não houver imagens válidas, se nenhum bloco for gerado, se qualquer bloco não tiver tradução nativa ou se o C++ produzido contiver callLower/callUpper (chamadas à engine exata). Isso é deliberadamente conservador: pode exigir ampliar o gerador ou registrar novas entradas em vu1n_heads.txt antes que o pacote seja aceito.

A CI usa duas imagens sintéticas de 16 KiB: uma vazia para cobertura nula e outra com uma instrução lower não suportada para verificar que o gerador rejeita callLower/callUpper. Os testes passaram na execução 42149014790; nenhum usa microcódigo do God of War. A geração real deve ocorrer no computador do usuário, com dumps privados; os .cpp gerados permanecem fora do Git e da CI pública. O runtime VU1 também precisará de uma trava equivalente para rejeitar misses em tempo de execução, e a build Android deverá compilar/linkar os blocos gerados no target correto.

## Seleção inicial dos patches KIexster

A lista abaixo foi extraída dos comentários e da ordem de aplicação de scripts/compilar.ps1 no commit auditado. É uma triagem de finalidade, não uma revisão linha a linha de cada diff. Nenhum patch opcional abaixo é aplicado pelo script de geração host, além do patch-base Waffle.

| Patch / grupo | Finalidade descrita pelo upstream | Decisão para Android |
|---|---|---|
| ps2recomp-waffle-runtime.patch | Base Waffle de runtime sobre PS2Recomp | Base necessária para reproduzir a arquitetura KIexster; selecionar/adaptar os módulos Android, não transplantar o runner desktop inteiro |
| ps2recomp-waffle-tests.patch | Remove testes de componentes substituídos pelo runtime Waffle | Não necessário para gerar C++; avaliar quando os testes nativos forem integrados |
| ps2recomp-waffle-gcc.patch | Compatibilidade do runtime com GCC/Linux | Referência útil, mas validar separadamente com Clang/NDK |
| ps2recomp-vu0-vf0.patch, ps2recomp-vu0-vi0.patch | Corrige constantes VF0/VI0 em VU0 | Avaliar como correção de semântica VU0 no runtime |
| ps2recomp-vu1n-heads.patch | Permite entradas extras ao gerador VU1 AOT | Necessário na integração do pipeline VU1; não faz parte da geração EE deste script |
| ps2recomp-vu1n-leak.patch | Corrige vida útil de estado nativo VU1 | Levar ao runtime após revisão e teste de memória |
| ps2recomp-vif1-unpack-rapido.patch, ps2recomp-dma-buffers.patch | Caminhos rápidos VIF1 e buffers DMA reutilizáveis | Otimizações candidatas; primeiro estabelecer testes de equivalência e profiling no ARM64 |
| ps2recomp-vu1-ranura64.patch | Ajusta a estrutura da ranura de despacho VU1 para uma linha de cache | Validar alinhamento, ABI e contenção no ARM64 antes de aceitar |
| ps2recomp-vblank-ntsc.patch, ps2recomp-iop-acumulado.patch, ps2recomp-ritmo-fino.patch, ps2recomp-ciclos-mitad.patch, ps2recomp-ee-ponerse-al-dia.patch, ps2recomp-ee-via-rapida.patch | Temporização VBlank, ciclos EE/IOP e checkpoints | Não copiar como pacote: verificar a semântica de relógio, sincronização e frame pacing no Android |
| ps2recomp-ee-120hz.patch | Modo opcional que aumenta a frequência do VBlank/lógica do EE para 120 Hz | Não habilitar por padrão; só avaliar depois de estabilidade NTSC, áudio e frame pacing medidos |
| ps2recomp-cd-stop.patch, ps2recomp-iop-delay-fuera.patch, ps2recomp-mpeg-fin-programa.patch | Corrige operações CD/IOP e término de stream MPEG | Candidatos de correção funcional; reproduzir testes de disco, áudio e FMV |
| ps2recomp-vu1-invalidacion-rango.patch, ps2recomp-vu1-madd-saturado.patch | Invalidação seletiva AOT VU1 e saturação MADD/MSUB | Candidatos importantes para correção VU1; validar com microprogramas capturados |
| ps2recomp-vu-redondeo.patch, ps2recomp-fpu-barrera.patch, ps2recomp-vu1-clip-sinsaltos.patch | MXCSR, barreiras FPU e otimização SSE | Não transplantar a implementação x86. Mapear arredondamento para FPCR/FPSR ARM64 e avaliar NEON/implementação portátil |
| ps2recomp-gs-z-como-color.patch, ps2recomp-gs-volumenes-agrupados.patch | Semântica de Z como cor e otimização de volumes GS | Portar ao modelo de recursos/pipelines Vulkan; validar imagens de referência |
| ps2recomp-gs-stage-lotes.patch, ps2recomp-gs-prioridad-presentacion.patch, ps2recomp-gs-finish-anticipado.patch, ps2recomp-gs-cuadros-en-vuelo.patch, ps2recomp-pantalla-latch.patch | Lotes, prioridade de apresentação, FINISH, frames em voo e latch de tela | Reavaliar com filas, fences, semáforos e apresentação Android Vulkan; não copiar pressupostos do renderer OpenGL/desktop |
| ps2recomp-gs-egl-linux.patch | Contexto EGL no desktop Linux | Não é backend Android Vulkan; não transplantar |
| ps2recomp-fps-overlay.patch, ps2recomp-fps-imagenes.patch, ps2recomp-gs-worker-trace.patch | Overlay e diagnóstico de FPS/worker | Manter somente diagnóstico útil, adaptado ao Logcat/telemetria Android |
| ps2recomp-pantalla-completa.patch | Janela desktop sem bordas/atalhos | Ignorar; lifecycle e tela cheia pertencem ao Android |
| ps2recomp-pad-rumble-scan.patch, ps2recomp-prueba-sin-mando.patch | Varredura de HID desktop e modo de teste | Substituir pela entrada Android/GameController; modo de teste só em build de desenvolvimento |
| ps2recomp-vu1-captura-nativa.patch | Captura de microcódigo com VU1 nativa ativa | Ferramenta de diagnóstico privada, não componente obrigatório de release |

### Critério para aceitar um patch no runtime Android

Antes de transplantar um patch: identificar os arquivos tocados; separar mudanças de compilador, runtime e host; conferir intrinsics/assembly e APIs de sistema; aplicar isoladamente; compilar ARM64; executar teste de regressão relevante; registrar [VERIFICADO] somente com evidência. Otimizações de desktop não são consideradas corretas ou úteis automaticamente.

## Referências fixadas

- [KIexster — commit auditado](https://github.com/KIexster/god-of-war-recomp/tree/a226151feb6ed22937e42be485aaf28a83acd166)
- [Script de compilação KIexster](https://github.com/KIexster/god-of-war-recomp/blob/a226151feb6ed22937e42be485aaf28a83acd166/scripts/compilar.ps1)
- [PS2Recomp — commit fixado](https://github.com/ran-j/PS2Recomp/tree/c5a9d02573410a2085a4b4b831b0b68ba3515440)
- [Runtime Waffle565](https://github.com/Waffle565/God-Of-War-Native-Port/tree/runtime)
