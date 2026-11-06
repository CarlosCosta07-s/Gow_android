# FORGE — interface e identidade visual

## Conceito

**FORGE** é o nome da interface de biblioteca do projeto Gow_android. Sua organização é inspirada em launchers modernos de jogos, com navegação lateral, área principal dedicada ao título, ações rápidas e cartões de estado.

A direção de arte é minimalista e moderna: superfícies grafite, tipografia sem serifa, espaçamento amplo e vermelho profundo como cor de destaque. A inspiração em God of War I aparece de forma sutil na paleta e no símbolo Ômega, sem reproduzir personagens, capturas, logotipos completos ou artes do jogo.

## Ícone

O ícone adaptativo utiliza um **Ômega branco sobre fundo vermelho sólido**. O símbolo é construído como vetor Android original do projeto em `drawable/ic_launcher_emblem.xml`; o fundo adaptativo é definido em `values/colors.xml`.

Não são usados retratos de Kratos, screenshots ou materiais extraídos do jogo.

## Organização da interface

- Barra lateral enxuta com marca FORGE, biblioteca, configurações, sobre, versão e saída.
- Cabeçalho principal com biblioteca e plataforma.
- Cartão do jogo com título, plataforma original e estado de desenvolvimento.
- Ações para jogar, selecionar imagem do disco e testar apresentação Vulkan.
- Cartões para estado da imagem e do runtime.
- Layout horizontal e conteúdo principal rolável para acomodar telas menores.

## Comportamento

- **Jogar:** abre o seletor de disco se nenhum arquivo estiver selecionado; se já existir uma imagem, informa o estágio real do projeto e não finge iniciar o jogo.
- **Selecionar imagem do disco:** abre o Storage Access Framework e mantém a validação ISO9660 existente.
- **Testar apresentação Vulkan:** abre a atividade de smoke test separadamente.
- **Configurações / Sobre:** apresentam informações reais e créditos do projeto.
- **Sair:** encerra o launcher.

A validação ISO9660 continua limitada ao descritor primário; a identificação de `SYSTEM.CNF` e a inicialização do jogo permanecem pendentes.
