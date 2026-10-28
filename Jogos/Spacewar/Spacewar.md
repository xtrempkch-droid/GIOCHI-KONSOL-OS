# Spacewar

Port do **Spacewar** (1962, Steve Russell e outros, MIT) para o Giochi Konsol
OS, em **Godot 4.3 + renderizador Compatibility (GLES3)** — o runtime que o
[ADR-07](../../docs/architecture-v1.2.md) fixa para o RK3566.

Dois navios, um star no centro, torpedos que caem na gravidade do star e um
campo que dá a volta em si mesmo. Nada de imagem: o original rodava num canhão
de vetores do PDP-1, e este port desenha com vetores também.

| Item | Valor |
|---|---|
| Código | [godot/](./godot) |
| Entrada | gamepad evdev ou teclado (dois jogadores no mesmo teclado) |
| Motor | Godot 4.3, `gl_compatibility` |
| Requisitos | [godot/forge.json](./godot/forge.json) |
| Testes | `make godot-test` (ou `godot/tests/run_tests.sh`) |
| Estado | jogável no host, medido; **nunca** rodado em aparelho |
| Listing original | [Spacewar-1962-listing.pdp1](./Spacewar-1962-listing.pdp1) |

## O arquivo do listing original

O `Spacewar-1962-listing.pdp1` é o listing em assembly do PDP-1 que você
forneceu, preservado **sem edição** (7.522 linhas), para consulta. Ele não é
código executável hoje: é a fonte histórica de onde as regras abaixo foram
tiradas.

O cabeçalho do arquivo diz `fio-dec system, june 1963` e, na primeira página,
`/spacewar 3.1  24 sep 62` — ou seja, é a **versão 3.1 (24 set 1962)** montada
sob o sistema FIO-DEC em junho de 1963. As linhas marcadas com `-` são
comentários de quem transcreveu, e os símbolos `ZZnnnn` são gerados pela
ferramenta de transcrição: onde eles aparecem, o texto original foi preservado
logo acima. Isso importa na hora de ler — **o listing não é o paper tape
original de 1962**, é uma transcrição anotada.

Regras que saíram do listing (linha do arquivo preservado):

| Linha | O que está lá | O que virou no port |
|---|---|---|
| 119–120 | `high order 4 bits, rotate ccw, rotate cw, (both mean hyperspace) / fire rocket, and fire torpedo` | os 5 controles: girar ×2, impulso, tiro, hyperspace |
| 30 | `hyperspatial uncertancy` + `random number` | hyperspace sorteia a posição e escala a velocidade |
| 688 | `/ display a star` | o star no centro, desenhado e com gravidade |
| 1690 | `/ torpedo calc routine` | o torpedo também cai na gravidade do star |
| 1731–1790 | `/ hyperspace routines` … `/ in hyperspace` | cooldown + invulnerabilidade breve ao sair |
| 2075 | `/ check if torpedos exhausted` | magazine limitado com paiol que devolve 1 por vez |
| 2163–2164 | `/ spaceships into star` … `/ now go bang` | navio no star explode e dá o ponto ao adversário |

O listing também tem o tubo com recarga (`check if torp tube reloaded`) e o
torpedo saindo com a velocidade do navio — o clássico "tiro relativo", que é o
que torna a esquiva possível.

## Controles

| Ação | P1 | P2 |
|---|---|---|
| Girar à esquerda | `A` | `←` |
| Girar à direita | `D` | `→` |
| Impulso | `W` | `↑` |
| Tiro | `S` | `↓` |
| Hyperspace | `Q` | `/` |
| Pausa | `Esc` (ou `Start`) | — |
| Orçamento (RAM/VRAM) | `F3` | — |
| Gráficos (na pausa) | `←` `→` | — |
| Sair (na pausa) | `Q` | — |

Com **dois controles conectados**, cada um assume um navio. Com um controle
só, o P1 joga e a IA ([godot/scripts/sim/ai.gd](./godot/scripts/sim/ai.gd))
assume o P2 — o HUD mostra `P2 (CPU)` e troca para `P2` no instante em que
alguém aperta uma tecla de P2, sem reiniciar a partida.

## Regras

- **Star**: puxa navios **e** torpedos (`G = 4,2e6 px³/s²`, com piso de 70 px
  para não explodir no centro). Quem entra no star explode e dá o ponto ao
  adversário.
- **Campo toroidal**: sai pela direita, entra pela esquerda (e o desenho mostra
  o navio e o torpedo "cortados" nas duas bordas).
- **Torpedo**: herda a velocidade do navio (`navio.vel + direção × 430`), vive
  2,2 s, tem raio 3 e é consumido ao acertar outro torpedo.
- **Tubo**: 4 torpedos, 0,55 s de recarga entre tiros, e o paiol devolve **um**
  torpedo a cada 2,4 s. Segurar o botão não dispara em rajada.
- **Hyperspace**: sorteia a posição (longe do star e do adversário), corta a
  velocidade para 25%, dá 0,35 s de invulnerabilidade e tem 4 s de recarga.
- **Invulnerabilidade**: 1,2 s ao renascer, 0,35 s ao sair do hyperspace.
  O desenho pisca para o jogador saber.
- **Abate**: torpedo do adversário, star, ou colisão entre os dois navios (nesse
  caso os dois explodem e ninguém pontua). **Primeiro a 7 abates** vence; a
  partida reinicia 3,2 s depois.

## O que foi modernizado

Zero arquivo de imagem e zero arquivo de áudio: toda a arte é vetor e som é
sintetizado no boot — a mesma restrição que o original tinha por hardware.

| Recurso | Como | Custo |
|---|---|---|
| Céu de fundo | `shaders/space.gdshader`: 3 camadas de estrelas com paralaxe e cintilação, nebulosa, halo do star, vinheta e grão | 1 draw call, 0 textura |
| Star | núcleo vetorial + corona pulsante + 12 raios girando + 2 sprites aditivos | 1 textura compartilhada |
| Navio | casco vetorial (agulha com aletas), halo aditivo, chama proporcional ao empuxo | vetor |
| Fantasmas | o navio e o torpedo aparecem nos dois lados do campo toroidal | sem custo |
| Rastro do torpedo | 0–20 pontos por torpedo, desenhados em segmentos de alpha decrescente | vetor |
| Explosão | 40 partículas + 18 na cor do time + anel de choque + flash | 0–34/emissão |
| Impacto | hit-stop de 55–70 ms, slow motion de 240 ms, camera shake | ~zero |
| HUD | abates por lado, **pips da carga** do tubo (o vazio pulsa recarregando) e barra do **cooldown do hyperspace** | 1 atlas de fonte |
| Som | tiro, estrondo, hyperspace, recusa, abate, vitória, UI — 7 sons sintetizados | ~5 KB cada, em RAM |
| Presets | Baixo/Médio/Alto (partículas, rastro, corona, nebulosa, shake) | trocável na pausa |

## Orçamento (medido, não estimado)

Limite do RK3566 (`device_profile.json`): **700 MB de RAM** e **128 MB de
VRAM**. Medido no host com Mesa 26 e GPU real (Radeon RX 580) — **no aparelho
nada disso foi medido**:

| Preset | RAM (RSS) | VRAM | Draw calls | vs. teto |
|---|---|---|---|---|
| Baixo | 186,1 MB | 22,2 MB | 36 | 27% RAM / 17% VRAM |
| Médio (padrão) | 186,7 MB | 22,2 MB | 41 | 27% / 17% |
| Alto | 186,8 MB | 22,2 MB | 40 | 27% / 17% |

A VRAM quase não muda entre presets porque os presets trocam **trabalho de
CPU** (partículas, rastro, corona), não memória de vídeo: dos 22 MB, a arte do
jogo é menos de 1 MB — o resto é o tema/fonte padrão do motor. Leitura honesta:
o número do aparelho só o aparelho decide; por isso o jogo tem o painel `F3`
(RAM/VRAM/draws contra 700/128) e o teste de orçamento **falha** se estourar.

## Como rodar

```sh
godot --path "Jogos/Spacewar/godot"
```

Sem monitor (CI/SSH):

```sh
godot --headless --path "Jogos/Spacewar/godot" \
      res://scenes/Main.tscn --quit-after 240
```

## Testes

```sh
make godot-test                         # roda os dois jogos
GODOT=/caminho/godot make godot-test    # ou aponte o binário
```

O runner ([godot/tests/run_tests.sh](./godot/tests/run_tests.sh)) faz quatro
etapas e sai com 0 sem o motor instalado (mesmo critério de
`tests/test_screenshots.py`, que pula sem Pillow):

| Etapa | O que cobre |
|---|---|
| importação | gera o cache de classes; sem ela o `--script` falha com *"Could not find type"* |
| `tests/invariants.gd` | 12 testes: nascimento simétrico, campo toroidal, gravidade no navio **e** no torpedo, tiro relativo, magazine/paiol, tiro do adversário, star, hyperspace, invulnerabilidade, passo fixo, vitória e 20 000 passos de fuzz com a IA dos dois lados |
| cena principal | 240 quadros em headless; falha se o engine ou um script imprimir erro |
| `tests/budget_probe.gd` | mede RAM/VRAM nos três presets e **falha** acima de 700 MB / 128 MB (precisa de OpenGL; pulado sem `DISPLAY`) |

Resultado atual: **47 abates e 4 partidas completas em 20 000 passos**, maior
intervalo sem abate **28 s** (o primeiro abate sai por volta de 23 s), sem NaN,
sem navio ou torpedo fora da arena, sem carga inválida e sem torpedo vencido na
lista.

### Por que o modelo é separado da cena

`scripts/sim/spacewar.gd` não conhece engine gráfico: recebe `Controls` por
navio e devolve eventos (`fire`, `hit`, `explode`, `score`, `win`, `hyper`,
`respawn`). `scripts/main.gd` consome os eventos e faz o juice. É o que torna o
teste de 20 000 passos determinístico (o modelo aceita semente) e o que permite
medir a IA sem desenhar nada.

O passo é fixo (`scripts/sim/fixed_step.gd`): acumula o delta escalado pelo
`Engine.time_scale` e gasta em passos de 1/60 s, com teto de 3 por quadro. É
isso que faz o slow motion funcionar sem quebrar a integração — e é testado.

### A IA

Três decisões, em ordem de urgência: **fugir do star**, **desviar de torpedo**,
**atacar**. Ela não é invencível de propósito:

- a mira usa **intercepção de segunda ordem** (itera três vezes somando a
  aceleração centrípeta do alvo) — mira ingênua erra por dezenas de px, porque
  o alvo está orbitando;
- o empuxo só liga se uma simulação de 0,9 s mostrar que o navio **não** cai no
  star (antes ela mergulhava no star perseguindo o adversário);
- a esquiva é decidida a cada **0,30 s**, não a cada quadro. Sem esse atraso de
  reação ela desviava de 100% dos torpedos e a partida não terminava: mediu-se
  **2 abates em 333 s**. Com o atraso: **4–7 abates por minuto**.

## Integração com o repositório

```console
$ python3 -m src.forge.giochi_forge check --device rk3566-2gb \
      --project "Jogos/Spacewar/godot"
```

O `forge.json` declara `godot_version: 4.3`, GLES3 com fallback GLES2, 320 MB
de RAM e 48 MB de VRAM — os números medidos com folga. O cache `godot/.godot/`
é gerado e está no `.gitignore` e nas exceções de
[tools/check-tracked.sh](../../tools/check-tracked.sh).

## O que falta

- **Nada foi rodado em aparelho.** Mali-G52 com Panfrost ≠ Mesa: o número real
  pode mudar, e só o `F3` no aparelho resolve.
- **Não vira `.giochi` ainda**: falta o export template recompilado contra musl
  ([ADR-03](../../docs/architecture-v1.2.md)) e o `giochi-forge` não chama o
  export do Godot ([docs/APPS.md](../../docs/APPS.md)).
- **RK3326** (1 GB / 64 MB): os números cabem, mas o ADR-07 fixa Godot **3.5
  LTS** nesse SoC, e um projeto 4.3 não abre no 3.5.
- Sem áudio verificado no aparelho (os sons são gerados e o pool toca).
- Sem placar salvo entre sessões e sem opção de dificuldade da IA.
- O Launcher não conhece esta pasta: o jogo roda chamado à mão.

## Histórico

O arquivo original desta pasta era **só o listing do PDP-1** (7.522 linhas de
assembly), sem nenhuma documentação e sem código executável para o console.
Ele foi preservado como [Spacewar-1962-listing.pdp1](./Spacewar-1962-listing.pdp1)
e este projeto foi escrito a partir dele, seguindo o mesmo padrão do
[Arcade Volleyball](../Arcade%20Volleyball/Arcade%20Volleyball.md).

Quatro defeitos apareceram **medindo**, não lendo:

1. **O tubo não recarregava.** A carga só voltava ao renascer: depois do quarto
   tiro o navio (e a IA) ficava sem torpedo para sempre e a partida virava duas
   naves orbitando. Medido no soak: `partidas=321 abates=2`. Corrigido com o
   paiol (1 torpedo a cada 2,4 s).
2. **Respawn fora da arena**: era radial e num canto caía em `y = -39`. O
   teste de invariante pegou (`navio 1 fora da arena`).
3. **IA invencível**: desviava de 100% dos torpedos, 2 abates em 333 s.
   Resolvido com tempo de reação de 0,30 s.
4. **IA suicida**: perseguia o adversário direto para dentro do star (2 abates
   em 60 s, os dois por suicídio). Resolvido simulando o empuxo antes de ligar.

Os dois primeiros só existiam porque o teste roda 20 000 passos; nenhum deles
aparece em 10 segundos de jogo olhando a tela.
