# SuperTux: análise de viabilidade

**Estado: avaliado e NÃO adicionado** (decisão do mantenedor, 2026-10-31 —
"só registrar a análise"). Este documento existe para que a próxima sessão não
refaça a pesquisa, e para que a decisão seja reversível com os números na mão.

Pergunta original: *"todas as versões ficam **40 fps** — isso não fica ruim para
jogar? veja a viabilidade."*

Resposta curta: **40 fps não estraga o jogo** (o motor tem passo lógico fixo,
abaixo). O que impede a adoção hoje são **tamanho**, **licença** e o
**contrato de jogo padrão** — três coisas independentes de desempenho.

| Dimensão | Veredito |
|---|---|
| 40 fps é jogável | **sim** — física independente do desenho (§1) |
| Cabe na imagem | **não** — 188 MB contra partição de 160 MB (§3) |
| Licença permite jogo padrão | **não** — GPL-3.0+ e CC-BY-SA (§4) |
| Encaixa no contrato de jogo padrão | **não** — binário nativo, não Godot (§5) |
| Roda no nosso stack DRM/KMS + GLES | **sim, com patch** no pacote (§2) |

---

## 1. Os 40 fps: por que não é problema

SuperTux 0.6.3 separa **passo lógico** de **desenho**. O laço está em
`src/supertux/screen_manager.cpp` (`ScreenManager::loop_iter`), lido no
tag `v0.6.3`:

```cpp
elapsed_ticks += ticks - last_ticks;
if (elapsed_ticks > ms_per_step * 8) elapsed_ticks = 0;   // carga de nível

if (elapsed_ticks < ms_per_step && !g_debug.draw_redundant_frames) {
    SDL_Delay(ms_per_step - elapsed_ticks);   // ainda não é hora de um passo
    return;
}

int steps = elapsed_ticks / ms_per_step;
int max_steps_per_frame = ceilf((1.0f / fps) / seconds_per_step);
if (max_steps_per_frame < 2) max_steps_per_frame = 2;
if (max_steps_per_frame > 4) max_steps_per_frame = 4;     // ~16 FPS
steps = std::min<int>(steps, max_steps_per_frame);

for (int i = 0; i < steps; ++i) {
    float dtime = seconds_per_step * m_speed * speed_multiplier;
    process_events(); update_gamelogic(dtime); elapsed_ticks -= ms_per_step;
}
```

O passo é fixo: `LOGICAL_FPS = 64.0` em `src/supertux/constants.hpp`, e
`ms_per_step = (Uint32)(1000.0f / LOGICAL_FPS)` = **15 ms** (o `static_cast`
trunca 15,625 — o passo efetivo é 15 ms, ou **66,7 passos/s**, não 64: detalhe
que só importa quando alguém for cronometrar o jogo).

Consequência com o desenho a 40 fps (25 ms por quadro):

| fps do desenho | passos por quadro | `max_steps_per_frame` | velocidade do jogo |
|---|---|---|---|
| 60 | 1,11 (alterna 1 e 2) | 2 | correta |
| **40** | **1,67 (alterna 1 e 2)** | **2** | **correta** |
| 30 | 2,22 | 3 | correta |
| 20 | 3,33 | 4 | correta |
| 16,7 | 4,00 | 4 (teto) | correta (no limite) |
| 15 | 4,44 → corta em 4 | 4 | **10% mais lento** |

Ou seja: **abaixo de ~16,7 fps o jogo desacelera de propósito**, e o comentário
do próprio fonte diz por quê — *"when the game is very laggy, it should slow
down instead of calculating lots of steps at once so that the player can still
control Tux reasonably"*. A 40 fps estamos muito longe disso: o teto calculado
é 2 e o necessário é 1,67.

**O que 40 fps realmente custa:** fluidez da rolagem horizontal (judder). Não
muda pulo, não muda colisão, não muda tempo de reação, não muda a velocidade do
Tux. Um jogo de plataforma é jogável assim — o que incomoda é *irregularidade*
de quadro, não a média.

**O que não dá para afirmar:** o fps real no aparelho. Nada rodou no Mali-G52
(Panfrost) nem no Mali-G31 (Lima); a estimativa otimista é que um jogo 2D de
2018 num GPU de 2021 passe de 60 a 720p, mas é estimativa.

---

## 2. Plataforma: viável com patch no pacote

Boa notícia: **o Buildroot 2024.02 já tem o pacote** (`package/supertux`,
versão **0.6.3**). Má notícia: como vem, ele **não seleciona** no nosso
defconfig.

| Exigência do `Config.in` | Nossa imagem |
|---|---|
| `depends on BR2_PACKAGE_XORG7` (GLX) | **não temos X** — DRM/KMS direto |
| `depends on BR2_PACKAGE_HAS_LIBGL` (GL desktop) | só **GLES/EGL** (`MESA3D_OPENGL_ES`, `MESA3D_OPENGL_EGL`, panfrost) |
| `.mk` fixa `-DENABLE_OPENGLES2=OFF` | precisa virar `ON` |
| `.mk` depende de `libgl` e `libglew` | desnecessários no caminho GLES2 |

O CMake **suporta** o caminho GLES2:

```cmake
option(ENABLE_OPENGLES2 "Enable OpenGLES2 support" OFF)
elseif(ENABLE_OPENGLES2 AND NOT EMSCRIPTEN)
  pkg_check_modules(GLESV2 REQUIRED glesv2)
  add_definitions(-DUSE_OPENGLES2)
```

`glesv2.pc` é exatamente o que o Mesa do nosso defconfig instala, e o SDL2 com
`KMSDRM` cria contexto por EGL/GLES. Então o patch é: `ENABLE_OPENGLES2=ON`,
`ENABLE_OPENGL=OFF`, e tirar `XORG7`, `libgl` e `libglew` do `Config.in`/`.mk`.

**Versão 0.7.0 (a atual) está fora por enquanto:** o `vcpkg.json` dela depende
de **SDL3**, `sdl3-image` e `sdl3-ttf`, e `package/sdl3` **não existe** no
Buildroot 2024.02 (404). Usar 0.7 exigiria trocar a versão do Buildroot — que
é decisão de plataforma, não de jogo.

Dependências que entrariam na imagem junto (as que ainda não temos): `boost`
(filesystem, locale, date_time, system), `openal`, `physfs`, `libvorbis`,
`libogg`, `libcurl`, `glm`, `libpng`, `zlib`, `sdl2_image`. Ordem de grandeza:
**~10 MB** de bibliotecas — pequeno perto do problema seguinte.

---

## 3. Tamanho: não cabe (este é o bloqueio duro)

`data/` do 0.6.3 medido pela árvore git: **234,0 MB em 4.061 arquivos**. Para
saber quanto disso sobra depois do SquashFS, a compressão foi **medida por
amostra** com `zstd -19` (mesma família do `-Xcompression-level 22` da imagem),
e aplicada aos totais exatos por extensão:

| Tipo | Cru | Razão medida | No SquashFS |
|---|---|---|---|
| `.ogg` (música e sons) | 88,0 MB | **1,09x** (2 amostras) | **80,5 MB** |
| `.png` (imagens) | 56,4 MB | **1,10x** (2) | **51,4 MB** |
| `.stl` (níveis, texto) | 48,3 MB | **23,89x** (2) | 2,0 MB |
| `.otf` (fonte CJK) | 15,7 MB | **1,33x** (1) | 11,9 MB |
| `.wav` | 8,9 MB | **1,99x** (2) | 4,5 MB |
| `.po` (traduções) | 6,3 MB | **5,71x** (2) | 1,1 MB |
| `.ttf` `.stwm` `.jpg` outros | 10,4 MB | 1,20x (estimado) | 8,6 MB |
| **Total** | **234,0 MB** | — | **≈ 160 MB** |

A conta que decide:

```
rootfs atual (medido no build real)   28 MB
data/ do SuperTux (comprimido)      +160 MB
                                   ---------
                                   ≈ 188 MB   >  partição system-a = 160 MB
```

**Não cabe**, e nenhum ajuste de SquashFS salva: **132 MB dos 160 são `.ogg` e
`.png`, que já estão comprimidos**. Os níveis (`.stl`) comprimem 24x e somem na
conta — o peso é áudio e imagem.

Os três caminhos, com o custo de cada um:

| Caminho | Efeito | Custo |
|---|---|---|
| Em `/user` (partição de 512 MB, hoje vazia) | imagem assinada intacta | precisa `runtime: "native"` (§5) |
| Na imagem **sem música** (`.ogg` = 80,5 MB) | rootfs ≈ 108 MB, cabe com ~52 MB de folga | jogo sem trilha sonora |
| Na imagem completo | `system-a` de 160M para 256M, A/B | **+192 MB** na imagem; o `.xz` publicado iria de **41 MB para ~170 MB** (estimativa: áudio e imagem não comprimem) — mata o argumento de imagem pequena |

O último número é o mais caro do projeto: o `.xz` de 41 MB é o artefato de
distribuição, e 132 MB de dados incompressíveis apareceriam quase inteiros nele.

---

## 4. Licença: o mesmo muro do Shattered Pixel Dungeon

O pacote declara:

```
SUPERTUX_LICENSE = GPL-3.0+ (code), CC-BY-SA-2.0, CC-BY-SA-3.0, CC-BY-SA-4.0,
                   GPL-2.0+ (images music sounds)
```

Código **e** assets são copyleft — não é o caso de "o código é GPL mas a arte é
livre". A análise é a mesma já registrada em
[Jogos/Shattered Pixel Dungeon/Shattered Pixel Dungeon.md](<../Jogos/Shattered Pixel Dungeon/Shattered Pixel Dungeon.md>):
o problema não é crédito nem "contaminação" (agregado não estende a licença),
é o **§6, *Installation Information*** — entregar binário GPL para um
"User Product" exige dar meios de instalar versão modificada, e a dispensa só
vale para ROM. Rootfs SquashFS assinado Ed25519 com a chave no projeto → a
dispensa não vale.

Instalar em `/user` resolve isso do mesmo jeito que resolveria para o
Shattered: o usuário troca o binário na partição gravável, e a
*Installation Information* é um procedimento público sem chave.

Ver a política em [docs/GAMES.md](GAMES.md) e a decisão em
[assets/default-games.json](../assets/default-games.json) → `excluded`.

---

## 5. Contrato de "jogo padrão": SuperTux não é Godot

[schemas/default-games.schema.json](../schemas/default-games.schema.json) exige,
para a seção `games`:

- `runtime` ∈ `godot` | `retroarch` | `proton`;
- `bench_scene` e `bench_args`;
- no projeto: `scripts/bench.gd` e `tests/run_tests.sh` executável.

SuperTux é **binário nativo C++** — nenhum dos três `runtime` serve, e não há
`bench.gd`. `tools/check-default-games.py` reprovaria, com razão: hoje "jogo
padrão" significa **teste e benchmark do aparelho**, medido pelo
[painel `F3` e pelo `budget_probe`](../Jogos/Spacewar/Spacewar.md).

Para adotar SuperTux seria preciso, no mínimo: `runtime: "native"` na spec, no
schema e no Launcher; e um probe de bench nativo (medir RSS/VRAM medindo o
processo, não pelo `Bench` do Godot). É trabalho de plataforma, não de jogo.

---

## 6. Se algum dia for adiante

Em `/user`, que resolve licença e tamanho de uma vez:

1. Patch no pacote Buildroot: `ENABLE_OPENGLES2=ON` e cair `XORG7`/`libgl`/`libglew`.
2. Instalar `supertux2` + `data/` em `/user/games/supertux` (não no rootfs
   assinado), com as ~10 MB de bibliotecas no rootfs.
3. `runtime: "native"` em `schemas/metadata.schema.json`, em
   `assets/default-games.json` e no carregamento do Launcher, mais um probe de
   bench nativo.
4. Medir no aparelho: fps real, RAM e VRAM. **Aí** a pergunta de 40 fps deixa
   de ser análise de fonte e passa a ser medida.

Enquanto isso não acontece, ele **não** entra em `assets/default-games.json` —
nem em `excluded`, porque `excluded` é para jogo que foi avaliado como jogo
padrão e barrado por licença; SuperTux foi barrado também por tamanho e por
contrato.

---

## 7. Como reconferir

```sh
# versão e exigências do pacote (Buildroot 2024.02.x)
curl -s https://raw.githubusercontent.com/buildroot/buildroot/2024.02.x/package/supertux/supertux.mk
curl -s https://raw.githubusercontent.com/buildroot/buildroot/2024.02.x/package/supertux/Config.in

# passo lógico e teto de passos por quadro
curl -s https://raw.githubusercontent.com/SuperTux/supertux/v0.6.3/src/supertux/screen_manager.cpp | grep -n "ms_per_step\|max_steps_per_frame"
curl -s https://raw.githubusercontent.com/SuperTux/supertux/v0.6.3/src/supertux/constants.hpp | grep -n LOGICAL_FPS

# tamanho do data/ (soma exata dos blobs, sem baixar o tarball)
#   GET /repos/SuperTux/supertux/git/trees/v0.6.3?recursive=1  -> somar data/

# razão de compressão por extensão
zstd -19 -q -f -o /tmp/x.zst <arquivo>   # cru / comprimido
```

**Cuidado ao conferir pacotes do Buildroot:** a API de conteúdo do GitHub
**trunca em 1000 entradas**, e `package/` tem mais que isso. Listar e filtrar
nessa resposta dá falso negativo (foi assim que `supertux` e `sdl2` "não
existiram" na primeira conferência). Sondar **por caminho**:

```sh
curl -s -o /dev/null -w "%{http_code}\n" \
  https://api.github.com/repos/buildroot/buildroot/contents/package/supertux?ref=2024.02.x
```
