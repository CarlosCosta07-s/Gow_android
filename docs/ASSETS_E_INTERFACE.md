# Identidade visual do launcher

## Direção de arte

O launcher do Gow_android adota a estética de *God of War* (2005): pedra escura, bronze envelhecido, vermelho de guerra, tipografia serifada e o símbolo Ômega.

## Ativos do aplicativo

- `drawable-nodpi/ic_launcher_foreground.webp`: retrato estilizado com Ômega e moldura bronze, usado no ícone e como elemento visual do launcher.
- `mipmap-anydpi-v26/ic_launcher.xml`: define o ícone adaptativo Android.
- `values/colors.xml`: fundo do ícone adaptativo.
- A identidade da tela é construída com views Android, gradientes, bordas e botões nativos; textos e controles continuam funcionais.

## Comportamento

- **Jogar:** abre o seletor de disco se nenhum arquivo estiver selecionado; se já existir uma imagem, informa o estágio real do projeto e não finge iniciar o jogo.
- **Selecionar imagem do disco:** abre o Storage Access Framework e mantém a validação ISO9660 existente.
- **Testar apresentação Vulkan:** abre a atividade de smoke test separadamente.
- **Configurações / Sobre:** apresentam informações reais do build e do estado do projeto.
- **Sair:** encerra o launcher.

A interface é horizontal para combinar com a apresentação e com o jogo de ação. A validação ISO9660 continua limitada ao descritor primário; a identificação de `SYSTEM.CNF` e a inicialização do jogo permanecem pendentes.
