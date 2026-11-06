# Leitura de disco e instalação no Android

**Revisão:** 2026-11-06  
**Base runtime:** Waffle/KIexter `49ca62c1d20b4659c61df6286c54e86edf411012`

## 1. Leitor ISO9660 disponível

O módulo `android/app/src/main/cpp/iso9660_reader.h/.cpp` implementa leitura aleatória de imagens ISO9660 com setores de 2048 bytes.

- Recebe um descritor aberto pelo Storage Access Framework e duplica o FD.
- Verifica o descritor primário ISO9660 no setor 16 e o registro do diretório raiz.
- Resolve arquivos por diretórios e extents, sem carregar a imagem inteira na RAM.
- Normaliza nomes sem diferenciar maiúsculas/minúsculas e aceita sufixos de versão como `;1`.
- Lê intervalos por offset, inclusive atravessando limites de setor.
- Valida limites da imagem, registros, tamanho de diretórios e componentes de caminho; rejeita traversal, diretório usado como arquivo e extents fora da imagem.

O componente tem fixture ISO sintética e teste host. Ele será reaproveitado como **fonte do instalador BT3**. O suporte atual a lookup/leitura aleatória não equivale, sozinho, a um instalador completo: ainda precisamos de enumeração segura da árvore e extração com progresso.

## 2. Limitações atuais

A primeira versão não aceita:
- imagens raw de CD com setores de 2352 bytes;
- CHD, CSO ou formatos compactados;
- extensões Joliet ou Rock Ridge;
- arquivos ISO9660 multi-extent.

A imagem selecionada precisa ser uma ISO9660 compatível com o parser. O reconhecimento do PVD não prova que a revisão do jogo é suportada nem que todos os arquivos necessários foram instalados.

## 3. Decisão de arquitetura: instalação por extração seletiva, inspirada no Dragon Rage BT3

A referência é [Dragon Rage / Tenkaichi3DecompAndroid](https://github.com/dfdx047/Tenkaichi3DecompAndroid). Seu fluxo BT3 valida a imagem e copia o executável e diretórios de dados selecionados para a área privada do aplicativo. Os arquivos AFS são mantidos como arquivos inteiros, em vez de serem desmembrados em milhares de arquivos menores.

**Decisão para Gow_android:** adotar esse modelo de instalação. A ISO será usada como fonte de importação; depois que a instalação terminar e for validada, o jogo deverá ler os arquivos preparados no armazenamento privado do aplicativo, e não depender de acesso aleatório contínuo à ISO via SAF durante a partida.

Não vamos copiar cegamente a lista de caminhos do Dragon Rage. God of War tem sua própria estrutura, e a lista de arquivos será determinada pela revisão suportada e pelo rastreamento dos acessos de boot/runtime.

### Fluxo de instalação planejado

1. **Selecionar e validar:** o launcher recebe a URI pelo SAF, verifica PVD ISO9660, `SYSTEM.CNF`, serial/região, revisão e identidade do ELF.
2. **Planejar espaço:** enumerar os arquivos que pertencem ao manifesto de instalação, calcular o tamanho necessário e conferir o espaço disponível antes de começar.
3. **Extrair para staging:** copiar em blocos para uma pasta temporária privada, mantendo nomes, diretórios e arquivos de dados originais. O ELF deve ficar no caminho local esperado pelo loader.
4. **Preservar arquivos grandes:** manter pacotes/arquivos de dados em seus formatos originais. Não expandir archives em milhares de arquivos pequenos nem carregar todo o conteúdo para a RAM.
5. **Verificar:** conferir tamanho e, quando houver hash de referência confiável, hash dos arquivos copiados. Um manifesto incompleto ou um erro de leitura impede a publicação da instalação.
6. **Publicar atomicamente:** somente depois da extração e validação, promover a pasta temporária para a pasta versionada da instalação. Cancelamentos não podem deixar uma instalação parcial marcada como pronta.
7. **Executar localmente:** configurar o VFS/CD root para a pasta instalada. A sessão não deve precisar manter um descritor da ISO aberto.
8. **Reparar/reinstalar:** se arquivos locais estiverem ausentes ou corrompidos, solicitar a ISO novamente e refazer a validação antes de reparar.

O identificador do cache deve considerar título, serial/revisão, versão do instalador e hash do manifesto. A instalação não pode ser aceita apenas porque existe um marcador antigo.

### Regras de desempenho para aparelhos modestos

- A extração acontece fora do loop do jogo, com progresso, tratamento de falta de espaço e cancelamento seguro.
- O instalador usa buffers limitados e não mantém a ISO nem os arquivos extraídos inteiros na memória.
- Arquivos grandes continuam como arquivos grandes; só serão convertidos ou divididos quando existir necessidade técnica comprovada.
- Durante a partida, o VFS lê do armazenamento privado sob demanda. Qualquer cache será limitado, configurável e medido; não haverá preload indiscriminado de toda a árvore.
- Prefetch assíncrono só será adicionado para padrões de leitura identificados por profiling. Não vamos presumir que mais cache sempre melhora aparelhos com pouca RAM.
- O primeiro objetivo é remover SAF/ISO do caminho crítico de leitura durante o jogo. Isso melhora a previsibilidade do I/O, mas não garante, sozinho, maior FPS.

## 4. Código de leitura híbrida já existente — etapa de transição

A implementação anterior preparou um caminho híbrido para leitura direta da ISO. Na revisão documentada, ele inclui:

- `android_disc_bridge.cpp`, com uma instância de `Iso9660Reader` e operações C ABI para abrir, consultar tamanho, ler e fechar arquivos;
- entrada JNI `GowGameActivity.nativeSetDiscFileDescriptor(int)`, que duplica o FD recebido;
- patch privado no `PS2IopHostAdapter`/`PS2Vfs` para rotear caminhos `gowdisc:/` ao provider ISO.

A CI comprovou a compilação dessa ponte e do runtime privado:
- Android [run 42161298942](https://github.com/CarlosCosta07-s/Gow_android/actions/runs/42161298942), commit `836da9a80a19d04b8b934da0481655ea167bb8e3): testes host DualShock 2/ISO, preparação do runtime privado, `assembleDebug` e publicação do APK ARM64.
- Host [run 42161298950](https://github.com/CarlosCosta07-s/Gow_android/actions/runs/42161298950): `py_compile`, CLI e compilação de `ps2_recomp` + runtime Waffle estrito.

**O que isso comprova:** os componentes híbridos compilaram nos ambientes descritos. **O que não comprova:** importação BT3 completa, teste com ISO real, extração completa dos arquivos de God of War, boot ou gameplay.

Com a decisão BT3, a ponte de leitura direta da ISO passa a ser uma base transitória e/ou ferramenta do importador. O caminho final de jogo deverá usar arquivos locais e um `cdRoot` privado. Não vamos manter duas estratégias ativas no caminho normal sem uma razão medida.

## 5. Próximas etapas

1. Completar a enumeração de diretórios ISO9660 e criar testes sintéticos para árvores, arquivos grandes, nomes e erros.
2. Definir o manifesto de instalação de God of War por revisão, baseado na estrutura real da imagem e no rastreamento dos acessos necessários.
3. Implementar estimativa de espaço, extração em staging, progresso, cancelamento, verificação e promoção atômica.
4. Preparar o ELF original no caminho privado que `PS2Runtime::loadELF()` espera; o loader atual usa `std::ifstream`.
5. Configurar o VFS para a pasta instalada e substituir o caminho `gowdisc:/` de runtime quando a leitura local estiver validada.
6. Medir leituras, memória, tempos de carregamento e frame time em aparelhos de referência; só então definir tamanho de cache e prefetch.
7. Validar instalação, reparo, interrupção, falta de espaço e inicialização com uma ISO do usuário em aparelho real.

## 6. Privacidade e integridade

ISO, ELF, IRX, arquivos extraídos, saves e C++ gerado a partir do executável permanecem fora do Git, Issues, releases e CI pública. A CI usa somente fixtures sintéticas sem dados proprietários.

## Referências do Dragon Rage

- [IsoImage.kt](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/android/app/src/main/java/com/dfdx047/dragonrage/data/IsoImage.kt): PVD, árvore ISO9660 e leitura aleatória.
- [GameDataInstaller.kt](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/android/app/src/main/java/com/dfdx047/dragonrage/data/GameDataInstaller.kt): validação, extração seletiva, progresso e verificação.
- [Bt4Installer.kt](https://github.com/dfdx047/Tenkaichi3DecompAndroid/blob/main/android/app/src/main/java/com/dfdx047/dragonrage/data/Bt4Installer.kt): referência do modo híbrido, que não será o caminho principal escolhido para Gow_android.
