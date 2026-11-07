# Porta obrigatória antes de entregar um APK para teste

**Regra:** nenhum APK será oferecido ao usuário como candidato de teste enquanto todas as portas abaixo não forem aprovadas. Um APK que apenas instala, abre o FORGE, valida a ISO ou apresenta um painel Vulkan é um shell de diagnóstico — não é um port jogável.

## 1. Proveniência e integridade do build

- [ ] Commit da branch `main` identificado e congelado para o candidato.
- [ ] APK ARM64 compilado a partir desse mesmo commit.
- [ ] SHA-256 do APK e certificado de assinatura registrados.
- [ ] Assinatura persistente confirmada; não aceitar a chave efêmera do runner para uma atualização de instalação existente.
- [ ] Estratégia de atualização preserva a instalação privada da ISO e os saves; nunca solicitar desinstalação como atalho.
- [ ] Nenhum ELF, IRX, PAK, captura, save ou dado derivado do disco incluído no repositório ou no artifact público.

## 2. Cobertura AOT antes de empacotar

O primeiro boot de captura é apenas uma etapa interna de desenvolvimento. Ele pode parar corretamente ao encontrar o primeiro bloco ainda não recompilado e **não pode ser entregue como APK de teste**.

- [ ] A tabela EE gerada contém funções reais do executável NTSC-U `SCUS_973.99` e cobre o endereço de entrada.
- [ ] O relatório privado AOT contém os seis translation units VU1.
- [ ] Todos os módulos IOP capturados têm código nativo e registro correspondente.
- [ ] O relatório não indica blocos VU1 sem suporte nem callouts ao interpretador.
- [ ] Todos os itens acima pertencem à mesma ISO/revisão e ao mesmo workspace/build identificado.
- [ ] A compilação do candidato usa `tools/build_android_game.py --require-full-native-aot`; o modo bootstrap/parcial deve falhar antes de gerar APK.

**Limite dessa verificação:** `full_native_aot=true` significa que o material presente nas capturas está completo segundo o gerador. Não prova que uma fase ainda não executada nunca carregará um novo microprograma ou módulo. Por isso a verificação de boot/menu abaixo continua obrigatória.

## 3. Boot real, imagem e menu

Tudo precisa ser reproduzido com a ISO NTSC-U real do projeto e com o APK candidato, não com fixtures sintéticas:

- [ ] Importação da ISO pelo seletor Android conclui sem perda de permissão ou corrupção.
- [ ] `SYSTEM.CNF`, BOOT2 e layout/setores são reconhecidos e montados pelo VFS privado.
- [ ] O runtime EE inicia e atravessa os módulos IOP e microprogramas VU1 usados no boot sem falta de código nativo.
- [ ] O backend GS apresenta o primeiro frame real do jogo; não basta limpar a tela, exibir geometria sintética ou capturar um frame vazio.
- [ ] O menu principal fica visível e estável.
- [ ] Direcional, START, CONFIRMAR e CANCELAR funcionam nos controles virtuais estilo PS2.
- [ ] O menu continua acessível após fechar/reabrir o jogo e após uma pausa/retomada do Android.
- [ ] Três inicializações a frio consecutivas passam sem crash, fechamento da Activity, retorno ao botão INSTALAR ou tela preta.

A evidência mínima é composta por: identificação do commit/APK, log privado correlacionado por estágio, captura do primeiro frame/menu e checklist de entrada. Os arquivos de jogo e as capturas AOT permanecem privados.

## 4. Desempenho e estabilidade

- [ ] Registrar modelo/SoC/GPU, API Android, driver Vulkan e memória disponível do aparelho de validação.
- [ ] Medir frame time, uso de CPU/GPU, PSS/RAM, I/O e temperatura durante pelo menos 20 minutos de execução representativa.
- [ ] Confirmar que não ocorre throttling térmico que faça o jogo perder progressivamente a cadência.
- [ ] Comparar a mesma cena e configuração antes/depois de cada otimização.
- [ ] Não prometer FPS, autonomia ou temperatura universal sem medições.

Um build aprovado em CI prova compilação; não prova desempenho térmico em aparelho físico. A aprovação de um aparelho também não garante compatibilidade com todos os modelos Android.

## 5. Experiência final do jogador

Antes de chamar o APK de pronto para o usuário:

- [ ] Instalar apenas o APK e escolher somente a ISO original.
- [ ] Não exigir Codespaces, Termux, ADB, computador, terminal, SDK/NDK ou exportação de arquivos intermediários.
- [ ] Preparação, cache e diagnóstico ocorrem dentro do FORGE, com progresso, cancelamento e recuperação.
- [ ] Depois da primeira preparação, o jogo inicia novamente a partir do cache privado, sem repetir a compilação.
- [ ] O APK entregue tem assinatura compatível com a instalação já existente ou uma migração de dados comprovadamente segura.

## 6. Classificação dos artefatos

| Tipo | Uso | Pode ser enviado ao usuário para jogar? |
|---|---|---|
| Shell de CI | Verificar Gradle, NDK, launcher e integração nativa | **Não** |
| Bootstrap AOT | Capturar blocos VU1/IOP ainda ausentes durante desenvolvimento | **Não** |
| AOT parcial | Iterar sobre capturas ainda incompletas | **Não** |
| Candidato de menu | AOT completo para o trajeto validado, boot/frame/menu e controles aprovados | **Sim, para teste controlado** |
| Release | Testes funcionais e térmicos, assinatura estável e experiência APK + ISO aprovados | **Sim** |

## 7. Situação atual em 07/11/2026

- Shell Android ARM64: builds de CI anteriores aprovados.
- AOT EE a partir da ISO real: ainda sem evidência de build completo no ambiente disponível.
- Cobertura VU1/IOP: capturas reais e cobertura completa ainda não comprovadas.
- Primeiro frame real/menu: não comprovados.
- Controles e áudio no jogo: não comprovados; a camada de áudio Android continua pendente.
- Medições físicas de bateria/temperatura: inexistentes.
- Fluxo de jogador somente APK + ISO: ainda não implementado por completo.

**Conclusão:** não existe atualmente um APK que possa ser descrito honestamente como pronto para você baixar e testar o jogo. O artefato de CI é diagnóstico. Essa classificação só muda quando as portas 1–5 forem demonstradas e registradas.
