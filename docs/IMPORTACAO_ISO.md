# Importação inicial da ISO

**Data:** 2026-11-06  
**Branch:** `main`

## Objetivo

A ISO escolhida pelo usuário é uma fonte de importação. Depois da preparação, o runtime deverá trabalhar sobre uma árvore privada `cdRoot`; a ISO não deverá permanecer no caminho de I/O do gameplay.

A extração é seletiva e dirigida por manifesto. O projeto não assume que toda a imagem precisa ser copiada nem que os caminhos de outro jogo possam ser reutilizados.

## Fluxo implementado

1. O FORGE abre a imagem selecionada pelo Storage Access Framework.
2. A Activity valida o descritor primário ISO9660 e lê `SYSTEM.CNF`.
3. O valor `BOOT2` é normalizado como caminho relativo e o serial do executável é validado.
4. Antes da importação, o FORGE reabre a URI e repete a identificação. O serial e o caminho BOOT2 precisam corresponder à seleção validada.
5. A ponte JNI entrega o descritor, o diretório privado e duas entradas explícitas ao instalador nativo.
6. O instalador resolve todos os caminhos no ISO antes de escrever, valida os destinos e calcula o total.
7. Os arquivos são copiados em blocos de 64 KiB para uma pasta de staging privada. O callback reporta bytes e arquivos concluídos e pode cancelar a operação.
8. Cada arquivo e o diretório que o contém são sincronizados; a pasta de staging é sincronizada e publicada por rename no mesmo filesystem. Falhas e cancelamentos removem o staging.
9. Antes da cópia, o instalador consulta o espaço disponível no armazenamento privado. O FORGE oferece janela de progresso e cancelamento.
10. Depois da publicação, o FORGE abre a pasta `disc` no provider nativo `CdRootVfs`.

## Manifesto inicial

A primeira ação do botão **PREPARAR BOOT** instala somente:

| Origem na ISO | Destino privado |
|---|---|
| `SYSTEM.CNF` | `gow_boot_seed/disc/SYSTEM.CNF` |
| Caminho encontrado em `BOOT2` | `gow_boot_seed/disc/<caminho BOOT2>` |

A versão atual reconhece os executáveis `SCUS_973.99` (NTSC-U) e `SCES_531.33` (PAL). O caminho BOOT2 é derivado do conteúdo da imagem, não montado a partir de um caminho presumido.

## Limites e garantias

- O leitor atual suporta ISO9660 com setores de 2048 bytes e descritor primário. BIN raw de 2352 bytes, CHD, CSO, Joliet e Rock Ridge ainda não estão cobertos.
- Caminhos relativos rejeitam componentes vazios, `.`, `..`, separadores dentro de nomes e profundidade excessiva.
- Manifestos vazios, destinos repetidos, colisões arquivo/diretório, arquivos ausentes e tamanhos incompatíveis são rejeitados antes da cópia.
- A extração não carrega a imagem inteira na memória; usa um buffer de 64 KiB.
- A pasta publicada é `files/installations/gow_boot_seed`, no armazenamento privado do aplicativo.
- O `CdRootVfs` é somente leitura, faz acesso por descritores relativos à raiz, rejeita traversal e symlinks, aceita apenas arquivos regulares, limita listagens a 65.536 entradas e mantém handles para leituras por offset. A busca de componentes é case-insensitive ASCII e ignora sufixos ISO `;versão`; duas entradas que colidam após essa normalização são rejeitadas.
- O provider expõe a API C ABI `gowAndroidCdRootOpenFile/ReadFile/CloseFile` e o sinalizador `gowAndroidCdRootIsReady`. O patch local do runtime Waffle foi atualizado para usar o prefixo `gowcdroot:/` no lugar do provider direto da ISO; a compilação desse patch ainda está em validação na CI.
- Se essa pasta já existir, a importação não a substitui automaticamente.

## O que esta etapa NÃO significa

A pasta `gow_boot_seed` não é uma instalação completa. Ela contém apenas `SYSTEM.CNF` e o executável indicado por `BOOT2`. Ainda precisamos identificar os arquivos de dados e módulos realmente acessados no boot e no gameplay, incorporá-los ao manifesto versionado, integrar a chamada do VFS ao fluxo real de inicialização do PS2Runtime e ligar o carregamento do ELF ao pipeline PS2Recomp.

A importação bem-sucedida também não significa que o jogo inicializa. O provider cdRoot já existe e está exposto ao patch de preparação do runtime, mas o PS2Runtime ainda não é iniciado pelo launcher e não foi executado gameplay real nesta etapa.

## Referências de engenharia

Black for Android e Dragon Rage são referências de engenharia indicadas pelo mantenedor. Nenhum código, asset, script ou binário desses projetos é copiado por esta implementação. O instalador descrito aqui é código próprio do Gow_android.
