# Arquitetura técnica — Gow_android

**Estado:** shell Android ARM64, launcher SAF/ISO9660, ícone adaptativo e interface FORGE compilados pela CI e abertos em aparelho Android real segundo o mantenedor; ainda não executa o jogo.

## Objetivo

Criar uma aplicação nativa para Android que execute *God of War* (PS2, 2005) por recompilação estática do código MIPS R5900 para C++/ARM64 e substitua os serviços de hardware do PS2 usados pelo jogo por implementações nativas Android.

O produto final não será um emulador genérico nem dependerá de interpretação/JIT do Emotion Engine como caminho de execução do jogo. A aplicação não incluirá a ISO ou os dados proprietários do jogo.

## Alvo inicial

- Plataforma exclusiva: Android.
- ABI inicial: `arm64-v8a`.
- Linguagem nativa: C++20.
- Build nativo: CMake + Android NDK `30.0.16248370`.
- Aplicativo: Gradle 9.6.0, Android Gradle Plugin 9.5.0 e AndroidX AppCompat 1.8.0, Core 1.19.1 e GameActivity 4.4.2.
- SDK de compilação: API 37; target SDK: API 36.
- API mínima inicial: Android 9 / API 28.
- Gráficos: Vulkan nativo, com surface e swapchain Android reais.
- Primeiro alvo de compatibilidade: NTSC-U, serial `SCUS-97399`, por ser a revisão mais documentada nas referências auditadas.
- PAL e outras revisões ficam para uma etapa posterior.

Essas versões fixam a primeira build de CI; serão atualizadas deliberadamente, não por dependências flutuantes.

## Estado do shell implementado

O código executável inicial do projeto está em:

- `android/app`: configuração Gradle, manifesto e Activity.
- `android/app/src/main/java/org/gowandroid/app/MainActivity.java`: launcher AppCompat temático, menu nativo, seletor SAF, validação ISO9660 e parser inicial de SYSTEM.CNF/serial.
- `android/app/src/main/res/mipmap-anydpi-v26/ic_launcher.xml`: ícone adaptativo do aplicativo.
- `android/app/src/main/res/drawable-nodpi/ic_launcher_foreground.webp`: arte original do ícone do projeto.
- `android/app/src/main/java/org/gowandroid/app/GowGameActivity.java`: Activity baseada em AndroidX GameActivity.
- `android/app/src/main/cpp/main.cpp`: ciclo nativo e smoke test Vulkan.
- `.github/workflows/android-shell.yml`: build de APK Debug ARM64.

O código nativo inicial em `main.cpp` ainda contém um bring-up Vulkan de clear/present. Embora compile na CI, o mantenedor observou que iniciar esse teste separado fecha o aplicativo no aparelho. Por decisão de escopo, o botão foi removido; esse teste não será tratado como critério de progresso nem como prova de compatibilidade gráfica. A atividade nativa será reaproveitada para receber o runtime real. A validação Vulkan acontecerá por meio do GS e de frames reais do jogo.

A interface FORGE e o seletor SAF/ISO9660 estão implementados. O parser de `SYSTEM.CNF` e a identificação inicial de `SCUS_973.99`/`SCES_531.33` foram compilados com sucesso pela CI no run `42142814087`; ainda aguardam validação com imagens de teste em aparelho. Não há frontend GS, código recompilado do jogo ou runtime de God of War integrado.

## Camadas futuras do port

### 1. Aplicativo Android

Responsabilidades:

- Tela inicial, seleção do disco e compatibilidade.
- Storage Access Framework para abrir arquivo escolhido pelo usuário.
- Permissão persistente de leitura quando o provedor permitir.
- Identificação da região/revisão e diagnóstico.
- Orientação horizontal, ciclo de vida, foco, pausa e recuperação da superfície.
- Configuração de controles, interface de toque e controles físicos.
- Gerenciamento de saves e exportação/importação por seletor Android.

**Decisão:** usar AndroidX GameActivity. A documentação Android recomenda GameActivity para jogos novos; ela fornece uma SurfaceView e entrega eventos/lifecycle ao código C/C++.

### 2. Disco e dados do usuário

Fluxo previsto:

1. Receber URI pelo seletor Android (`MainActivity` já implementa esta etapa).
2. Abrir o fluxo de leitura e tentar preservar a permissão concedida.
3. Validar ISO9660, descritor primário no setor 16.
4. Ler a entrada de diretório raiz e `SYSTEM.CNF`; o parser inicial reconhece as revisões NTSC-U/PAL conhecidas e já passou na compilação CI (run `42142814087`), mas ainda aguarda validação física.
5. Ler diretamente do descritor ou preparar somente elementos de boot estritamente necessários no diretório privado.
6. Nunca enviar dados do usuário para repositório, Issues, releases ou CI pública.

A ISO não será copiada para o APK nem armazenada no Git.

### 3. Recompilador estático

Ferramenta do pipeline de build, não parte do loop de jogo:

- Usar PS2Recomp, na revisão fixada pelo KIexster, como base do decoder/analyzer/lifter MIPS R5900.
- Usar `recompiler/config/funcmap.csv` e `recomp.template.toml` já importados como configuração específica de `SCUS_973.99`.
- Adaptar geração e compilação do C++ resultante para `arm64-v8a`, corrigindo diferenças de ABI, intrinsics e alinhamento.
- Manter ELF, ISO, mapa derivado e C++ gerado a partir do executável original em ambiente privado, fora do Git e da CI pública.
- Registrar commit do recompilador, parâmetros, hashes dos insumos privados e relatório de funções traduzidas/pendentes.

O compilador será executado no ambiente host de build. O APK recebe apenas código nativo gerado/compilado; não interpreta nem recompila o ELF durante a partida.

### 4. Runtime EE e unidades vetoriais

A base Waffle/KIexster já contém estado de EE, memória, scheduler, stubs e serviços usados pelo executável. Vamos selecionar e adaptar esses módulos, em vez de reimplementar tudo:

- Memória, registradores, exceções, syscalls, threads, timers e interrupções do EE.
- Operações MMI e ligação das funções recompiladas aos serviços nativos.
- VU0 macro e VU1 AOT.
- Checkpoints e diagnóstico de PC, RA, SP, thread e último subsistema confirmado.

As APIs e sincronização que dependem do runner desktop serão substituídas pelo host Android. Não será mantido um caminho de interpretação/JIT do EE no produto final.

### 5. Subsistemas PS2 utilizados

Reutilizar os serviços Waffle/KIexster que já implementam os contratos utilizados pelo jogo e adaptá-los ao host Android:

- IOP/R3000A e módulos IRX; os módulos necessários devem ser gerados/compilados antecipadamente para evitar interpretador no caminho final.
- SIF, DMA, VIF e GIF, preservando o fluxo de pacotes necessário entre EE, VU1 e GS.
- CD/DVD e leitura dos arquivos no layout esperado pelo port.
- Cartão de memória virtual e operações save/load.
- SPU2/LIBSD e temporização de áudio.
- Serviços de controle que reproduzem a semântica DualShock 2.

Cada subsistema terá logs e contadores próprios. A seleção de arquivos pelo SAF precisa alimentar o VFS/cache que o runtime espera; o simples URI do ISO não satisfaz automaticamente esse contrato.

### 6. Renderizador Graphics Synthesizer (GS) via Vulkan

O código experimental de clear/present não equivale a modelar o Graphics Synthesizer e falhou ao ser iniciado no aparelho do mantenedor. Não é uma dependência para começar o trabalho no runtime; o backend GS será integrado e validado com comandos e frames reais do jogo.

Estratégia gráfica:

1. Reaproveitar o frontend de comandos GS/GIF e as rotinas de formatos, memória, CLUT e swizzle do runtime KIexster/Waffle onde não estiverem acopladas ao OpenGL.
2. Não transplantar o backend `gs_gl`, WGL nem o host Raylib do desktop.
3. Implementar o backend GS/Vulkan para Android: render targets, depth, alpha test, destination-alpha, blending, texturas e sincronização.
4. Substituir mecanismos OpenGL específicos por pipelines, descriptors, render passes/dynamic rendering e recursos Vulkan compatíveis com o aparelho.
5. Comparar a primeira imagem do jogo e cenas de referência, não um clear/present isolado.
6. Medir sincronização, filas, memória e frame time em GPUs móveis reais.

**Critério do renderer:** primeira imagem real do jogo apresentada na tela Android. Um triângulo, clear pass ou probe não conclui esta fase.

### 7. Áudio

O motor de som SPU2/LIBSD do runtime Waffle/KIexster é um candidato a reaproveitamento. A saída de áudio específica do PC será substituída por um adaptador AAudio Android. Buffers e sincronização permanecem responsabilidade do runtime; o callback não deve bloquear, alocar memória imprevisivelmente nem ler disco.

### 8. Entrada e interface

- Reaproveitar a tradução dos comandos de pad do jogo quando independente do host.
- Implementar overlay de toque configurável no FORGE/runtime Android.
- Mapear gamepad Bluetooth/USB para a semântica DualShock 2 usando eventos Android.
- Remover XInput/Win32 e chamadas de janela desktop.
- Tratar pausa, perda de foco, reconexão, retorno e feedback tátil.

### 9. Frame pacing e desempenho

Separar relógio da lógica, submissão GPU e apresentação. Usar inicialmente AChoreographer/Choreographer e os mecanismos de sincronização Vulkan do sistema; soluções externas de frame pacing só serão avaliadas se medições demonstrarem necessidade. Prioridade: estabilidade e velocidade correta; metas de FPS só serão publicadas após medições reproduzíveis.

## Estratégia de reaproveitamento seletivo

**A base principal é KIexster/god-of-war-recomp.** O objetivo é aproveitar a recompilação, configuração e comportamento específicos de God of War já trabalhados no PC, não reconstruir todo o software. Os arquivos já importados e seus hashes estão em [BASE_KIEXTER.md](BASE_KIEXTER.md).

A composição da base tem três origens que precisam permanecer identificadas:

1. **KIexster:** mapa de 6.414 funções, template de configuração, overrides específicos de God of War e ajustes/diagnósticos do port de PC.
2. **Waffle565:** runtime nativo usado pelo KIexster, incluindo serviços EE/IOP/VU1/GS, áudio, pad e memória. Os três arquivos de código importados já indicam essa proveniência nos cabeçalhos.
3. **PS2Recomp:** ferramenta de análise e recompilação estática MIPS R5900 → C++. O script KIexster fixa a revisão `c5a9d02573410a2085a4b4b831b0b68ba3515440`.

### Regra de seleção

- Reaproveitar os módulos portáveis e os hooks necessários ao jogo.
- Adaptar ou substituir tudo que dependa de Windows, Raylib desktop, OpenGL/WGL, teclado/mouse ou dispositivos de áudio do PC.
- Preservar créditos, cabeçalhos, GPL-3.0 e avisos de componentes derivados de PCSX2.
- Não importar o runtime Waffle inteiro nem aplicar a pilha de patches desktop sem separar as dependências.
- Não incluir ISO, ELF, IRX original, PAK, dumps de microcódigo ou C++ gerado a partir do ELF no repositório público.

### Caminho Android

O launcher FORGE e a seleção SAF continuam sendo nossa camada Android. O runtime será ligado à atividade nativa e receberá os dados do jogo por um cache/VFS privado compatível com o layout usado pela base KIexster. A camada gráfica usará o frontend GS que puder ser separado do host e um backend Vulkan Android; o backend OpenGL do PC não será usado diretamente.

A geração AOT de VU1 e dos módulos IRX deve ocorrer como ferramenta host, antes da compilação do APK. O CMake de referência exclui a geração VU1 quando `PS2X_IS_ANDROID` está ativo; essa etapa precisa ser separada do build Android e seus resultados compilados para ARM64. O produto final não deve recorrer a interpretadores do EE/VU1/IOP como caminho de execução.

Bibliotecas genéricas Android existentes no launcher podem continuar. Para novos componentes, priorizar APIs do sistema e adicionar dependências externas somente quando necessárias e documentadas.

## Estrutura lógica pretendida

```text
android/       Aplicativo, GameActivity, permissões e interface
native/        Runtime C++ e serviços PS2
renderer/      Frontend GS e backend Vulkan Android
recompiler/    Ferramentas de análise e recompilação estática
port/          Configuração e overrides específicos de God of War
tools/         Validação, diagnóstico e automação
docs/          Arquitetura, estado, testes, roadmap e créditos
third_party/   Somente dependências externas aprovadas e justificadas, se necessárias
```

Os diretórios de runtime/recompilação serão criados quando houver implementação real, testes e documentação. Não criaremos módulos vazios.


## Pipeline executável de geração host

O script tools/build_recompiler_host.py prepara o PS2Recomp na revisão fixada pelo KIexster, aplica o patch-base do runtime e compila somente a ferramenta host ps2_recomp. O ELF e os arquivos C++ produzidos permanecem em uma pasta privada fora do repositório. O Android NDK será usado em uma etapa separada para compilar o código gerado e o runtime adaptado.

O procedimento, dependências, versões fixadas e a triagem dos patches estão documentados em [PIPELINE_RECOMPILADOR.md](PIPELINE_RECOMPILADOR.md). A seleção lógica dos módulos do runtime está em [BASE_KIEXTER.md](BASE_KIEXTER.md). A preparação do script ainda não equivale a uma compilação verificada.
