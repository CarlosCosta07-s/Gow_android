# God of War — Android Native Port

**Projeto independente para criar um port nativo de *God of War* (PlayStation 2, 2005), exclusivo para Android ARM64.**

> Estado: fundação documental. Ainda não há APK jogável nem código do jogo neste repositório.

## Objetivo

Construir uma aplicação Android que execute o código original de God of War por **recompilação estática MIPS R5900 → C++/ARM64**, com runtime nativo para os subsistemas de PS2 usados pelo jogo e renderização do Graphics Synthesizer por Vulkan.

Não queremos um emulador genérico, uma ISO dentro do APK ou um caminho final baseado em interpretação/JIT do Emotion Engine. O usuário selecionará seus próprios dados de jogo.

## Escopo

- **Plataforma única:** Android.
- **Arquitetura inicial:** `arm64-v8a`.
- **Gráficos:** Vulkan nativo, com surface e swapchain Android reais.
- **Código nativo:** C++20, CMake e Android NDK.
- **Aplicativo:** Gradle e AndroidX GameActivity.
- **Primeira revisão-alvo:** NTSC-U, serial `SCUS-97399`; outras regiões serão avaliadas depois.
- **Desenvolvimento:** somente branch `main`.

## Situação e referências

A auditoria inicial encontrou três linhas de trabalho relevantes:

1. [God of War: Recomp — CodeAbhi826](https://github.com/CodeAbhi826/God-Of-War-Recomp): referência mais próxima para Android ARM64, seleção de ISO, validação ISO9660 e build NDK. O próprio projeto informa que o renderer GS Android ainda impede a apresentação de frames e que não há release jogável.
2. [god-of-war-recomp — KIexster](https://github.com/KIexster/god-of-war-recomp): referência para mapa de funções, configuração e progresso desktop de recompilação/gameplay.
3. [God-Of-War-Native-Port — Waffle565](https://github.com/Waffle565/God-Of-War-Native-Port): referência para subsistemas nativos de GS, VU1, IOP, DMA, áudio e saves no PC.

Esses projetos são referências, não resultados nossos. **Nenhum código deles foi copiado neste commit.** Qualquer integração futura deverá registrar o commit, os autores, a licença e as alterações.

## Roadmap

1. Fixar as bases de código e licenças.
2. Construir o shell Android ARM64 e validar seleção do disco do usuário.
3. Implementar apresentação Vulkan real na janela Android.
4. Integrar recompilação estática e runtime nativo.
5. Apresentar a primeira imagem do jogo.
6. Completar renderização, controles, áudio, FMVs e saves.
7. Validar áreas do jogo e otimizar para GPUs móveis.

Consulte o [roadmap](docs/ROADMAP.md) para critérios de aceitação.

## Direitos e dados

Este repositório **não fornece** ISO, ELF, IRX, PAK, texturas, modelos, áudio, vídeos, saves nem código gerado a partir do executável original. O usuário deverá fornecer sua própria cópia legitimamente obtida e cumprir as leis aplicáveis.

A licença GPL-3.0 deste repositório cobre somente o código que seus autores podem licenciar; ela não altera os direitos sobre God of War. Consulte a [política de direitos e dados](docs/LEGAL_E_DADOS.md).

## Documentação

- [Arquitetura técnica](docs/ARQUITETURA.md)
- [Roadmap e critérios de conclusão](docs/ROADMAP.md)
- [Auditoria das referências](docs/REFERENCIAS.md)
- [Créditos e avisos de terceiros](THIRD_PARTY_NOTICES.md)
- [Política de direitos e dados](docs/LEGAL_E_DADOS.md)
- [Testes e diagnóstico](docs/TESTES_E_DIAGNOSTICO.md)
- [Estado verificado](docs/STATUS.md)

## Licença

O código original deste projeto é distribuído sob **GNU GPL-3.0-only**. Dependências e trechos reutilizados permanecem sujeitos às suas próprias licenças e avisos. O conteúdo proprietário de God of War não está incluído.

*God of War* é propriedade de seus respectivos titulares. Este é um projeto de fãs independente, sem afiliação ou endosso da Sony Interactive Entertainment ou Santa Monica Studio.
