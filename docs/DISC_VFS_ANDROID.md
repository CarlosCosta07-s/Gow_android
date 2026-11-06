# Leitura de disco e VFS no Android

**Revisão:** 2026-11-06  
**Base runtime:** Waffle/KIexter `49ca62c1d20b4659c61df6286c54e86edf411012`

## 1. Componente de leitura ISO9660 preparado

O módulo `android/app/src/main/cpp/iso9660_reader.h/.cpp` oferece um leitor de acesso aleatório para imagens ISO9660 com setores de 2048 bytes.

- Recebe um descritor já aberto pelo Storage Access Framework e duplica o FD; o proprietário Java continua responsável pelo descritor original.
- Verifica o descritor primário ISO9660 no setor 16 e o registro do diretório raiz.
- Percorre diretórios por extent e tamanho, sem extrair nem copiar o disco inteiro.
- Resolve nomes sem diferenciar maiúsculas/minúsculas e aceita sufixos de versão como `;1`.
- Lê intervalos arbitrários de arquivos por offset, inclusive leituras que atravessam limites de setor.
- Verifica limites de imagem, tamanho de diretório, tamanho de registro e componentes de caminho; rejeita `..`, caminhos de diretório onde se esperava um arquivo e extents fora da imagem.

O componente é independente da Activity e do runtime, permitindo testes host com uma ISO sintética sem dados de jogo. O workflow Android executa os testes junto com os testes do pacote DualShock 2.

## 2. Limitações declaradas

Esta primeira versão não aceita:
- imagens raw de CD com setores de 2352 bytes;
- CHD, CSO ou formatos compactados;
- diretórios/nomes Joliet ou Rock Ridge;
- arquivos ISO9660 multi-extent.

O seletor SAF atual reconhece PVD ISO9660 de 2048 bytes e lê `SYSTEM.CNF`; o leitor nativo estende essa fundação com lookup em diretórios e leitura de arquivo por offset. A aceitação de uma imagem pelo parser ainda não significa que o runtime saiba montá-la.

## 3. Decisão de arquitetura: instalação híbrida, inspirada no Dragon Rage

A referência analisada é o projeto [Dragon Rage / Tenkaichi3DecompAndroid](https://github.com/dfdx047/Tenkaichi3DecompAndroid). Ele possui dois fluxos úteis:

- **BT3 — instalação por extração seletiva:** o instalador lê a ISO por SAF e copia o executável e diretórios de dados selecionados (`BIN/`, `DATA/`). Arquivos AFS são mantidos inteiros, sem dividir suas entradas em milhares de arquivos pequenos.
- **BT4 — instalação híbrida com imagem mantida:** o instalador valida o ELF, copia o executável e arquivos menores exigidos pelo runtime, guarda a URI com permissão persistente e mantém os grandes arquivos de dados na imagem. Ao iniciar, a Activity abre um `ParcelFileDescriptor` e entrega ao motor nativo `/proc/self/fd/<fd>`.

Para God of War, adotaremos a **estratégia híbrida do BT4**, adaptada aos serviços de CD/DVD do PS2. Não vamos copiar a ISO de vários gigabytes nem presumir que a estrutura de dados do BT3 se aplica ao jogo.

### Fluxo planejado para God of War

1. O launcher usa SAF `ACTION_OPEN_DOCUMENT`, solicita permissão persistente de leitura e valida se o provider permite acesso aleatório ao descritor.
2. O importador verifica PVD ISO9660, `SYSTEM.CNF`, serial/região e identidade/revisão do ELF. O cache registra URI, tamanho, identidade e versão do importador.
3. O ELF original é copiado para o armazenamento privado com o nome esperado pelo runtime. Isso é necessário porque `PS2Runtime::loadELF()` atualmente recebe um caminho e abre o arquivo com `std::ifstream`.
4. Arquivos pequenos de boot/IRX/BIN só serão copiados depois de uma lista de dependências confirmada por rastreamento do boot. O instalador verifica tamanho/hash do que copiou e publica o marcador de instalação apenas ao final.
5. A imagem permanece na origem SAF. Ao iniciar `GowGameActivity`, o Java abre `ParcelFileDescriptor`; a camada nativa recebe um descritor duplicado/transferido com ownership explícito e o mantém até encerrar a sessão.
6. O provider ISO nativo mapeia caminhos `cdrom0:\\...` para entradas ISO9660 e implementa abrir, consultar tamanho, ler em offset e fechar handle para o IOP. O leitor usa acesso aleatório e poderá adicionar cache limitado de blocos; não extrai toda a árvore.
7. Se a permissão persistente tiver sido revogada ou a imagem estiver indisponível, o launcher solicita que o usuário selecione novamente a mesma imagem e revalida a identidade antes de iniciar.

**Não copiar por suposição:** a lista de diretórios/arquivos a extrair será derivada dos acessos reais durante o boot de God of War. Primeiro manteremos os dados grandes na imagem; somente arquivos que o runtime precise como caminho físico local serão colocados no cache privado.

## 4. Integração implementada no runtime Waffle

A revisão atual prepara um provider ISO nativo e o injeta na cópia privada do runtime Waffle durante `--prepare-android-runtime`.

- `android_disc_bridge.cpp` mantém uma instância de `Iso9660Reader` e uma tabela de handles, com operações C ABI `gowAndroidDiscOpenFile`, `gowAndroidDiscReadFile`, `gowAndroidDiscCloseFile` e `gowAndroidDiscIsReady`.
- A entrada JNI `GowGameActivity.nativeSetDiscFileDescriptor(int)` duplica o FD recebido. A Activity espera o extra `EXTRA_DISC_FD`; o launcher ainda não inicia essa Activity nem envia o descritor.
- O patch privado estende `PS2IopHostAdapter::HostFile` com handle ISO e roteia open/size/read/close de caminhos `gowdisc:/` para o provider, mantendo `FILE*` para caminhos comuns de host.
- O patch privado implementa `AndroidDiscOpenFile` em `PS2Vfs`, com read/seek de arquivos ISO somente leitura e uma consulta de tamanho para arquivos. Listagem de diretórios/metadata avançada ainda não está implementada.
- `PS2Runtime::configureIoPathsFromElf` seleciona `gowdisc:/` como `cdRoot` quando o provider já possui uma imagem aberta.
- As alterações privadas são aplicadas a partir do snapshot Waffle fixado; não são armazenadas como fontes de terceiros no repositório público.

### Evidência de compilação

- Android [run 42161298942](https://github.com/CarlosCosta07-s/Gow_android/actions/runs/42161298942), commit `836da9a80a19d04b8b934da0481655ea167bb8e3`: passou nos testes host DualShock 2/ISO, preparou o runtime privado, compilou `assembleDebug` e publicou o APK ARM64.
- Host [run 42161298950](https://github.com/CarlosCosta07-s/Gow_android/actions/runs/42161298950), mesmo commit: passou em `py_compile`, CLI e compilação do `ps2_recomp` + runtime Waffle estrito. O log confirma `ps2_iop` e `ps2_runtime` construídos sem ELF/ISO do jogo.

**O que isso prova:** os patches privados foram aplicados e o target Android que contém o provider compilou. **O que não prova:** que o FD já está sendo enviado pelo FORGE, que uma ISO real foi aberta, que God of War solicitou arquivos via provider ou que o boot ocorreu.

### Próximos passos de integração

1. Ao liberar o início do jogo, abrir/duplicar o FD da imagem selecionada e enviá-lo no extra `GowGameActivity.EXTRA_DISC_FD`; depois fechar a cópia temporária Java sem invalidar o descritor que o provider duplicou.
2. Implementar enumeração/stat de diretórios se o rastreamento de boot demonstrar que os serviços CD/DVD a exigem.
3. Adicionar cache de blocos limitado e sincronização de handles; atualmente o provider serializa operações por mutex.
4. Verificar, com imagem do usuário, quais caminhos/IRX/BIN são requisitados durante o boot.
5. Criar o acesso ao ELF privado: `PS2Runtime::loadELF()` ainda usa caminho local/`std::ifstream`, então o ELF precisa de cache privado antes da sessão.
6. Não chamar `PS2Runtime::initialize()` até GS/Vulkan, apresentação e áudio Android substituírem os adaptadores desktop.

## 5. Estado

**[IMPLEMENTADO; TESTE HOST APROVADO]** leitor ISO9660 nativo por FD e fixture sintética, integrado ao alvo Android. Os testes host passaram na etapa de testes do workflow `Android shell`; o APK completo do commit mais recente ainda precisa de sua própria confirmação CI.

**[NÃO IMPLEMENTADO]** persistência da URI SAF no Gow_android, transmissão do FD de MainActivity para GowGameActivity, cache seletivo do ELF/arquivos pequenos, provider de arquivos no `PS2IopHostAdapter`, configuração `cdRoot` do runtime e boot a partir da ISO. Nenhum dado de jogo foi adicionado ao repositório.

## 5. Estado

**[COMPILADO NA CI]** leitor ISO9660, provider C ABI, ponte JNI e patches privados do `PS2IopHostAdapter`/`PS2Vfs` foram incluídos e compilados no Android run `42161298942`; o runtime/recompiler host foi aprovado no run `42161298950`. O workflow Android também executou fixtures sintéticas sem dados do jogo.

**[NÃO TESTADO]** a transferência real do FD a partir do launcher, leitura de uma ISO real, enumeração completa do dispositivo CD/DVD, cache seletivo do ELF/arquivos de boot, requisições reais do IOP e boot do jogo. O `EXTRA_DISC_FD` em `GowGameActivity` está preparado, mas MainActivity não inicia a Activity nesta revisão. Nenhum dado de jogo foi adicionado ao repositório.

## Referências de implementação do Dragon Rage

- [IsoImage.kt](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/android/app/src/main/java/com/dfdx047/dragonrage/data/IsoImage.kt): PVD, árvore ISO9660 e leitura aleatória por `FileChannel`.
- [GameDataInstaller.kt](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/android/app/src/main/java/com/dfdx047/dragonrage/data/GameDataInstaller.kt): verificação e extração seletiva do BT3, progresso, espaço livre e verificação dos arquivos copiados.
- [Bt4Installer.kt](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/android/app/src/main/java/com/dfdx047/dragonrage/data/Bt4Installer.kt): permissão persistente, extração dos arquivos pequenos e preservação da ISO.
- [Bt4Activity.kt](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/android/app/src/main/java/com/dfdx047/dragonrage/engine/Bt4Activity.kt): abertura do descritor e passagem de `/proc/self/fd/<fd>` ao motor.