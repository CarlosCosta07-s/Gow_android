# Validação de cenas gráficas reais — Gow_android

**Objetivo:** comprovar a correção visual do backend GS Android com capturas do jogo real, sem publicar arquivos ou imagens proprietárias.

## Regras de evidência

Uma cena só pode receber o estado **[APROVADA]** quando todos estes itens existirem:

1. A cena foi executada pelo código nativo recompilado do jogo em um aparelho Android ARM64.
2. A captura Android foi feita pelo botão Debug **Capturar cena** ou pela captura inicial automática.
3. Existe uma captura de referência da mesma cena, região, resolução e estado de jogo, produzida por um renderer de referência.
4. Ambas as imagens têm dimensões idênticas e foram comparadas com `tools/compare_gs_scene.py`.
5. O relatório guarda o commit do app, aparelho/GPU, resolução, métricas, limiares escolhidos e diferenças visuais observadas.

Build, teste unitário, fixture sintética, tela do launcher ou frame vazio não contam como cena real.

## Matriz mínima de cenas

| ID | Tipo de cena | O que observar no GS | Estado |
|---|---|---|---|
| S01 | Menu/tela estática do jogo | sprites, fontes, paleta, alpha, posição e proporção | [PENDENTE] |
| S02 | Primeira cena 3D controlável | geometria, UV/STQ, texturas, perspectiva, depth e câmera | [PENDENTE] |
| S03 | Cena com múltiplos planos/objetos | ordenação de framebuffer, Z test/write, scissor e máscaras | [PENDENTE] |
| S04 | Cena com efeitos transparentes | blending AB/CD/FIX, alpha test, DATE, PABE e FBA | [PENDENTE] |
| S05 | Cena com partículas/efeitos e transparência sobre o cenário | formatos de textura, CLUT, filtragem e mistura com fundo | [PENDENTE] |
| S06 | Transição/fade/cena de carregamento | PMODE, DISPLAY1/2, ALP, BGCOLOR, interlace e timing | [PENDENTE] |

As linhas descrevem categorias de teste; a cena exata deve ser identificada a partir do jogo em execução, sem presumir que um efeito específico aparece em determinada fase.

## Procedimento por cena

1. Iniciar o jogo por um build com tabela AOT gerada localmente a partir da cópia do usuário.
2. Anotar localização/cena, estado (menu, gameplay, transição), resolução e frame/timing observável.
3. Capturar a mesma composição no renderer de referência. Evitar comparar frames de instantes diferentes quando há animação, câmera ou partículas.
4. No Android Debug, tocar **Capturar cena**. O Logcat `GowAndroid` fornece o caminho do arquivo `gow_android_scene_NN.ppm`.
5. Copiar a captura para um diretório local e executar:

```bash
python tools/compare_gs_scene.py \
  --reference ./reference_scene.ppm \
  --actual ./gow_android_scene_01.ppm \
  --tolerance 8 \
  --min-matching-pixels 95 \
  --max-mae 12 \
  --diff ./scene_diff.ppm
```

6. Inspecionar visualmente a imagem de diferenças. Um resultado numérico aprovado é necessário, mas não substitui a revisão de erros estruturais como textura errada, objeto ausente, HUD deslocado ou framebuffer incorreto.
7. Registrar os resultados no relatório local. Repetir com a mesma cena após mudanças de rasterizador.

## Relatório de execução

Manter uma cópia local de um CSV ou Markdown com estas colunas:

- ID da cena e descrição observável.
- Commit do app e versão do APK.
- Modelo do aparelho, SoC, GPU, Android e driver Vulkan.
- Origem/revisão do jogo, sem incluir serial privado ou caminho do usuário no relatório público.
- Resolução da captura, região, frame/timing e estado do jogo.
- Arquivos PPM de referência/Android (somente locais).
- MAE, RMSE, PSNR, erro máximo por canal e porcentagem de pixels na tolerância.
- Limiares usados, observações visuais, falhas e resultado.

## Segurança e privacidade

- ISO, ELF, IRX, arquivos extraídos, saves, screenshots, texturas, áudio e frames do jogo permanecem no dispositivo/diretórios privados do usuário.
- Não anexar capturas reais a commits, issues, releases, Actions, logs públicos ou artefatos de CI.
- O comparador e o gravador são testados com imagens sintéticas; isso não requer nem autoriza enviar conteúdo do jogo.

## Estado atual (06/11/2026)

**[CAPTURA E TENTATIVA DE BOOT CONECTADAS; CENA REAL PENDENTE]** Existe captura automática do primeiro `PresentationFrame`, captura local adicional sob demanda no APK Debug (até 32 frames por processo), gravador PPM P6 e comparador offline. A `main` também tenta iniciar o worker EE quando o ELF/AOT privado é válido e o GS Android + presenter Vulkan estão prontos. A CI #356 validou a correção da assinatura e publicou o shell; a CI #362 verifica o novo caminho e o log de estados GS não suportados. O APK público continua com tabela AOT vazia, portanto não inicia o guest.

**[PENDENTE — PRIMEIRA CENA REAL]** A tabela AOT pública ainda está vazia, pois o ELF NTSC-U do usuário não foi recompilado neste ambiente. O APK sem AOT recusa corretamente a carga; a tentativa do worker só é armada depois da validação AOT e da preparação GS/Vulkan. Nenhuma captura desta matriz foi produzida pelo God of War em Android. Não marcar qualquer linha como aprovada até haver captura real e referência pareada.
