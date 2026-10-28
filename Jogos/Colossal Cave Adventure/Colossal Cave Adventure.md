# Colossal Cave

Port de **Colossal Cave Adventure** (1976, Will Crowther e Don Woods) para o
Giochi Konsol OS, em **Godot 4.3 + renderizador Compatibility (GLES3)** — o
runtime que o [ADR-07](../../docs/architecture-v1.2.md) fixa para o RK3566.

Caverna por turnos, **multi-idiomas** (português, inglês e espanhol trocáveis no
menu de pausa), com vista em 2,5D desenhada por shader — sem nenhuma imagem no
repositório.

| Item | Valor |
|---|---|
| Código | [godot/](./godot) |
| Entrada | gamepad evdev ou teclado |
| Motor | Godot 4.3, `gl_compatibility` |
| Requisitos | [godot/forge.json](./godot/forge.json) |
| Testes | `make godot-test` (ou [godot/tests/run_tests.sh](./godot/tests/run_tests.sh)) |
| Estado | jogável no host, medido; **nunca** rodado em aparelho |
| Idiomas | pt-BR, en, es ([godot/scripts/i18n/locale.gd](./godot/scripts/i18n/locale.gd)) |
| Jogo padrão | sim ([assets/default-games.json](../../assets/default-games.json)) |

## Fontes e licença

Referências usadas (anotadas pelo mantenedor antes do código existir):

- <https://github.com/wh0am1-dev/adventure> — reimplementação web, usada como
  conferência de conteúdo (salas e objetos).
- <https://gitlab.com/esr/open-adventure> — a versão de Eric S. Raymond, cuja
  nota de licença (**BSD-2-Clause**) registra que o código de 1976 foi liberado
  por Crowther e Woods, que autorizam uso e redistribuição. É a evidência que o
  manifesto cita (`license.kind = freeware_permission`), e
  `tools/check-default-games.py` recusa jogo padrão sem ela.

**O que este port não é**: não é o *open-adventure* e não é o jogo de 430 pontos
("Adventure 2.5"). É uma **reconstrução** do esqueleto do original — 18 salas,
9 objetos, 100 pontos — escrita do zero em GDScript, com as regras que fazem o
jogo ser o jogo: escuridão, lampião que queima, limite de carga e tesouro que só
pontua na Casa do Poço. Não há uma linha de código de terceiros aqui.

## Regras

Todas vivem no modelo ([godot/scripts/sim/cave.gd](./godot/scripts/sim/cave.gd)),
que **não conhece o motor gráfico**: recebe `Action` e devolve eventos.

| Regra | Valor |
|---|---|
| Salas | 18, ligadas por `n/s/l/o/c/b` (norte, sul, leste, oeste, cima, baixo) |
| Objetos | 9: lampião, chave, corda, pássaro + 5 tesouros |
| Pontos | 20 por tesouro guardado, **100 no total** |
| Turnos | 900 (`MAX_TURNS`) |
| Lampião | 330 turnos aceso (`LAMP_FUEL`), queima **por turno**, não por segundo |
| Carga | 6 objetos (`MAX_CARRY`) — lampião + 5 tesouros |
| Queda | `-25` pontos e volta para a Casa do Poço |
| Escuridão | sem luz só se anda para sala **já visitada** |

Guardar tesouro só pontua na **Casa do Poço** (`predio`): é o contrato do
original e é testado. Fechar os 5 tesouros encerra a partida com vitória.

## Controles

| Ação | Teclado | Gamepad |
|---|---|---|
| Norte / Sul | `W` `↑` / `S` `↓` | analógico esquerdo (Y) |
| Leste / Oeste | `D` `→` / `A` `←` | analógico esquerdo (X) |
| Cima / Baixo | `R` `PgUp` / `F` `PgDn` | `Y` (baixo) |
| Pegar | `E` ou `Enter` | `A` |
| Largar | `Q` | `B` |
| Lampião | `L` | `X` |
| Pausa / menu | `Esc` | `Start` |
| Orçamento (RAM/VRAM) | `F3` | — |

No menu de pausa: `↑` `↓` escolhem a linha (**gráficos** ou **idioma**), `←` `→`
mudam o valor, `Q` sai. O idioma escolhido é salvo em `user://` e, no primeiro
boot, é detectado do locale do sistema.

## Idiomas

**98 entradas por idioma**, nos três, com paridade verificada por teste:
`test_idiomas_completos` reprova se faltar chave em qualquer língua.

A regra estrutural é mais forte que a cobertura: `cave.gd` **não contém uma
única frase**. O modelo devolve `message_key` + `message_args` e o HUD resolve o
texto no idioma ativo — `test_idiomas_sem_frase_no_modelo` varre o arquivo e
falha se aparecer texto de tela lá dentro. Consequência prática: acrescentar um
quarto idioma é acrescentar um bloco em
[locale.gd](./godot/scripts/i18n/locale.gd) e nada mais.

## Orçamento (medido, não estimado)

Limite do RK3566 (`device_profile.json`): **700 MB de RAM** e **128 MB de VRAM**.
Medido no host com Mesa 26 e GPU real (Radeon RX 580) — **no aparelho nada disso
foi medido**:

| Preset | RAM (RSS) | VRAM | Draw calls | vs. teto |
|---|---|---|---|---|
| Baixo | 185,9 MB | 18,2 MB | 33 | 27% RAM / 14% VRAM |
| Médio (padrão) | 186,3 MB | 18,1 MB | 33 | 27% / 14% |
| Alto | 186,3 MB | 18,1 MB | 37 | 27% / 14% |

A VRAM quase não muda entre presets porque eles trocam **trabalho de CPU**
(partículas, grão, névoa, tremor do lampião), não memória de vídeo: a caverna é
**um draw call e zero textura** — um shader procedural
([cavern.gdshader](./godot/shaders/cavern.gdshader)). O número do aparelho só o
aparelho decide; por isso existe o painel `F3` e o teste de orçamento **falha**
acima de 700 MB / 128 MB.

## Como rodar

```sh
godot --path "Jogos/Colossal Cave Adventure/godot"
```

Sem monitor (CI/SSH):

```sh
godot --headless --path "Jogos/Colossal Cave Adventure/godot" \
      res://scenes/Main.tscn --quit-after 240
```

Modo benchmark (rota automática, laudo em JSON):

```sh
godot --path "Jogos/Colossal Cave Adventure/godot" -- \
      --bench --bench-frames 1800 --bench-out /caminho/laudo.json
```

O `--bench` sobe o jogo com a rota de referência
([autoplay.gd](./godot/scripts/sim/autoplay.gd)) e um turno a cada 30 quadros,
para medir com a caverna **em andamento** e não parada no menu. `make bench`
roda os três jogos padrão e falha se algum estourar o orçamento.

## Testes

```sh
make godot-test                         # roda os três jogos padrão
GODOT=/caminho/godot make godot-test    # ou aponte o binário
```

O runner ([godot/tests/run_tests.sh](./godot/tests/run_tests.sh)) faz quatro
etapas e sai com 0 sem o motor instalado (mesmo critério de
`tests/test_screenshots.py`, que pula sem Pillow):

| Etapa | O que cobre |
|---|---|
| importação | gera o cache de classes; sem ela o `--script` falha com *"Could not find type"* |
| [tests/invariants.gd](./godot/tests/invariants.gd) | 14 testes: mapa consistente (toda saída volta), itens existem e estão em sala válida, movimento, escuridão, lampião, carga, tesouro só pontua na Casa do Poço, queda, turnos, **caverna completável**, rota com semente zero, **paridade dos três idiomas**, ausência de frase no modelo, troca de idioma não muda regra |
| cena principal | 240 quadros em headless; falha se o engine ou um script imprimir erro |
| [tests/budget_probe.gd](./godot/tests/budget_probe.gd) | mede RAM/VRAM nos três presets e **falha** acima de 700 MB / 128 MB (precisa de OpenGL; pulado sem `DISPLAY`) |

Resultado atual: **rota de referência com 39 turnos, 100 pontos, 5/5 tesouros** —
o teste prova que a caverna tem solução, não só que as peças existem.

### A rota de referência

`scripts/sim/autoplay.gd` é uma lista de 39 ações que fecha a caverna. Ela serve
a dois senhores: mede o aparelho sem depender da habilidade de quem joga, e
quebra na hora se o mapa mudar — é o alarme de "a caverna ficou sem solução".
Foi ela que pegou o defeito do `MAX_CARRY` (ver Histórico).

## Integração com o repositório

```console
$ python3 -m src.forge.giochi_forge check --device rk3566-2gb \
      --project "Jogos/Colossal Cave Adventure/godot"
```

O `forge.json` declara `godot_version: 4.3`, GLES3 com fallback GLES2, 320 MB de
RAM e 48 MB de VRAM — os números medidos com folga. O jogo é **padrão do
sistema**: entra no rootfs por `buildroot/package/giochi-games/` (que lê
`assets/default-games.json`, sem repetir nomes de jogo) e no benchmark por
`make bench`. Ver [docs/GAMES.md](../../docs/GAMES.md).

## Melhorias pendentes

O jogo **funciona**, mas está abaixo do que os outros dois portes entregam. O
mantenedor marcou esta pasta para uma próxima rodada; a ordem abaixo é a que vale
atacar, do mais visível ao mais estrutural:

1. **Leitura da caverna.** O shader foi reescrito uma vez — a primeira medição
   dava luminância média **12,6/255** (mancha preta); agora lê em **38/255**, com
   piso iluminado em (128,106,83). Ainda falta o degrau entre corredor e sala
   grande, e relevo na parede de fundo.
2. **Experiência 3D.** A intenção registrada era "reestruturar o jogo e dar uma
   experiência 3D". Hoje o 3D é **perspectiva de um ponto de fuga** (polígonos e
   billboards em `Node2D`, [cave_view.gd](./godot/scripts/cave_view.gd)), não
   geometria real. Um `MeshInstance3D` com materiais unshaded custaria poucos
   draw calls e caberia no orçamento — é a mudança que mais muda a percepção.
3. **Sem mapa.** O original tinha mapa de papel; aqui o jogador se perde sem um
   minimapa das salas visitadas. O `visited` já é rastreado no modelo: é só
   desenho.
4. **Sem save.** Fechar o jogo perde a partida. O estado é serializável (campos
   planos), falta o arquivo em `user://`.
5. **Conteúdo pequeno.** 18 salas e 100 pontos contra as 430 do original.
   Ampliar é escrever `ROOMS`/`ITEMS` mais textos nos três idiomas — o teste de
   paridade pega o que faltar.
6. **Áudio mínimo**: 9 sons sintetizados, nenhum ambiente. Sem eco de caverna e
   sem passo diferente por tipo de piso.
7. **Nada rodado em aparelho.** Mali-G52 com Panfrost ≠ Mesa; só o `F3` no
   aparelho resolve.
8. **Não vira `.giochi` ainda**: falta o export template recompilado contra musl
   ([ADR-03](../../docs/architecture-v1.2.md)) — vale para os três jogos.

## Histórico

A pasta continha **só uma nota de três linhas** (as duas fontes acima e a
intenção de "experiência 3D"); o projeto foi escrito do zero a partir dela, no
mesmo padrão do [Arcade Volleyball](../Arcade%20Volleyball/Arcade%20Volleyball.md)
e do [Spacewar](../Spacewar/Spacewar.md). A nota foi preservada aqui, não
apagada.

Dois defeitos apareceram **medindo**, não lendo:

1. **A caverna não fechava.** A rota de referência parava no quinto tesouro com
   a mão cheia: `MAX_CARRY = 5` não cabe lampião + 5 tesouros. O teste de
   completude reprovou na hora; corrigido para 6 (o original permite, já que o
   lampião é obrigatório).
2. **Uma sala sem volta.** `queda` saía para `salao` e `grade`, e nenhuma das
   duas voltava — quem caísse ficava preso. O teste de simetria do mapa (toda
   saída tem volta) reprovou; `queda` agora só sai por `c: abismo`.

O bug do `MAX_CARRY` não aparece em cinco minutos de jogo olhando a tela: só
aparece quando alguém joga a caverna inteira.
