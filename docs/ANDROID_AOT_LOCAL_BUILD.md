# Build local do AOT Android a partir do ELF do usuário

Este fluxo gera o C++ nativo do jogo na máquina do usuário e o compila dentro do APK ARM64. O ELF, os arquivos gerados e a área de trabalho do recompilador permanecem locais. **Não envie ISO, ELF, IRX, microcódigo, código C++ gerado ou saves para GitHub, CI, issues ou releases.**

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
  --work-dir "/tmp/gowport"
```

O script executa, em sequência:

1. Valida as regras de privacidade: ELF e workspace precisam ficar fora do repositório; o staging de código precisa estar sob `android/app/build/` e ser ignorado pelo Git.
2. Chama `tools/build_recompiler_host.py` para validar o ELF32/MIPS, compilar as ferramentas host, executar o PS2Recomp e exportar os arquivos C++/headers.
3. Confere a presença de `register_functions.cpp`, `ps2_recompiled_functions.h`, `ps2_recompiled_stubs.h` e das unidades C++ de funções.
4. Analisa a tabela gerada: rejeita zero slots, ausência de registros, endereços desalinhados/fora da faixa, slots incorretos ou duplicados e exige que o endereço de entrada do ELF esteja registrado. Esse é o mesmo requisito que a porta Android verifica antes de aceitar a carga.
5. Só então executa `gradlew assembleDebug`. O CMake detecta `android/app/build/gow-generated-game/` automaticamente e substitui a tabela vazia do shell pelo código AOT gerado.
6. Exibe o caminho e o SHA-256 do APK resultante.

## Saídas locais

- Código AOT: `android/app/build/gow-generated-game/` (ignorado pelo Git).
- Workspace do host: `C:/gowport` ou `/tmp/gowport` (fora do repositório).
- APK: `android/app/build/outputs/apk/debug/app-debug.apk`.

Opções úteis:

- `--plan-only` valida caminhos e imprime os comandos, sem executá-los.
- `--generate-only` gera e prepara o C++ AOT, mas não chama Gradle.

## O que esse APK permite verificar — e o que ainda não permite

O orquestrador agora faz uma pré-validação estática da tabela gerada antes do Gradle; fixtures de CI cobrem tabela válida, tabela vazia, ausência do entrypoint e mapeamento incorreto de slot. Um build AOT local validado poderá então testar se o código foi ligado e se o BOOT2 privado é carregado. Isso ainda não foi executado com o ELF do usuário nem em aparelho.

**Ainda não é gameplay.** O build Android atual mantém `PS2Runtime::run()` bloqueado. O backend GS/Vulkan Android, presenter do GS, cadeia real de boot (incluindo IRX/VU1 AOT e serviços PS2) e áudio Android ainda precisam ser implementados e validados. Um resultado positivo na porta ELF/AOT não significa que o menu do jogo ou uma cena já possa ser executada.

O fluxo local não envia o ELF nem o C++ gerado a nenhum serviço. O usuário deve manter a ISO original e os dados privados correspondentes à mesma revisão do executável.
