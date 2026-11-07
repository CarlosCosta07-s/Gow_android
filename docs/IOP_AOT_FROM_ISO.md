# IOP AOT a partir da ISO — limites e próximo passo

**Objetivo:** reduzir o número de iterações de captura VU1/IOP sem criar código nativo com endereços ou relocations presumidos.

## O que o carregador realmente faz

O perfil Android usa o carregador IOP do PS2Recomp, fixado em `ran-j/PS2Recomp@c5a9d02573410a2085a4b4b831b0b68ba3515440` e preparado pelas alterações de runtime do projeto. O contrato do carregador pode ser conferido em [iop_module_loader.cpp](https://github.com/ran-j/PS2Recomp/blob/c5a9d02573410a2085a4b4b831b0b68ba3515440/ps2xIOP/src/emulator/services/iop_module_loader.cpp).

O carregador lê ELF32 little-endian MIPS e, para cada módulo:

1. Lê os segmentos `PT_LOAD` ou, quando não existem, as seções `SHF_ALLOC`.
2. Decide se pode preservar o endereço de carga ou se precisa realocar a imagem.
3. Copia os segmentos para a RAM IOP, zera BSS e aplica as relocations.
4. Obtém entrada e GP do metadado `PT_SCE_IOPMOD` ou do `PT_MIPS_REGINFO`.
5. Inicia o módulo e registra o nome a partir da folha do caminho carregado.

Os tipos `ET_SCE_IOPRELEXEC` e `ET_SCE_IOPRELEXEC2` são relocáveis por definição. O carregador também pode realocar um `ET_EXEC` se o intervalo não couber na RAM IOP ou começar abaixo da base reservada. O código de instruções que o gerador consome é o conteúdo **já carregado e relocacionado**, não simplesmente o arquivo IRX original.

O gerador `port/kiexter/tools/iopn_gen.py` espera um dump produzido em execução: `<name>.bin` contém os bytes relocacionados na base real; `modules.txt` contém nome, base, tamanho e hash FNV. O registry gerado só atende quando nome, base e bytes do módulo carregado coincidem. Referência: [gerador IOP do port de PC](https://github.com/Waffle565/God-Of-War-Native-Port/blob/main/tools/iop/iopn_gen.py).

## O que foi implementado agora

`tools/inventory_iop_irx.py` é executado automaticamente depois que a ISO original é extraída para o workspace privado. Ele:

- localiza arquivos com extensão `.IRX`;
- valida ELF32, little-endian e MIPS, tabelas de segmentos e seções;
- conta relocations MIPS e identifica tipos fora do conjunto suportado pelo carregador;
- calcula o intervalo de endereços dos segmentos `PT_LOAD`;
- classifica como *candidato de imagem fixa* apenas um `ET_EXEC` que não exige relocação, cabe na RAM IOP, não tem relocation records e não possui segmentos sobrepostos;
- grava `iop-irx-inventory.json` fora do repositório público.

A classificação é deliberadamente conservadora. Um candidato de imagem fixa **não é ainda um módulo AOT liberado**: o nome de registro, conflitos com outros módulos carregados e os bytes efetivamente vistos pelo runtime precisam ser comprovados. O inventário não gera `.cpp`, não altera a ISO e não publica nenhum dado do jogo.

Testes com ELF sintético cobrem imagens fixas, tipos relocáveis, relocation records, tabelas inválidas, extensões maiúsculas e caminhos simbólicos. A CI não utiliza uma ISO de God of War.

## Por que não compilar todos os IRX diretamente

Compilar os arquivos brutos em ordem alfabética ou assumir uma base padrão seria incorreto. A base dinâmica pode depender da ordem de carga e do estado do alocador; as relocations podem reescrever instruções e ponteiros. Um registry produzido com um nome/base/hash diferente não será selecionado pelo runtime nativo. Pior: um binário de outro endereço pode saltar para código/dados incorretos e fechar o jogo.

A estratégia segura permanece:

1. Pré-inventariar IRX da ISO e identificar imagens realmente fixas.
2. Reutilizar diretamente apenas módulos fixos depois de comparar nome, endereço e bytes com o carregador.
3. Para módulos relocáveis, continuar usando capturas após a relocação real, ou implementar e testar um simulador do carregador com a mesma ordem e o mesmo estado de memória do runtime.
4. Só marcar AOT como completo quando a identidade SHA-256 do BOOT2, a cobertura VU1/IOP e todos os fontes C++ estiverem comprovados pelo gate de build.
5. Mesmo então, exigir primeiro frame, menu, controles e reinicializações com a ISO real antes de liberar APK.

## Situação

- [x] Inventário estático de IRX extraídos e classificação conservadora.
- [x] Integração do relatório ao workspace privado do build ISO.
- [x] Testes sintéticos adicionados às CIs Android e host.
- [ ] Comparar o inventário com os nomes/bases/hashes capturados num boot real.
- [ ] Determinar se algum IRX do perfil NTSC-U pode ser AOT sem captura, sem mudar sua identidade no registry.
- [ ] Comprovar menu e gameplay no APK candidato.

**Conclusão:** o inventário reduz incerteza e prepara uma otimização parcial, mas não substitui as capturas dinâmicas nem prova boot. O artefato de shell continua sendo diagnóstico e não deve ser enviado ao usuário para teste.
