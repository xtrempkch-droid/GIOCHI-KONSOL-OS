# Lista de jogos

Alvos de porte (ou de emulação) para o Giochi Konsol OS — uma lista de desejos,
**não** o catálogo do console. Os três primeiros já são **jogos padrão do
sistema** ([docs/GAMES.md](../docs/GAMES.md)); o resto é candidato, sem código
no repositório.

Entrar na lista de jogos padrão exige licença compatível: domínio público,
permissão dos autores ou código/arte próprios. GPL e software proprietário
ficam de fora — ver [docs/GAMES.md](../docs/GAMES.md).

| Jogo | Ano | Estado |
|---|---|---|
| [Colossal Cave](./Colossal%20Cave%20Adventure/Colossal%20Cave%20Adventure.md) | 1976 | **jogo padrão do sistema** — projeto Godot 4.3 em 2,5D, com português, inglês e espanhol |
| [Arcade Volleyball](./Arcade%20Volleyball/Arcade%20Volleyball.md) | 1988 | **jogo padrão do sistema** — projeto Godot 4.3 + port C++/SDL2 de referência |
| [Spacewar](./Spacewar/Spacewar.md) | 1962 | **jogo padrão do sistema** — projeto Godot 4.3, regras tiradas do listing original do PDP-1 |
| Spatial Hyperdrive | 1993 | candidato |
| The Great Giana Sisters | 1987 | candidato |
| Shattered Pixel Dungeon | 2014 | **fora da lista padrão**: GPL-3.0, não é domínio público (ver [docs/GAMES.md](../docs/GAMES.md)) |
| Eye of the Beholder C64 | 2022 | candidato |
| Gloom | 1995 | candidato |
| Crazy Sue | 1990 | candidato |
| Super Obliteration | 1993 | candidato |

## Como um candidato vira algo jogável

O runtime do sistema é **Godot** (ADR-07: 3.5 LTS no RK3326, 4.x
Compatibility/GLES3 no RK3566), então há dois caminhos:

1. **Godot nativo:** projeto Godot próprio, como o Arcade Volleyball. É o
   caminho com menos atrito — o `giochi-forge` já lê `forge.json` e classifica
   o jogo contra o perfil do aparelho. Falta o export ARM64 (template musl,
   ADR-03) para virar `.giochi`.
2. **Emulação:** um core do RetroArch e `runtime: "retroarch"` no
   `metadata.json` — previsto para a fase 2, ver [docs/APPS.md](../docs/APPS.md).

Licença e origem de cada título precisam ser checadas **antes** de qualquer
porte ou empacotamento: parte desta lista é software comercial.
