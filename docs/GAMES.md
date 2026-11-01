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
| Shattered Pixel Dungeon | **GPL-3.0** | fora por decisão do mantenedor (2026-10-27) **e** por não ter port: o copyleft colide com a raiz assinada (§6, anti-tivoization) e o jogo é Java/libGDX, sem projeto Godot nem bench. Ver a nota da pasta |

**O que a GPL-3.0 exige** (texto conferido no `LICENSE.txt` do projeto): §2
confirma permissão ilimitada para *executar* o programa; §4 e §5a obrigam a
manter os avisos de copyright e a datar as modificações; §5c faz o trabalho
inteiro passar a GPL-3.0 para quem receber uma cópia; §6 obriga a entregar a
*Corresponding Source*. Ou seja: **crédito é necessário, mas não é o que
permite o uso** — o preço do copyleft é a fonte e a licença, não a atribuição.

**O que a GPL NÃO faz:** contaminar o resto da imagem. §5, último parágrafo,
diz que incluir um trabalho coberto num **agregado** não estende a licença às
outras partes. O Launcher, o `giochi-init` e o kernel não viram GPL por
conviverem com um jogo GPL na mesma mídia.

**O impedimento real é a raiz assinada.** §6 (*Installation Information*)
exige, para um "User Product", os "métodos, procedimentos, chaves de
autorização ou outras informações necessárias para instalar e executar versões
modificadas" — e a dispensa só vale quando ninguém consegue instalar código
modificado (ROM). O GIOCHI usa SquashFS assinado Ed25519 em A/B, com a chave no
projeto: a dispensa não vale, e entregar a chave destruiria a raiz imutável.

Além disso, ele **não tem port**: é Java/libGDX, não Godot, e não tem projeto,
cena de bench nem `tests/run_tests.sh` — o que a seção `games` exige. O debate
de licença não muda esse fato. Detalhes e o caminho que funcionaria (instalar
em `/user`, fora da imagem assinada) estão em
[Jogos/Shattered Pixel Dungeon/Shattered Pixel Dungeon.md](<../Jogos/Shattered Pixel Dungeon/Shattered Pixel Dungeon.md>).

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

## Candidatos já avaliados

Jogo proposto e **recusado** fica com a análise documentada, para a decisão não
depender de memória:

| Jogo | Veredito | Por quê |
|---|---|---|
| SuperTux | **não adicionado** (2026-10-31) | 40 fps é jogável (o motor tem passo lógico fixo), mas o `data/` ocupa ≈160 MB comprimido — não cabe na partição de 160 MB —, a licença é GPL-3.0+ com assets CC-BY-SA, e é binário nativo (o contrato de jogo padrão hoje é Godot). O pacote do **Alpine aarch64** confirma: `supertux-data` = 244 MB instalados para 5 MB de binário. Ver [LOJA.md](LOJA.md). |
| itch.io (catálogo) | **não adotado** (2026-10-31) | o modelo de dados não tem ARM (`all`/`386`/`amd64`) — não há como filtrar o que roda em aarch64. Ver [LOJA.md](LOJA.md). |

## Outras engines

O `runtime` aceito em `metadata.json` é `godot` | `retroarch` | `proton`
(ADR-07, ADR-13). Engine nova só entra com ADR própria — e as duas perguntadas
não passam:

| Engine | Veredito | Por quê |
|---|---|---|
| **RAGE** (Rockstar — GTA IV/V, RDR2) | **impossível** | proprietária, nunca licenciada, sem fonte; binários só x86; exige D3D ou **Vulkan** — `device_profile.json` declara `vulkan: false` e o RK3326 não tem (ADR-06); e GTA V pede 4–8 GB de RAM, contra 1–2 GB |
| **id Tech 5** (codinome "RAGE", jogo *Rage* de 2011) | **impossível** | a id Software liberou até o **id Tech 4**: a lista oficial de repositórios dela termina em `DOOM-3-BFG` (GPL-3.0). O id Tech 5 **nunca** foi liberado |
| **Construct 3** (Scirra) | **não cabe** | o runtime **não é aberto** (os 14 repositórios da Scirra são SDK de addon, plugins e exemplos). O export é **HTML5**, então rodar exige motor de navegador: `webkit2gtk` = **114,9 MB** instalado no aarch64 + `gtk+3.0` 11,5 MB, contra um rootfs de **28 MB** numa partição de **160 MB** |

> **Atenção à ambiguidade do nome:** "RAGE" designa duas coisas diferentes — a
> engine da Rockstar e o codinome da id Tech 5. As duas são fechadas; não vale
> a pena discutir qual foi pedida.

### O que **é** viável na mesma família

**id Tech 4** — `DOOM-3-BFG` é **GPL-3.0** (repositório oficial da id Software) e
o porte canônico, [dhewm3](https://github.com/dhewm/dhewm3), também é GPL-3.0.
O PortMaster **já tem** `ports/doom3` compilado para ARM (`dhewm3` 37,8 MB,
`base.so` 33,2 MB, `d3xp.so` 35,0 MB — builds sem strip). É a engine "grande"
que de fato cabe: os **dados** são do jogo comercial, então o usuário fornece,
como em qualquer port legal.

Fora isso, o que já está na mesa: **Godot** (ADR-07, os três jogos padrão),
**Solarus** e **Minetest** (pacotes já no Buildroot) e o **TIC-80** (engine MIT de
4,9 MB). Ver [LOJA.md](LOJA.md).

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
