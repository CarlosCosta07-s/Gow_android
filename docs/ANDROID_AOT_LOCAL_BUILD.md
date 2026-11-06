# Build local do AOT Android a partir do ELF do usuário

Este fluxo gera o C++ nativo do jogo na máquina do usuário e o compila dentro do APK ARM64. O ELF, os arquivos gerados e a área de trabalho do recompilador permanecem locais. **Não envie ISO, ELF, IRX, microcódigo, código C++ gerado ou saves para GitHub, CI, issues ou releases.**

## Assinatura dos APKs Debug e atualizações

O Android só permite atualizar um aplicativo existente quando o certificado de assinatura permanece o mesmo. Os APKs das CI #339, #344, #347, #348 e #349 foram assinados por chaves Debug efêmeras diferentes (os certificados SHA-256 foram inspecionados diretamente nos APKs). Portanto, **não instale um APK de outra execução por cima de um FORGE Debug já instalado**. Não desinstale o aplicativo apenas para contornar o conflito: o Android apaga o armazenamento privado do app, incluindo a instalação do disco.

O Gradle agora aceita uma chave Debug persistente por meio de quatro secrets do GitHub Actions. Configure-os em *Settings → Secrets and variables → Actions*:

- `GOW_ANDROID_DEBUG_KEYSTORE_BASE64`: conteúdo do keystore em Base64.
- `GOW_ANDROID_DEBUG_KEYSTORE_PASSWORD`: senha do keystore.
- `GOW_ANDROID_DEBUG_KEY_ALIAS`: alias da chave.
- `GOW_ANDROID_DEBUG_KEY_PASSWORD`: senha da chave.

Gere uma chave privada sua em um computador e guarde uma cópia segura fora do repositório. Exemplo (PowerShell; use senhas fortes):

```powershell
keytool -genkeypair -v -keystore gow-android-debug.p12 -storetype PKCS12 -alias forge-debug -keyalg RSA -keysize 3072 -validity 10000 -dname "CN=FORGE Android Debug"
[Convert]::ToBase64String([IO.File]::ReadAllBytes("gow-android-debug.p12")) | Set-Clipboard
```

Cole o texto Base64 no secret `GOW_ANDROID_DEBUG_KEYSTORE_BASE64` e cadastre as outras três informações nos secrets correspondentes. Não cole a chave, o Base64 ou as senhas em issues, commits, mensagens de log ou arquivos versionados. Sem esses secrets, a CI ainda compila o APK, mas usa a chave efêmera padrão do runner e o artefato não é apropriado para atualizar uma instalação de outra execução.

**Atenção:** adicionar a chave persistente resolve as atualizações futuras entre builds assinados por ela; não muda retroativamente a assinatura dos APKs antigos. Se um desses APKs efêmeros já estiver instalado com os dados do disco, preserve-o até existir um procedimento de migração/backup da instalação privada.

## Perfil atualmente suportado

O mapa e a configuração em `recompiler/config/` estão fixados na revisão NTSC-U **SCUS_973.99**. Não use o executável PAL **SCES_531.33** com esse mapa: ele precisa de um mapa de funções e uma configuração próprios antes de ser compilado.

O arquivo deve ser o ELF `SCUS_973.99` extraído da sua própria imagem. Não use arquivos baixados de terceiros.

## Requisitos na máquina de build

- Windows x64 ou Linux x86-64.
- Python 3.11.
- Git e CMake 3.21 ou superior.
- Compilador C/C++ compatível com C++20 e Ninja (recomendado).
- Android SDK/NDK instalados e configurados para o Gradle do projeto.
- Java 17 (o projeto usa o Gradle Wrapper).

## Controle de memória durante a compilação

O padrão do fluxo local é conservador: o CMake usa **1 job** para compilar o runtime e o C++ AOT, e o Gradle usa um worker. Isso reduz picos de memória em computadores modestos, embora possa aumentar o tempo total do build.

O número de jobs do recompilador host pode ser alterado pela variável de ambiente `GOW_BUILD_JOBS` (aceita de 1 a 16). Para a primeira compilação, mantenha 1:

```powershell
$env:GOW_BUILD_JOBS = "1"
```

No Linux:

```bash
GOW_BUILD_JOBS=1 python3 tools/build_android_game.py --elf "$HOME/gow-private/SCUS_973.99" --capture-bootstrap --work-dir "/tmp/gowport"
```

Não aumente o paralelismo antes de confirmar a memória disponível durante a compilação das unidades AOT.

## Windows (PowerShell)

Copie o ELF da sua própria mídia para uma pasta privada fora do repositório. Exemplo:

```powershell
python tools/build_android_game.py `
  --elf "D:\GowPrivate\SCUS_973.99" `
  --work-dir "C:\gowport"
```

## Linux

```bash
python3 tools/build_android_game.py \
  --elf "$HOME/gow-private/SCUS_973.99" \
  --capture-bootstrap \
  --work-dir "/tmp/gowport"
```

O orquestrador executa, em sequência:

1. Valida as regras de privacidade e prepara a revisão fixada do runtime Waffle numa pasta local privada.
2. Valida o ELF32/MIPS, compila as ferramentas host, executa PS2Recomp e exporta os arquivos C++/headers do EE.
3. Confere a presença de `register_functions.cpp`, `ps2_recompiled_functions.h`, `ps2_recompiled_stubs.h`, slots consistentes e uma função registrada no endereço de entrada.
4. No modo de captura, remove qualquer staging VU1/IOP antigo e compila um APK explicitamente diagnóstico. O Android continua com EE, VU1 e IOP em modo nativo estrito: um bloco/módulo ausente encerra a execução com diagnóstico, nunca com fallback interpretado.
5. No modo com capturas, valida o ZIP, cada micro-memória VU1 (16 KiB), o manifesto IOP, tamanhos e hashes FNV-1a; gera C++ VU1/IOP; liga somente blocos VU1 nativos e registra a cobertura restante. Capturas parciais geram um APK de próxima iteração; cobertura integral do material capturado permite avançar, mas não prova que todos os microprogramas futuros do jogo já foram observados.
6. Só então executa `gradlew assembleDebug`. O CMake inclui o C++ privado do EE, os blocos VU1 e os módulos IRX/IOP quando existem; dados e código gerado permanecem em `android/app/build/`.
7. Exibe o caminho e o SHA-256 do APK resultante.


## Ciclo incremental de captura VU1/IOP

O primeiro APK com código EE deve ser gerado em **modo de captura estrita**, não como se já fosse uma build de gameplay completa:

```powershell
python tools/build_android_game.py `
  --elf "D:\\GowPrivate\\SCUS_973.99" `
  --capture-bootstrap `
  --work-dir "C:\\gowport"
```

No Linux:

```bash
python3 tools/build_android_game.py \
  --elf "$HOME/gow-private/SCUS_973.99" \
  --capture-bootstrap \
  --work-dir "/tmp/gowport"
```

Instale esse APK no aparelho que contém a instalação privada, abra **FORGE → Verificar runtime** e deixe a tentativa de boot prosseguir até atingir um primeiro módulo ou bloco VU1 sem AOT. O runtime deve parar com diagnóstico. Enquanto inicializa os subsistemas, ele guarda em `files/gow-aot-captures/vu1/` as micro-memórias de 16 KiB e em `files/gow-aot-captures/iop/` os módulos IRX relocados, junto com `modules.txt` (base, tamanho e hash FNV-1a).

Na tela Debug, toque em **Exportar dados AOT** e salve `gow-aot-captures.zip` num local acessível ao computador de build. O ZIP é dado derivado do disco do usuário: mantenha-o privado e não o envie para GitHub, CI, issues ou releases.

Recompile no computador, incorporando as capturas:

```powershell
python tools/build_android_game.py `
  --elf "D:\\GowPrivate\\SCUS_973.99" `
  --aot-captures "D:\\GowPrivate\\gow-aot-captures.zip" `
  --work-dir "C:\\gowport"
```

Linux:

```bash
python3 tools/build_android_game.py \
  --elf "$HOME/gow-private/SCUS_973.99" \
  --aot-captures "$HOME/gow-private/gow-aot-captures.zip" \
  --work-dir "/tmp/gowport"
```

Repita o ciclo com o novo APK quando ele parar num módulo/microprograma ainda não capturado. O gerador VU1 em modo `--strict-partial` **omite** blocos que exigiriam callouts ao motor exato; o runtime nunca os interpreta. Uma captura VU1 nova pode acrescentar blocos nativos; um IRX novo acrescenta uma unidade nativa ao registro. O arquivo `native-aot-summary.json` relata a cobertura vista. `full_native_aot=true` significa que os dados VU1/IOP presentes no ZIP foram gerados sem lacunas conhecidas naquele material; **não** comprova cobertura de todos os microprogramas ou módulos que o jogo poderá carregar mais adiante.

## Saídas locais

- Código AOT: `android/app/build/gow-generated-game/` (ignorado pelo Git).
- Workspace do host: `C:/gowport` ou `/tmp/gowport` (fora do repositório).
- APK: `android/app/build/outputs/apk/debug/app-debug.apk`.

Opções úteis:

- `--plan-only` valida caminhos e imprime os comandos, sem executá-los.
- `--generate-only` gera e prepara o C++ AOT, mas não chama Gradle.

## O que esse APK permite verificar — e o que ainda não permite

O orquestrador agora faz uma pré-validação estática da tabela gerada antes do Gradle; fixtures de CI cobrem tabela válida, tabela vazia, ausência do entrypoint e mapeamento incorreto de slot. Um build AOT local validado poderá então testar se o código foi ligado e se o BOOT2 privado é carregado. Isso ainda não foi executado com o ELF do usuário nem em aparelho.

**Ainda não é gameplay comprovado.** O `android_main()` agora possui uma tentativa de boot AOT Android condicionada a quatro requisitos: tabela AOT não vazia, entry point do ELF registrado, backend GS Android anexado e surface/presenter Vulkan prontos. O worker dedicado chama `startGuestExecutionAndroid()` e encerra com `join()`; o caminho desktop `PS2Runtime::run()` continua bloqueado no perfil Android. A primeira tentativa é **silenciosa**: ainda não existe saída SPU2/AAudio Android. O IOP mantém sua fila de áudio limitada, mas nenhum som é reproduzido.

Esse caminho permite investigar o primeiro estágio do guest, mas não comprova que os serviços de boot estão completos. A geração real do ELF AOT, cobertura VU1 e IRX AOT, chamadas CDVD/SIF/DMA usadas pelo jogo e validação no aparelho continuam pendentes. Se o worker iniciar e parar em uma instrução/módulo não suportado, o diagnóstico deve registrar o último PC e o subsistema; não converter isso em sucesso. O shell público ainda usa a tabela AOT vazia, portanto recusa a execução.

O fluxo local não envia o ELF nem o C++ gerado a nenhum serviço. O usuário deve manter a ISO original e os dados privados correspondentes à mesma revisão do executável.
