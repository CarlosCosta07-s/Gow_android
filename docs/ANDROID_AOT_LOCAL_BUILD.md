# Fluxo AOT legado — substituído pelo fluxo ISO-only

**Este documento foi substituído.** Não siga os passos antigos de exportar `SCUS_973.99`, criar `gow-aot-captures.zip` ou exportar logs pelo FORGE.

O fluxo atual recebe somente a ISO original e extrai os intermediários automaticamente:

- [Fluxo Android com somente a ISO original](ANDROID_ISO_ONLY_BUILD.md)
- [Estado verificado do projeto](STATUS.md)

Comando atual (Windows):

```powershell
python tools/build_android_game.py --iso "D:\GowPrivate\God of War.iso" --work-dir "C:\gowport"
```

Linux:

```bash
python3 tools/build_android_game.py \
  --iso "$HOME/gow-private/God of War.iso" \
  --work-dir "/tmp/gowport"
```

O ELF, os módulos IRX, os microprogramas VU1, as capturas, os arquivos C++ gerados e o log de runtime permanecem em armazenamento privado e são tratados automaticamente. Com ADB autorizado, as capturas são sincronizadas diretamente do armazenamento privado do Android durante a próxima execução do mesmo comando.

**Importante:** builds Debug de assinaturas diferentes não podem atualizar umas às outras. Não desinstale uma instalação que contenha dados privados apenas para contornar um erro de assinatura; a desinstalação apaga os dados locais do aplicativo.
