# Requisito de produto — instalação e jogo somente pelo APK

## Contrato obrigatório para o jogador

A experiência final do Gow_android deve exigir **somente dois itens**:

1. O APK oficial do Gow_android para Android ARM64.
2. A ISO original compatível de God of War, selecionada pelo próprio jogador.

O jogador não deve precisar de computador, Codespaces, Termux, ADB, Git, terminal, SDK, NDK, Gradle, exportação de ELF/IRX, arquivos de captura, ferramentas externas, conta de desenvolvedor ou acesso à internet para preparar ou iniciar o jogo.

### Jornada esperada

1. Instalar o APK.
2. Abrir o FORGE.
3. Selecionar a ISO original através do seletor de arquivos Android.
4. O aplicativo identifica a edição suportada, valida a imagem e prepara a instalação privada automaticamente.
5. O aplicativo prepara ou obtém de forma legítima todo o código nativo necessário, com progresso, cancelamento, retomada e diagnóstico claro.
6. O aplicativo inicia God of War.
7. Nas próximas sessões, reutiliza o cache validado e abre o jogo sem repetir a preparação.

A preparação inicial deve ser privada, versionada e vinculada à identidade da ISO e à versão do runtime/recompilador. Arquivos derivados, cache, capturas, saves e metadados não devem ser publicados no Git, enviados para serviços externos ou solicitados como exportação manual.

## Arquitetura técnica a investigar

O runtime final permanece nativo ARM64 e não deve recorrer a emulador convencional nem a interpretador/JIT do EE como caminho normal de execução.

A implementação atual usa PS2Recomp em um host Linux para transformar o ELF do jogo em C++ e depois compilar esse C++ com o Android NDK. Isso **não pode ser confundido com uma experiência pronta para jogadores**.

Para cumprir o contrato de produto, avaliar duas rotas:

### Rota A — AOT preparado antes da distribuição

Gerar o código nativo específico da versão suportada durante o processo de desenvolvimento/release e incluí-lo de forma legítima no pacote distribuído. O APK ainda solicita a ISO do jogador para validar identidade e ler os dados originais necessários.

Esta rota só pode ser usada se a origem, licença e forma de distribuição do código recompilado forem juridicamente aceitáveis. Não copiar executável, dados, assets, firmware ou conteúdo proprietário da ISO para o repositório, CI ou artefato público.

### Rota B — AOT local no primeiro uso

Incorporar ao APK um pipeline suportado no Android que analise a ISO, gere o código ARM64 e produza o cache nativo localmente. O app deverá incluir todas as ferramentas necessárias; não depender de Codespaces, Termux, ADB ou conexão com a internet.

Antes de adotar essa rota, provar tecnicamente:
- disponibilidade e tamanho de um compilador C++/backend AArch64 executável no Android;
- integração com a linker/loader e com as regras de segurança do Android;
- possibilidade de gerar e atualizar as unidades AOT de EE, VU1 e IOP sem interpretação;
- compatibilidade com versões atuais do Android e política da loja de distribuição;
- consumo de CPU, RAM, armazenamento, bateria e temperatura em aparelhos de entrada;
- retomada segura após falta de espaço, encerramento do processo ou aquecimento.

Não declarar a rota viável apenas porque clang existe no Android NDK: o NDK fornecido no build host contém toolchains precompiladas para hosts de build específicos. É necessário provar um toolchain executável no próprio Android ou implementar um backend de geração/linkagem ARM64 adequado.

## Restrições Android e distribuição

O Android 14+ exige que arquivos carregados dinamicamente sejam tratados como somente leitura antes de serem escritos e recomenda evitar dynamic code loading sempre que possível. Qualquer mecanismo de código nativo gerado localmente deve ser projetado com verificação de integridade, armazenamento privado, assinatura/identidade da geração e compatibilidade com as regras da versão Android suportada.

Se a distribuição for pela Google Play, a política atual também restringe o download de código executável fora do mecanismo da loja. Não basear o produto em baixar bibliotecas nativas, DEX/JAR ou executáveis de um servidor para contornar a ausência de AOT local. A rota escolhida deve passar por revisão de política antes do lançamento.

## Ferramentas de desenvolvimento não são dependências do jogador

Codespaces, CI, Termux, ADB, host Linux, scripts build_android_game.py, build_recompiler_host.py e sincronização de capturas são ferramentas de engenharia e validação. Podem permanecer disponíveis para os mantenedores, mas **não fazem parte do fluxo de instalação, preparação ou jogo do usuário final**.

O documento PHONE_ONLY_BUILD.md descreve uma ferramenta opcional de desenvolvimento remoto, não instruções para jogadores.

## Critérios de aceitação do produto

- [ ] Instalar o APK sem instalar ferramentas adicionais.
- [ ] Selecionar apenas a ISO original pelo FORGE.
- [ ] Preparar automaticamente a instalação e o código nativo no próprio fluxo do produto, sem Codespaces/Termux/ADB.
- [ ] Não pedir ao jogador ELF, IRX, ZIP AOT, microcódigo ou diretórios extraídos.
- [ ] Funcionar offline depois que o APK e a ISO estiverem no aparelho.
- [ ] Guardar cache privado íntegro e reutilizável por versão/identidade de ISO.
- [ ] Exibir progresso, cancelamento, retomada, espaço necessário e erros compreensíveis.
- [ ] Não usar intérprete/JIT do EE no caminho de jogo aprovado.
- [ ] Alcançar o menu principal e comprovar gameplay em aparelho físico.
