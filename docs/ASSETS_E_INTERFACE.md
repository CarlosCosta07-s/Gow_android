# Identidade visual do launcher

## Direção de arte

A interface combina uma organização de biblioteca inspirada em launchers de PC, como a Steam, com uma ambientação original de fantasia grega e guerra antiga: pedra escura, bronze envelhecido, vermelho profundo, tipografia serifada e um emblema geométrico inspirado em símbolos gregos.

A identidade visual não utiliza retratos de Kratos, capturas de tela, artes promocionais ou outros materiais visuais extraídos de God of War. O emblema do aplicativo é um vetor original do próprio projeto.

## Ativos do aplicativo

- `drawable/ic_launcher_emblem.xml`: emblema vetorial original em vermelho e bronze, usado no ícone adaptativo e como elemento decorativo no launcher.
- `mipmap-anydpi-v26/ic_launcher.xml`: define o ícone adaptativo Android.
- `values/colors.xml`: fundo do ícone adaptativo.
- A interface é construída com views Android, gradientes, bordas e botões nativos; não depende de uma imagem de fundo para funcionar.

## Organização da interface

- Barra lateral com a biblioteca selecionada, configurações, sobre, saída e versão.
- Página principal com título do jogo, versão, identificação da plataforma e ações rápidas.
- Ação principal para jogar, ação secundária para selecionar a imagem do disco e teste Vulkan separado.
- Cartões para o estado da imagem selecionada e o estágio real do port.
- Layout horizontal com rolagem vertical do conteúdo para acomodar diferentes dimensões de tela.

## Comportamento

- **Jogar:** abre o seletor de disco se nenhum arquivo estiver selecionado; se já existir uma imagem, informa o estágio real do projeto e não finge iniciar o jogo.
- **Selecionar imagem do disco:** abre o Storage Access Framework e mantém a validação ISO9660 existente.
- **Testar apresentação Vulkan:** abre a atividade de smoke test separadamente.
- **Configurações / Sobre:** apresentam informações reais e créditos do projeto.
- **Sair:** encerra o launcher.

A validação ISO9660 continua limitada ao descritor primário; a identificação de `SYSTEM.CNF` e a inicialização do jogo permanecem pendentes.
