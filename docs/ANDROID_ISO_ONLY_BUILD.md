# Fluxo Android com somente a ISO original

**Entrada de jogo:** uma única ISO original de God of War NTSC-U.  
**Produto final:** Android ARM64.  
**Dados intermediários:** sempre privados; nenhuma exportação manual de ELF, IRX, microcódigo, ZIP ou log.

## O que o usuário fornece

O usuário seleciona/importa somente a imagem original do disco. O FORGE identifica `SYSTEM.CNF`, valida o BOOT2 e instala os arquivos da ISO no armazenamento privado do Android, preservando o layout por setores (`.disc_layout` e `.disc_meta`). Não existe botão para exportar o ELF nem para exportar capturas/logs.

## Geração nativa a partir da ISO

O fluxo de desenvolvimento host recebe diretamente a ISO:

```powershell
python tools/build_android_game.py --iso "D:\GowPrivate\God of War.iso" --work-dir "C:\gowport"
```

Linux:

```bash
python3 tools/build_android_game.py \
  --iso "$HOME/gow-private/God of War.iso" \
  --work-dir "/tmp/gowport"
```

O comando executa automaticamente:

1. Prepara os commits fixados de KIexster/PS2Recomp e o runtime Waffle em um workspace privado.
2. Usa o `tools/iso_extract.py` da revisão fixada para extrair a ISO completa, incluindo `SYSTEM.CNF`, o executável indicado pelo BOOT2, módulos IRX, pacotes e metadados de setores.
3. Confere que `SYSTEM.CNF` seleciona o executável NTSC-U `SCUS_973.99`; não mistura o mapa NTSC-U com a revisão PAL.
4. Gera o C++ estático do EE, valida a tabela de funções e a cobertura do entry point.
5. Compila o APK Debug ARM64 com o NDK. ELF, IRX, pacotes, capturas, C++ gerado e workspace permanecem fora do Git e da CI.

O ELF nunca é selecionado ou exportado pelo usuário. Ele é um arquivo intermediário extraído automaticamente da ISO.

### Usar a instalação que já está no Android

Quando a ISO já foi instalada pelo FORGE no aparelho, não é necessário copiar os 7,94 GB de volta para o computador. Com o aplicativo Debug instalado, Depuração USB autorizada e ADB disponível no host, use:

```powershell
python tools/build_android_game.py --installed-android --work-dir "C:\gowport"
```

Linux:

```bash
python3 tools/build_android_game.py --installed-android --work-dir "$HOME/gowport"
```

Nesse modo, o host encontra o aparelho, lê `SYSTEM.CNF` e o executável BOOT2 diretamente do armazenamento privado de `org.gowandroid.app.debug` usando `run-as`, valida o perfil NTSC-U e gera o C++ EE. O mesmo dispositivo é usado para tentar sincronizar as capturas VU1/IOP. Somente `SYSTEM.CNF` e o ELF BOOT2 são copiados para o workspace privado do host; o restante do disco permanece no Android para o runtime usar durante o jogo. Não há seletor de arquivos, ZIP manual nem exportação pelo usuário.

O caminho pressupõe a pasta privada criada pela instalação NTSC-U do FORGE (`files/installations/gow_scus_973_99_ntsc_u/disc`). Se houver mais de um aparelho autorizado, acrescente `--device-serial SEU_SERIAL`. O comando não desinstala nem atualiza o aplicativo e não altera a instalação privada.

### Migração segura da assinatura Debug

O APK entregue pelo CI #412 foi assinado por uma chave temporária do runner. Um APK construído em outro computador não pode atualizá-lo diretamente; o Android rejeitaria a assinatura diferente. **Não desinstale o FORGE manualmente**, pois isso apagaria os 7,94 GB instalados.

Depois de gerar a build local conectada ao mesmo aparelho, a migração única é solicitada explicitamente assim:

```powershell
python tools/build_android_game.py --installed-android --migrate-installed-debug-signature --work-dir "C:\gowport"
```

Linux:

```bash
python3 tools/build_android_game.py --installed-android --migrate-installed-debug-signature --work-dir "$HOME/gowport"
```

Antes de remover o app antigo, o fluxo:
1. Confirma que `run-as` consegue acessar o pacote Debug e que o ADB aceita a restauração por fluxo.
2. Salva cópias do APK atualmente instalado para rollback.
3. Copia a instalação privada completa para o workspace host, confere a estrutura, o BOOT2 NTSC-U e **todos os hashes do manifesto SHA-256**.
4. Só depois desinstala o pacote com a assinatura temporária, instala o APK local e restaura a pasta privada.
5. Se a instalação/restauração falhar, tenta reinstalar o APK original e restaurar os dados anteriores automaticamente.

O backup verificado permanece em `<work-dir>/android-signature-migrations/`; ele não é enviado ao GitHub nem à CI. A operação transfere aproximadamente o tamanho da instalação (nesta cópia, cerca de 7,94 GB) para o computador e exige espaço livre equivalente no host. A cópia antiga é removida do aparelho somente após a validação do backup. **Mantenha o arquivo de backup até abrir o FORGE e confirmar novamente a integridade do disco.**

Depois da migração, preserve a chave Debug local do Gradle em `%USERPROFILE%\\.android\\debug.keystore` (Windows) ou `$HOME/.android/debug.keystore` (Linux). Ela permite que builds futuras feitas no mesmo computador atualizem o app sem repetir a migração. O FORGE agora recupera `SYSTEM.CNF` e BOOT2 da instalação privada quando a preferência do seletor de ISO não existe, portanto não exige selecionar novamente o arquivo original. Para as próximas compilações no mesmo host, use apenas `--installed-android` (sem `--migrate-installed-debug-signature`); a migração é necessária uma única vez, quando a assinatura instalada não corresponde à chave Debug local.

Este procedimento já possui testes sintéticos de backup, SHA-256, caminhos e BOOT2 na CI. A migração ADB completa ainda precisa ser executada num aparelho real antes de ser considerada comprovada.

## Capturas VU1/IOP sem exportação pelo usuário

A primeira build com o ELF real é um bootstrap AOT estrito: ela contém o código EE gerado, mas ainda não presume cobertura VU1/IOP. Instale essa build Debug no Android e execute **FORGE → Verificar runtime**. Os dados carregados são guardados em `files/gow-aot-captures/`, no armazenamento privado do app. O runtime continua sem fallback interpretado e para quando chega a código que ainda não possui implementação nativa.

Quando o aparelho estiver conectado por USB, com Depuração USB autorizada e ADB disponível no computador, execute novamente o mesmo comando `--installed-android`. O orquestrador procura automaticamente o dispositivo e tenta ler o diretório privado com `run-as`; converte o fluxo diretamente em um ZIP temporário no workspace privado, valida as entradas e gera o C++ AOT de VU1/IOP. O usuário não abre seletor, não cria ZIP e não escolhe arquivos derivados.

Se não houver aparelho autorizado ou ainda não existirem capturas, o comando não falha: gera a build bootstrap e informa por que a sincronização foi ignorada. Depois de cada nova tentativa no Android, o mesmo comando pode ser repetido para coletar novas capturas privadas.

O pacote Android de Debug precisa continuar instalado e ser depurável para que `run-as` possa ler seus próprios arquivos privados. O ADB não remove nem desinstala o aplicativo. Se uma instalação existente tiver assinatura diferente, não a desinstale como tentativa automática: isso apagaria a instalação privada. Resolva a assinatura/atualização primeiro.

## Requisitos de build host

A recompilação estática atual usa ferramentas nativas de desenvolvimento e é executada num host Windows x64 ou Linux x86-64: Python 3.11, Git, CMake, Ninja, compilador C++20, Java 17, **Gradle 9.6.0 disponível no PATH**, Android SDK/NDK e `glslangValidator`. O repositório não contém `gradlew`; o orquestrador executa Gradle 9.6 diretamente com `--project-dir android`, como no CI. O host é apenas ferramenta para gerar/compilar o código nativo; o executável entregue e a execução do jogo continuam sendo Android ARM64.

A geração inicial pode consumir bastante memória e espaço. O número de jobs padrão é conservador. O computador não faz parte do produto e não executa o jogo.

## Build candidato (somente após as capturas AOT)

A primeira execução de `tools/build_android_game.py --iso ...` pode gerar apenas um APK bootstrap para capturar VU1/IOP; esse APK é **diagnóstico interno, não jogável e não deve ser entregue para teste**. Depois que as capturas privadas forem sincronizadas e a geração AOT for completa, o mantenedor pode usar:

```bash
python tools/build_android_game.py --iso "/caminho/privado/God of War.iso" --work-dir "/workspace/privado/gowport" --require-full-native-aot
```

O parâmetro `--require-full-native-aot` falha antes de compilar o APK quando não existe arquivo privado de capturas ou quando o relatório não comprova seis partes VU1, todos os módulos IOP mais o registro, zero blocos VU1 sem suporte e zero callouts ao interpretador. O modo padrão continua permitindo builds bootstrap/iterativas para desenvolvimento, mas esses resultados não são candidatos de teste.

Mesmo um APK produzido com essa opção **não está automaticamente aprovado para download**. Ainda precisa passar pela execução da ISO real, primeiro frame, menu, controles, reinicializações e validação da assinatura descritos em [TEST_RELEASE_GATE.md](TEST_RELEASE_GATE.md).

## Limites honestos

- O sucesso do build ou a geração de C++ AOT não provam que o jogo iniciou.
- `full_native_aot=true` significa que o material VU1/IOP presente nas capturas foi gerado sem lacunas conhecidas; não garante todos os microprogramas ou módulos que o jogo carregará em fases futuras.
- O primeiro marco de conclusão é a primeira imagem real de God of War apresentada no Android. Menus, gameplay, áudio, FMVs, saves e compatibilidade de cenas exigem validação adicional.
- Nenhuma ISO, ELF, IRX, PAK, microcódigo, captura AOT, log ou C++ derivado será enviada ao GitHub, Actions, Issues ou Releases.
