# FORGE — interface e identidade visual

## Conceito

**FORGE** é o nome da interface de biblioteca do projeto Gow_android. Sua organização é inspirada em launchers modernos de jogos, com navegação lateral, área principal dedicada ao título, ações rápidas e cartões de estado.

A direção de arte é minimalista e moderna: superfícies grafite, tipografia sem serifa, espaçamento amplo e vermelho profundo como cor de destaque. A inspiração em God of War I aparece de forma sutil na paleta e no símbolo Ômega, sem reproduzir personagens, capturas, logotipos completos ou artes do jogo.

## Ícone

O ícone adaptativo do aplicativo é uma **bigorna com uma faísca** (referência ao nome FORGE), em aço claro sobre um fundo grafite iluminado por um brilho de brasa vermelha na parte inferior. É arte vetorial original do projeto, composta por três camadas em `res/drawable/`:

- `ic_app_icon_background.xml`: fundo em gradiente radial (grafite `#0D0F12` → brasa `#C5222D`);
- `ic_app_icon_foreground.xml`: bigorna, ponto de calor e faíscas, dentro da zona segura de 66 dp;
- `ic_app_icon_monochrome.xml`: a mesma silhueta em um único tom, para ícones temáticos do Android.

O emblema Ômega usado dentro do launcher (`drawable/ic_launcher_emblem.xml`) é um recurso separado e não foi alterado.

Não são usados retratos de Kratos, screenshots ou materiais extraídos do jogo.

## Organização da interface

- Barra lateral enxuta com marca FORGE, biblioteca, configurações, sobre, versão e saída.
- Cabeçalho principal com biblioteca, **hora e bateria do aparelho** e plataforma.
- Cartão do jogo com título, plataforma original e estado de desenvolvimento.
- Ações para jogar, selecionar imagem do disco e testar apresentação Vulkan.
- Cartões para estado da imagem e do runtime.
- Layout horizontal e conteúdo principal rolável para acomodar telas menores.

## Hora e bateria

O cabeçalho mostra a hora e a data do aparelho e o nível da bateria. A implementação é dividida para ser testável:

- `BatteryReading` (Java puro, sem Android) interpreta uma leitura: percentual validado e limitado a 0–100, estado (carregando, carga completa, conectado, na bateria), nível de alerta e texto para leitores de tela. Alerta de **nível baixo** a partir de 15% e **crítico** a partir de 5%; nunca é exibido com uma fonte de energia conectada. Há teste em JVM comum em `src/test/java/.../BatteryReadingTest.java`, executado na CI.
- `DeviceStatusMonitor` registra receptores apenas para broadcasts protegidos do sistema (`ACTION_BATTERY_CHANGED`, `ACTION_TIME_TICK`, `ACTION_TIME_CHANGED`, `ACTION_TIMEZONE_CHANGED`). Não exige permissões. Inicia em `onStart` e para em `onStop`, entregando os valores atuais imediatamente ao iniciar. A hora respeita o formato 12/24 h e o idioma do aparelho.
- `BatteryGaugeView` desenha o indicador com Canvas (nível colorido por alerta e raio quando carregando), sem depender de emoji ou fonte de ícones.

Esses dados são apenas informativos no launcher. Ainda não alteram o comportamento do jogo (por exemplo, perfis de economia de energia), que fica para a etapa de otimização.

## Comportamento

- **Jogar:** abre o seletor de disco se nenhum arquivo estiver selecionado; se já existir uma imagem, informa o estágio real do projeto e não finge iniciar o jogo.
- **Selecionar imagem do disco:** abre o Storage Access Framework e mantém a validação ISO9660 existente.
- **Testar apresentação Vulkan:** abre a atividade de smoke test separadamente.
- **Configurações / Sobre:** apresentam informações reais e créditos do projeto.
- **Sair:** encerra o launcher.

A validação ISO9660 continua limitada ao descritor primário; a identificação de `SYSTEM.CNF` e a inicialização do jogo permanecem pendentes.
