# Estado do projeto

**Data da auditoria:** 2026-11-06  
**Branch de desenvolvimento:** `main`  
**Objetivo:** port nativo de God of War (PS2, 2005) para Android ARM64, usando recompilação estática e runtime de serviços PS2 nativo; não é um emulador.

## [VERIFICADO] Shell FORGE

- O aplicativo FORGE foi aberto em pelo menos um aparelho Android real, conforme relato do mantenedor.
- A CI compilou o APK Debug ARM64 no run `42142814087`, commit `18839524eb1061c5331b5762213dadaa51d55dd3`.
- O teste Vulkan isolado foi removido do launcher após fechar o aplicativo no aparelho. A validação gráfica será feita com o GS e o jogo real.
- O launcher usa Storage Access Framework para escolher a imagem do usuário.
- A implementação inicial lê ISO9660 e `SYSTEM.CNF`, interpreta `BOOT2` e reconhece inicialmente `SCUS_973.99` (NTSC-U) e `SCES_531.33` (PAL). Ela compilou na CI; ainda falta validação com imagens reais em aparelho.

## [VERIFICADO] Componentes KIexter importados

Fonte: [KIexster/god-of-war-recomp](https://github.com/KIexster/god-of-war-recomp), commit `a226151feb6ed22937e42be485aaf28a83acd166`, licença declarada GPL-3.0.

- `recompiler/config/funcmap.csv`: mapa das 6.414 funções de `SCUS_973.99`. Verificação estrutural: intervalos ordenados, tamanhos coerentes e sem sobreposições.
- `recompiler/config/recomp.template.toml`: configuração de stubs, entry points, skips e patches do executável NTSC-U; marcadores `@ELF@`, `@MAP@`, `@OUT@` presentes.
- `port/kiexter/gow_overrides.cpp`: overrides específicos de God of War, em staging.
- `port/kiexter/tools/vu1n_gen.py`: gerador AOT VU1, em staging.
- `port/kiexter/tools/iopn_gen.py`: gerador AOT dos módulos IRX/IOP, em staging.
- `port/kiexter/tools/vu1n_heads.txt` e `port/kiexter/gow_pad2_packet.h`: arquivos auxiliares importados.

**Importante:** os arquivos de staging não estão no CMake/Gradle e ainda não são compilados pelo APK. O override depende do runtime Waffle/KIexster e de headers auxiliares; os geradores exigem dumps privados produzidos a partir da cópia do usuário.

## [VERIFICADO] Proveniência das bases

- O projeto KIexster usa como base o runtime de [Waffle565/God-Of-War-Native-Port](https://github.com/Waffle565/God-Of-War-Native-Port), branch `runtime`, licença GPL-3.0 nessa branch.
- Os cabeçalhos dos overrides e dos geradores VU1/IOP apontam para código originalmente derivado de Waffle565, commit `34f30d5e432751369177af115104f4d19af6d080`.
- O build KIexster fixa [PS2Recomp](https://github.com/ran-j/PS2Recomp) no commit `c5a9d02573410a2085a4b4b831b0b68ba3515440`, licença GPL-3.0.
- Nenhum runtime Waffle completo, PS2Recomp ou renderer desktop foi importado ainda. O antigo submódulo CodeAbhi foi removido.

## [NÃO TESTADO] Runtime e jogo

- Pipeline PS2Recomp integrado ao projeto e gerando C++ do ELF privado.
- Compilação do código gerado para Android ARM64.
- Runtime EE e serviços PS2 selecionados/adaptados para Android.
- Geração VU1 AOT cobrindo todos os microprogramas usados pelo jogo.
- Geração AOT dos módulos IRX necessários, sem fallback interpretado no produto final.
- Runner conectado à `GowGameActivity` mantendo o FORGE como launcher.
- Cache/VFS dos arquivos do disco no layout de setores que o runtime KIexter espera.
- Backend GS/Vulkan apresentando comandos e frames reais do jogo.
- Menus, gameplay, áudio, FMVs, controles, saves, desempenho e compatibilidade em GPUs diferentes.

## Próximos bloqueios reais

1. Validar em aparelho o parser `SYSTEM.CNF` com a imagem NTSC-U do usuário.
2. Integrar PS2Recomp na revisão fixada e gerar C++ em ambiente privado.
3. Selecionar e importar os módulos portáveis do runtime Waffle/KIexter que o jogo necessita.
4. Adaptar runner, lifecycle, leitura/cache do disco, áudio e entrada para Android.
5. Reaproveitar frontend GS/formatos portáveis e implementar o backend Vulkan Android.
6. Executar VU1 e IRX necessários por AOT, sem fallback interpretado.
7. Atingir boot, primeira imagem, menus e primeiro combate.

**Nenhum gameplay foi testado ou declarado funcional.** Os arquivos importados são preparação da base; não constituem ainda um runtime jogável.
