# Auditoria da origem de disco PS2 — Android

**Revisão:** 2026-11-06  
**Projeto:** Gow_android  
**Fontes de engenharia:** KIexster/God of War Recomp `a226151feb6ed22937e42be485aaf28a83acd166` e runtime Waffle/PS2Recomp incorporado por esse projeto.

## 1. Conclusão

O acesso aos dados de God of War exige duas interfaces distintas:

1. **VFS por caminho:** atende operações como `cdrom0:/...`, abertura, leitura, seek e metadados.
2. **Fonte de setores do disco:** atende chamadas CD/DVD por LSN, inclusive as leituras de streaming feitas pelo IOP.

O provider Android `CdRootVfs` cobre a primeira interface. Ele não substitui a segunda. O runtime de referência possui `ps2x::iop::DiscSource`, cujo contrato expõe `readSectors(lsn, count, destination)`, `findFile(path)` e `layer1Start()`.

Portanto, montar apenas arquivos soltos no VFS não é suficiente para reproduzir as leituras de disco que o jogo faz.

## 2. O que o port para PC já documenta

No commit fixado de KIexster, `tools/iso_extract.py` implementa a extração de uma imagem ISO9660 com suporte a DVD de dupla camada. O fluxo é:

- Ler o descritor de volume primário da camada 0 no setor 16.
- Obter o tamanho da primeira camada a partir do descritor.
- Verificar se há outro descritor no início físico da camada 1.
- Quando a segunda camada existe, calcular sua base lógica como `layer0Sectors - 16`.
- Percorrer as árvores de diretórios de ambas as camadas.
- Extrair os arquivos mantendo seus caminhos e LSNs originais.

Com `--layout`, o extrator produz dois arquivos auxiliares na raiz da instalação.

### `.disc_layout`

É um manifesto textual com este formato:

```text
sectors <quantidade-total-de-setores>
layer1 <primeiro-LSN-da-camada-1-ou-zero>
file <LSN> <tamanho-em-bytes> <caminho-relativo>
```

Cada arquivo extraído continua sendo um arquivo comum no armazenamento privado, mas o manifesto preserva sua posição original no disco.

### `.disc_meta`

É uma sequência binária de registros fixos:

- 4 bytes: LSN little-endian.
- 2048 bytes: conteúdo original daquele setor.

O extrator guarda os setores não vazios que não pertencem aos dados de arquivos, incluindo descritores de volume, estruturas UDF, diretórios e tabelas de caminhos. Assim, a implementação `DiscSource::openExtracted()` pode reconstruir leituras por LSN usando arquivos e metadados locais.

A implementação do runtime espera exatamente esse layout. Não devemos inventar outro formato sem adaptar e testar também o consumidor.

## 3. Por que isso importa para God of War

A documentação de estado do port de PC registra que `SMPD_IOP.IRX` é o streamer de dados do jogo. Ele lê `GODOFWAR.TOC` e carrega WADs/pacotes consultando setores com `sceCdRead`. O TOC contém entradas que relacionam nomes e dados a setores do disco. O acesso a `PART1.PAK` e `PART2.PAK` depende dessas posições.

O mesmo projeto também registra que os módulos IRX da raiz precisam estar disponíveis no diretório `IOP_MOD/`, pois o jogo solicita os módulos por caminhos relativos como `IOP_MOD/xxx.irx`.

Isso gera requisitos que não podem ser resolvidos apenas com `PS2Vfs::open()`:

- Preservar os LSNs originais dos arquivos e dos setores de metadados.
- Tratar as duas camadas de um DVD de dupla camada.
- Dar ao CDVD/IOP uma fonte de leitura por setor.
- Manter os arquivos de dados grandes íntegros, sem expandir seus formatos internos.
- Disponibilizar os IRX no layout que o código do jogo solicita.

As referências de PC são documentação de engenharia e contratos de runtime. Os números de LSN e a lista de arquivos devem ser gerados da cópia que o usuário selecionou, não codificados a partir de outra imagem.

## 4. Auditoria do Android antes desta revisão

A implementação Android já tinha:

- Leitor ISO9660 de setor 2048, com PVD no setor 16.
- Leitura aleatória e enumeração de diretórios.
- Extração seletiva para armazenamento privado com staging/cancelamento.
- Provider seguro `CdRootVfs`, com `openat`, `O_NOFOLLOW`, proteção contra traversal e leitura por offset.
- Integração de `cdrom0:` ao PS2 VFS por uma ponte nativa.

Mas havia três lacunas:

1. O importador de boot extraía somente `SYSTEM.CNF` e o ELF BOOT2. Não criava a instalação completa do jogo.
2. O leitor ISO Android ainda não implementava a segunda camada nem a exportação de `.disc_layout` e `.disc_meta`.
3. O runtime recebia o identificador virtual `gowcdroot:/` como se fosse um caminho físico. O runtime normaliza `IoPaths` com `std::filesystem::absolute()`, então esse identificador podia ser convertido em um caminho do host e deixar de corresponder ao prefixo esperado pelas pontes. Além disso, `DiscSource::openExtracted()` precisa receber a pasta física que contém o layout extraído.

## 5. Correção de integração enviada

Na revisão de 06/11/2026, foi alterado o fluxo de montagem:

- A ponte JNI guarda o caminho físico da raiz privada depois de abrir `CdRootVfs`.
- A nova função C ABI `gowAndroidCdRootCopyPath()` copia esse caminho para o runtime.
- `PS2Runtime::IoPaths.cdRoot` recebe a pasta privada real, em vez do identificador `gowcdroot:/`.
- O adaptador de `PS2Vfs::open()` e `PS2Vfs::stat()` identifica o dispositivo PS2 `cdrom0:` a partir do caminho já analisado, antes de qualquer conversão para caminho do host.
- O smoke test agora monta o caminho físico privado, em vez de depender de um caminho virtual que não é válido para `std::filesystem`.

Commits da alteração:

- `e531ea1` — expõe a cópia do caminho físico.
- `1741d98` — guarda e disponibiliza a raiz privada no bridge JNI.
- `d124812` — atualiza o smoke test para usar a raiz física.
- `a7f93b4` — ajusta o runtime e a seleção de `cdrom0:` no PS2 VFS.

**Limite da validação:** os workflows acionados por essa alteração precisam concluir com sucesso. Mesmo depois de compilada, a correção ainda não comprova que a fonte de setores, a extração completa ou o boot real funcionem.

## 6. Arquitetura Android necessária

O runtime deve receber a pasta física privada da instalação em `IoPaths.cdRoot`. A partir dela:

- `PS2Vfs` encaminha operações de caminho `cdrom0:` ao `CdRootVfs` seguro.
- `DiscSource::openExtracted(cdRoot)` carrega `.disc_layout` e `.disc_meta` para operações de LSN do CDVD/IOP.
- O loader usa o caminho local do ELF recompilado.
- Os módulos IRX ficam disponíveis no layout esperado pelo jogo, incluindo `IOP_MOD/` quando requerido.

Não usar `gowcdroot:/` como substituto de `IoPaths.cdRoot`. Se um nome virtual voltar a ser necessário no futuro, ele deve ser um campo/contrato separado, e não um `std::filesystem::path` reaproveitado como caminho físico.

## 7. Próximas etapas de implementação

1. **Testar a correção de montagem:** concluir os workflows host e Android e analisar qualquer falha antes de acrescentar outra camada.
2. **Implementar um scanner ISO de dupla camada:** enumerar diretórios/arquivos nas duas camadas, validar extents e tamanhos, rejeitar caminhos ambíguos e detectar formatos não suportados.
3. **Gerar o layout de disco:** produzir `.disc_layout` e `.disc_meta` no formato consumido por `DiscSource::openExtracted()`, mantendo LSNs originais.
4. **Implementar extração completa e segura:** staging privado, previsão de espaço, progresso, cancelamento, validação e publicação atômica. Não carregar a imagem ou os pacotes inteiros na RAM.
5. **Criar testes sintéticos sem dados do jogo:** ISO de dupla camada mínima, extents por camada, setores de metadados, leituras cruzando limites, EOF/LSN inválido, cancelamento e instalação incompleta.
6. **Validar o contrato do runtime:** testar `DiscSource::openExtracted()`, `readSectors()`, `findFile()` e `layer1Start()` usando apenas a imagem sintética.
7. **Integrar o boot:** disponibilizar ELF, arquivos e IRX extraídos; depois ligar lifecycle do runtime à GameActivity e instrumentar cada fase de inicialização.

## 8. Dados do usuário e CI

A ISO, ELF, IRX, arquivos extraídos, `.disc_layout`, `.disc_meta` reais, saves e C++ recompilado do jogo devem permanecer no armazenamento privado local. Nenhum desses dados deve ser adicionado ao Git, a Issues, a releases ou à CI pública.

A CI deve usar exclusivamente imagens e arquivos sintéticos fabricados para os testes. A implementação deve calcular LSNs, tamanhos, número de camadas e manifesto a partir da imagem selecionada pelo usuário.
