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

## 4. Integração pendente com o runtime Waffle

A base Waffle usa `PS2Runtime::IoPaths` para definir `hostRoot`, `cdRoot` e `mcRoot`. O `PS2IopHostAdapter` resolve caminhos de disco para caminhos do host e armazena `FILE*` em sua tabela de arquivos; `PS2Vfs::open` também abre arquivos com `fopen`. Isso não consegue consumir diretamente uma URI SAF nem um leitor ISO baseado em `pread`.

A ponte de runtime ainda precisa:
1. manter o FD da imagem válido enquanto a sessão estiver ativa e transferir uma cópia duplicada de forma explícita para o código nativo GameActivity;
2. expor operações nativas de abrir arquivo por caminho ISO, consultar tamanho, ler em offset e fechar handle;
3. adaptar o `PS2IopHostAdapter` para usar handles de disco Android quando `cdRoot` for o dispositivo ISO, mantendo `FILE*` para caminhos normais de host e cartão de memória;
4. preservar o `cdRoot` explicitamente configurado quando `loadELF()` prepara os caminhos de I/O;
5. garantir sincronização e liberação de handles em pausa, fim de sessão e troca de imagem;
6. validar os nomes e arquivos que God of War realmente solicita no boot, incluindo serviços CD/DVD e módulos necessários.

Essa estratégia evita materializar uma cópia completa do disco no armazenamento interno, mas exige integração do provider com a interface de arquivos do IOP. O leitor ISO está pronto para ser esse provider; o provider do runtime ainda não está conectado.

## 5. Estado

**[IMPLEMENTADO; TESTE HOST APROVADO]** leitor ISO9660 nativo por FD e fixture sintética, integrado ao alvo Android. Os testes host passaram na etapa de testes do workflow `Android shell`; o APK completo do commit mais recente ainda precisa de sua própria confirmação CI.

**[NÃO IMPLEMENTADO]** persistência da URI SAF no Gow_android, transmissão do FD de MainActivity para GowGameActivity, cache seletivo do ELF/arquivos pequenos, provider de arquivos no `PS2IopHostAdapter`, configuração `cdRoot` do runtime e boot a partir da ISO. Nenhum dado de jogo foi adicionado ao repositório.

## Referências de implementação do Dragon Rage

- [IsoImage.kt](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/android/app/src/main/java/com/dfdx047/dragonrage/data/IsoImage.kt): PVD, árvore ISO9660 e leitura aleatória por `FileChannel`.
- [GameDataInstaller.kt](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/android/app/src/main/java/com/dfdx047/dragonrage/data/GameDataInstaller.kt): verificação e extração seletiva do BT3, progresso, espaço livre e verificação dos arquivos copiados.
- [Bt4Installer.kt](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/android/app/src/main/java/com/dfdx047/dragonrage/data/Bt4Installer.kt): permissão persistente, extração dos arquivos pequenos e preservação da ISO.
- [Bt4Activity.kt](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/android/app/src/main/java/com/dfdx047/dragonrage/engine/Bt4Activity.kt): abertura do descritor e passagem de `/proc/self/fd/<fd>` ao motor.