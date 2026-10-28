# Jogos padrão do sistema

Os jogos que vêm no aparelho. Servem a três coisas ao mesmo tempo:

1. **demonstração** — quem liga o portátil tem o que jogar antes de copiar nada;
2. **teste** — gamepad, vídeo, áudio e Launcher são exercitados por código real;
3. **benchmark** — cada um roda um modo `--bench` determinístico e emite um
   laudo de RAM/VRAM/framerate, comparável entre aparelhos.

A fonte única é [assets/default-games.json](../assets/default-games.json)
(schema: [schemas/default-games.schema.json](../schemas/default-games.schema.json)).

## A lista

| Jogo | Ano | Licença | Por que pode vir no aparelho |
|---|---|---|---|
| [Spacewar](../Jogos/Spacewar/Spacewar.md) | 1962 | domínio público | criado no MIT em 1962, nunca registrado, distribuído livremente desde o início |
| [Colossal Cave](<../Jogos/Colossal Cave Adventure/Colossal Cave Adventure.md>) | 1976 | permissão dos autores | Crowther & Woods liberaram o código; o `open-adventure` (BSD-2-Clause) é distribuído com a permissão deles |
| [Arcade Volleyball](<../Jogos/Arcade Volleyball/Arcade Volleyball.md>) | 1988 | código e arte próprios | escrito neste repositório; nenhum binário, gráfico ou som do original |

Todos os três são **Godot 4.3 + Compatibility/GLES3**, com arte vetorial e som
sintetizado: nenhum arquivo de imagem ou áudio entra no rootfs.

## Fora da lista

| Jogo | Licença | Motivo |
|---|---|---|
| Shattered Pixel Dungeon | **GPL-3.0** | não é domínio público. Retirado da lista de jogos padrão em 2026-10-27 por decisão do mantenedor; volta quando houver decisão sobre como proceder |

O GPL é *copyleft*: distribuir um derivado obriga a abrir o código e manter a
mesma licença. Isso não é incompatível com o projeto em si, mas é uma decisão
de licenciamento do sistema inteiro, não de um jogo — por isso está fora e
registrado em `excluded`, com o motivo.

## Como a regra é aplicada

`tools/check-default-games.py` roda em `make lint` e recusa:

- jogo da lista com licença `copyleft` ou `proprietary`;
- jogo sem projeto, documentação, cena de bench, `scripts/bench.gd` ou
  `tests/run_tests.sh`;
- lista vazia (o aparelho viria sem jogo);
- jogo em `games` e em `excluded` ao mesmo tempo.

`tests/test_default_games.py` (suíte `jogos-padrao`) cobra o mesmo e mais:
toda entrada precisa de **evidência** de licença, e o jogo GPL precisa
continuar registrado em `excluded` (para ninguém reverter sem saber).

## Benchmark

O mesmo comando funciona no PC e no aparelho:

```sh
make bench                        # 1800 quadros (~30 s) por jogo
make bench FRAMES=3600 QUALITY=2  # mais longo, preset fixo (comparável)
GODOT=/caminho/godot make bench   # binário do motor fora do PATH
```

No aparelho, com os jogos instalados pelo pacote `giochi-games`:

```sh
GAMES_ROOT=/usr/share/giochi/games OUT=/user/bench tools/bench-games.sh
```

Cada laudo (`build/bench/<id>.json`) traz:

| Bloco | Campos |
|---|---|
| `frames` | `p50_ms`, `p95_ms`, `p99_ms`, `fps_mean`, `process_ms_mean`, `physics_ms_mean`, `over_33ms_ratio` |
| `budget` | `ram_mb` (RSS de `/proc/self/status`), `vram_mb`, `draw_calls`, `objects`, `passed` |
| `device` | CPU, GPU, API gráfica, núcleos, RAM total, tela |
| `config` | preset, viewport, quadros, semente, tamanho da janela |

O runner **falha** (exit 1) se algum laudo trouxer `budget.passed = false`,
ou seja, acima de **700 MB de RAM** ou **128 MB de VRAM** (o teto do RK3566 em
`device_profile.json`; o RK3326 tem 1 GB/64 MB e é mais apertado).

A entrada em bench é sempre sintética — cada jogo implementa seu autoplay, para
a medição não depender de quem está segurando o controle.

### Medições de host (não são do aparelho)

Host com Mesa 26 e GPU discreta (Radeon RX 580), preset **Médio**:

| Jogo | RAM | VRAM | Draw calls | p50 | p99 |
|---|---|---|---|---|---|
| Spacewar | 187 MB | 18,2 MB | 28 | 0,7 ms | 1,3 ms |
| Colossal Cave | 187 MB | 24,2 MB | 49 | 0,9 ms | 3,0 ms |
| Arcade Volleyball | 188 MB | 19,7 MB | 47 | 1,2 ms | 1,7 ms |

**Isso não é o número do aparelho.** O Mali-G52 com Panfrost tem outro driver,
e os 187 MB são quase todos do motor Godot. No aparelho, rode o bench e leia o
painel `F3` dentro de cada jogo.

## Instalação na imagem

`buildroot/package/giochi-games` (`BR2_PACKAGE_GIOCHI_GAMES=y`, já nos cinco
defconfigs) instala:

```
/usr/share/giochi/default-games.json     manifesto
/usr/share/giochi/games/<id>/            projeto Godot de cada jogo
```

`install-games.sh` lê o manifesto em vez de repetir nomes de jogo — incluir um
jogo novo é editar **um** arquivo. O `.godot/` (cache do editor) fica de fora:
os três projetos somam ~420 KB no rootfs.

**O que ainda falta:** o template de export do Godot para `aarch64`/musl
(ADR-03). Sem ele não há binário pronto, então o pacote instala os projetos, e
o jogo precisa do motor no aparelho para rodar. Quando o template entrar no
Buildroot, é nesse `.mk` que ele passa a ser usado.

## Jogar

```sh
godot --path "Jogos/Spacewar/godot"
godot --path "Jogos/Colossal Cave Adventure/godot"
godot --path "Jogos/Arcade Volleyball/godot"
```

Controles e regras de cada um estão no `.md` da pasta do jogo. O Colossal Cave
tem **três idiomas** (português, inglês e espanhol), trocáveis no menu de pausa
sem reiniciar a partida — o texto vive em `scripts/i18n/locale.gd` e a
simulação não tem nenhuma frase.

## Verificação

```sh
make lint          # inclui tools/check-default-games.py
make test          # inclui a suíte jogos-padrao
make godot-test    # invariantes + fumaça + orçamento dos três
make bench         # laudos de benchmark
```
