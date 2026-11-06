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

## Capturas VU1/IOP sem exportação pelo usuário

A primeira build com o ELF real é um bootstrap AOT estrito: ela contém o código EE gerado, mas ainda não presume cobertura VU1/IOP. Instale essa build Debug no Android e execute **FORGE → Verificar runtime**. Os dados carregados são guardados em `files/gow-aot-captures/`, no armazenamento privado do app. O runtime continua sem fallback interpretado e para quando chega a código que ainda não possui implementação nativa.

Quando o aparelho estiver conectado por USB, com Depuração USB autorizada e ADB disponível no computador, execute novamente o mesmo comando `--iso`. O orquestrador procura automaticamente o dispositivo e tenta ler o diretório privado com `run-as`; converte o fluxo diretamente em um ZIP temporário no workspace privado, valida as entradas e gera o C++ AOT de VU1/IOP. O usuário não abre seletor, não cria ZIP e não escolhe arquivos derivados.

Se não houver aparelho autorizado ou ainda não existirem capturas, o comando não falha: gera a build bootstrap e informa por que a sincronização foi ignorada. Depois de cada nova tentativa no Android, o mesmo comando pode ser repetido para coletar novas capturas privadas.

O pacote Android de Debug precisa continuar instalado e ser depurável para que `run-as` possa ler seus próprios arquivos privados. O ADB não remove nem desinstala o aplicativo. Se uma instalação existente tiver assinatura diferente, não a desinstale como tentativa automática: isso apagaria a instalação privada. Resolva a assinatura/atualização primeiro.

## Requisitos de build host

A recompilação estática atual usa ferramentas nativas de desenvolvimento e é executada num host Windows x64 ou Linux x86-64: Python 3.11, Git, CMake, Ninja, compilador C++20, Java 17, Android SDK/NDK e `glslangValidator`. O host é apenas ferramenta para gerar/compilar o código nativo; o executável entregue e a execução do jogo continuam sendo Android ARM64.

A geração inicial pode consumir bastante memória e espaço. O número de jobs padrão é conservador. O computador não faz parte do produto e não executa o jogo.

## Limites honestos

- O sucesso do build ou a geração de C++ AOT não provam que o jogo iniciou.
- `full_native_aot=true` significa que o material VU1/IOP presente nas capturas foi gerado sem lacunas conhecidas; não garante todos os microprogramas ou módulos que o jogo carregará em fases futuras.
- O primeiro marco de conclusão é a primeira imagem real de God of War apresentada no Android. Menus, gameplay, áudio, FMVs, saves e compatibilidade de cenas exigem validação adicional.
- Nenhuma ISO, ELF, IRX, PAK, microcódigo, captura AOT, log ou C++ derivado será enviada ao GitHub, Actions, Issues ou Releases.
