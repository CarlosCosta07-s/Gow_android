# Política de direitos, dados e distribuição

Gow_android é um projeto independente de preservação e interoperabilidade. Não é afiliado, patrocinado ou endossado pela Sony Interactive Entertainment ou Santa Monica Studio.

## Direitos sobre o jogo

*God of War*, seus personagens, marcas, código original, músicas, vídeos, texturas, modelos, imagens e demais elementos permanecem sujeitos aos direitos de seus respectivos titulares.

A licença GPL-3.0 deste repositório cobre somente o código que seus autores podem licenciar. Ela não concede licença sobre o jogo original nem sobre dados derivados dele.

## Dados fornecidos pelo usuário

O usuário deverá fornecer sua própria cópia legitimamente obtida e cumprir as leis aplicáveis. A posse de um disco não deve ser interpretada como autorização automática para distribuir cópias, executáveis ou conteúdo protegido.

A aplicação poderá ler a imagem selecionada pelo usuário. O repositório não fornecerá arquivos do jogo.

## Conteúdo proibido no repositório público

Nunca adicionar em commits, Issues, Pull Requests, releases, artefatos públicos de CI ou anexos:

- ISO, BIN/CUE ou dump do disco.
- ELF original do jogo ou módulos IRX.
- Arquivos PAK, texturas, modelos, áudio, FMVs, mapas ou dados extraídos.
- Saves ou cartões de memória de usuários.
- Código C/C++ gerado diretamente a partir do ELF proprietário.
- Configurações privadas que contenham material extraído ou dados pessoais.

A regra se aplica mesmo quando os dados estiverem compactados, renomeados ou divididos em partes.

## CI e builds

A CI pública pode compilar o shell, bibliotecas abertas e testes independentes de dados proprietários.

Qualquer etapa que necessite de ELF ou dados extraídos deverá ocorrer em ambiente privado controlado pelo usuário. Não enviar insumos ou código gerado do jogo para artefatos públicos.

Um APK com código gerado a partir do executável original ainda pode levantar questões de direitos autorais mesmo sem texturas, músicas ou vídeos. Não presumir que remover assets torna a distribuição automaticamente autorizada. Revisar o cenário jurídico antes de distribuir builds completas.

## Créditos e licenças

- Preservar GPL-3.0 dos componentes reutilizados quando aplicável.
- Manter avisos de copyright e licenças de bibliotecas.
- Não apresentar código externo como autoria própria.
- Não usar a licença do projeto para afirmar que o conteúdo de God of War é livre.

Esta página é uma política técnica do repositório, não aconselhamento jurídico.
