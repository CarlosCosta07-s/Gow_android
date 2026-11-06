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

## 3. Integração pendente com o runtime Waffle

A base Waffle usa `PS2Runtime::IoPaths` para definir `hostRoot`, `cdRoot` e `mcRoot`. O `PS2IopHostAdapter` resolve caminhos de disco para caminhos do host e armazena `FILE*` em sua tabela de arquivos; `PS2Vfs::open` também abre arquivos com `fopen`. Isso não consegue consumir diretamente uma URI SAF nem um leitor ISO baseado em `pread`.

A ponte de runtime ainda precisa:
1. manter o FD da imagem válido enquanto a sessão estiver ativa e transferir uma cópia duplicada de forma explícita para o código nativo GameActivity;
2. expor operações nativas de abrir arquivo por caminho ISO, consultar tamanho, ler em offset e fechar handle;
3. adaptar o `PS2IopHostAdapter` para usar handles de disco Android quando `cdRoot` for o dispositivo ISO, mantendo `FILE*` para caminhos normais de host e cartão de memória;
4. preservar o `cdRoot` explicitamente configurado quando `loadELF()` prepara os caminhos de I/O;
5. garantir sincronização e liberação de handles em pausa, fim de sessão e troca de imagem;
6. validar os nomes e arquivos que God of War realmente solicita no boot, incluindo serviços CD/DVD e módulos necessários.

Essa estratégia evita materializar uma cópia completa do disco no armazenamento interno, mas exige integração do provider com a interface de arquivos do IOP. O leitor ISO está pronto para ser esse provider; o provider do runtime ainda não está conectado.

## 4. Estado

**[IMPLEMENTADO; CI PENDENTE]** leitor ISO9660 nativo por FD e fixture sintética, integrado ao alvo Android. A execução do teste host está no workflow `Android shell`.

**[NÃO IMPLEMENTADO]** transmissão do FD de MainActivity para GowGameActivity, provider de arquivos no `PS2IopHostAdapter`, configuração `cdRoot` do runtime e boot a partir da ISO. Nenhum dado de jogo foi adicionado ao repositório.
