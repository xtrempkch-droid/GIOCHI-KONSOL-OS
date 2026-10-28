# Lista de jogos

Alvos de porte (ou de emulação) para o Giochi Konsol OS — uma lista de desejos,
**não** o catálogo do console. Com exceção do Arcade Volleyball, nada aqui tem
código nem pacote `.giochi` no repositório.

| Jogo | Ano | Estado |
|---|---|---|
| [Arcade Volleyball](./Arcade%20Volleyball/Arcade%20Volleyball.md) | 1988 | projeto Godot 4.3 (Compatibility/GLES3) jogável no host + port C++/SDL2 de referência |
| [Spacewar](./Spacewar/Spacewar.md) | 1962 | projeto Godot 4.3 jogável no host, regras tiradas do listing original do PDP-1 |
| Spatial Hyperdrive | 1993 | candidato |
| The Great Giana Sisters | 1987 | candidato |
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
