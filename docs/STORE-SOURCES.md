# Fontes de jogos para a Loja: comparação medida

**Estado: mapa de fontes avaliado em 2026-10-31.** Nada foi adotado ainda; este
documento existe para escolher com número na mão, e complementa
[ITCHIO.md](ITCHIO.md) (que conclui que o itch.io não serve como catálogo) e
[GAMES.md](GAMES.md) (a política de jogos padrão).

A pergunta era: *há outra fonte que possa ser usada nesse sentido?* —

## O critério que separa as fontes

O aparelho é **aarch64 (Cortex-A55)** com **musl**
(`BR2_TOOLCHAIN_BUILDROOT_MUSL=y`, ADR-03) e **700 MB** de orçamento de UI.
Toda fonte cai em uma de três classes:

1. **Binário aarch64 pronto** — baixa e roda. Melhor caso.
2. **Projeto/fonte** — exige recompilar (e é aqui que o template Godot musl do
   ADR-03 vira alavancagem, ver [ITCHIO.md](ITCHIO.md) §3.4).
3. **Catálogo sem arquitetura** — o itch.io hoje: baixa para descobrir que é
   x86.

## Resumo

| Fonte | Classe | Oferta aarch64 | Licença | Custo no aparelho |
|---|---|---|---|---|
| **PortMaster** | 1 (binário) | **≥488 ports**, `.aarch64` 430 MB no repo | MIT (ferramenta); **por port** | só o dos ports escolhidos |
| **Alpine Linux aarch64** | 1 (binário **musl**) | 24.049 pacotes, incl. RetroArch + 42 cores | por pacote | 5–244 MB por jogo |
| **TIC-80 + cartuchos** | 1 + 2 | build `arm64` 4,9 MB; cartuchos ~100 KB | **MIT** (engine); cartucho = do autor | engine 5 MB + KB por jogo |
| **Solarus** | 2 (fonte) | — | GPL-2.0 | **já está no Buildroot** |
| **Minetest/Luanti** | 2 (fonte) | — | LGPL-2.1 | **já está no Buildroot** |
| **itch.io** | 3 (sem arquitetura) | ~18 em 1,7 M | mista | — ver ITCHIO.md |

---

## 1. PortMaster — a fonte mais alinhada (binários aarch64 prontos)

`PortsMaster/PortMaster-New`, **MIT**, feito especificamente para portáteis ARM
(ArkOS, JELOS, muOS). Medido na árvore do repositório:

- **488 diretórios de port** em `ports/` — e a árvore vem **truncada** pela API,
  então o número real é maior;
- **430,7 MB de arquivos `.aarch64`**: são binários **pré-compilados para ARM**,
  não fonte. É a classe 1 — baixar e rodar;
- 1.294 MB de `.so`, 1.007 MB de `.pck` (dados de engine Godot), 453 MB de
  `.ogg`: o peso é dado, não código.

Por que é a melhor: **é ARM por construção**, e o repositório **já reservou a aba
para ele** (`APP:portmaster` em `layout.json`, registrado como não existente em
[APPS.md](APPS.md)).

**Cuidado de licença:** o PortMaster (a ferramenta) é MIT, mas **cada port tem
a licença do jogo original**. Muitos ports embrulham jogos comerciais que
exigem que o usuário forneça os arquivos originais. Curadoria obrigatória.

## 2. Alpine Linux aarch64 — o achado: é **musl**, como nós

Esta é a fonte que ninguém citou ainda e que casa com a nossa libc. O Alpine usa
**musl-libc**, exatamente a escolha do ADR-03. Medido nos índices oficiais
`v3.20/aarch64`:

- **24.049 pacotes** aarch64 (`main` 5.543 + `community` 18.506);
- binários linkados a `so:libc.musl-aarch64.so.1`.

### O que existe lá de jogo (medido, tamanho **instalado**)

| Pacote | Instalado | Licença |
|---|---|---|
| `easyrpg-player` | **3,4 MB** | **MIT** |
| `love` (LÖVE) | **3,8 MB** | **Zlib** |
| `retroarch` | **11,5 MB** | GPL-2.0-only |
| `pingus` | 0,8 MB (+13 MB dados) | GPL-2.0 |
| `supertux` | 5,0 MB | GPL-3.0 |
| `supertux-data` | **244,2 MB** | GPL-3.0 |
| `supertuxkart` | 17,4 MB | GPL-3.0 |
| `supertuxkart-data` | **717,3 MB** | GPL-3.0 |
| `openmw` | 26,1 MB | GPL-3.0 |
| `nethack` | 1,7 MB | NGPL |
| `minetest` | 8,9 MB | LGPL-2.1 + CC-BY-SA-3.0 |
| `ppsspp` | 16,4 MB | GPL-2.0 |
| `xonotic` (+data **1,1 GB**) | 0,6 MB | GPL-2.0 |

**Confirmação independente da análise do SuperTux:** o `supertux-data` do Alpine
tem **244 MB instalados**, o que bate com os **234 MB crus** que medi na árvore
do upstream ([SUPERTUX.md](SUPERTUX.md) §3) — e confirma que o problema é dado,
não código (o binário são 5 MB).

### Os 42 cores libretro — e o filtro de licença

RetroArch no Alpine é GPL-2.0, e há **42 pacotes `libretro-*`** aarch64.
Classificando por licença: **10 permissivos**, **32 copyleft/outros**.

| Core permissivo | Licença | Instalado |
|---|---|---|
| `libretro-2048` | Unlicense | 84 KB |
| `libretro-uzem` | MIT | 152 KB |
| `libretro-o2em` | Artistic-2.0 | 372 KB |
| `libretro-sameboy` | **MIT** | 412 KB |
| `libretro-handy` (Lynx) | Zlib | 880 KB |
| `libretro-core-info` | MIT | 1,1 MB |
| `libretro-craft` (Minecraft-like) | **MIT** | 1,1 MB |
| `libretro-mgba` (GBA) | MPL-2.0 | 3,0 MB |
| `libretro-mrboom` | MIT | 8,2 MB |
| `libretro-database` | MIT | **336 MB** |

Fora da lista permissiva ficam `beetle-psx`, `bsnes`, `gambatte`, `nestopia`,
`desmume`, `melonds`, `ppsspp` etc. — todos GPL. Note que retro **não é
"jogo"**: o emulador é ferramenta; a questão de licença é do *core*, e a ROM é
do usuário. O contrato de jogo padrão de hoje (`runtime: "retroarch"` existe no
schema) já prevê o caminho — mas **fase 2** pela
[Lista Consolidada](<Lista Consolidada de Aplicativos do Giochi Konsol OS (v1.2).md>).

### O risco honesto desta fonte

Binário do Alpine **não é** binário nosso. Ele espera o musl do Alpine (versão
específica) e as libs do Alpine. Musl tem ABI estável, mas *não* se pode
afirmar que roda no nosso rootfs **sem testar** — e o jeito certo de verificar é
pegar um pacote e rodar num aparelho ou num chroot musl aarch64.

A alternativa sem risco é **usar o Alpine como referência de patch e receita**,
não como binário: se o Alpine consegue compilar `love`/`retroarch`/`easyrpg`
para musl aarch64, o `APKBUILD` dele é o mapa exato do que o Buildroot precisa —
mesma função dos ports do Alpine para outras distros.

## 3. TIC-80 + cartuchos — a fonte de jogos mais barata que existe

`nesbox/TIC-80`: **MIT**, C, 6.180 estrelas, fantasy console (estilo PICO-8).
Medido no release `v1.4.1`:

| Asset | Tamanho |
|---|---|
| `tic80-v1.4-linux-arm64.zip` | **4,9 MB** |

Cartucho `.tic` medido no próprio repositório: **18 KB a 100 KB**.

Por que é excelente **para a Loja**: um cartucho é **um arquivo pequeno**, e o
catálogo de cartuchos gratuitos é grande (itch.io incluso). Cem jogos ocupam
**~10 MB**. É o oposto do SuperTux (244 MB por um jogo).

Ressalva: os cartuchos do TIC-80 **não têm licença uniforme** — cada autor
decide. Para *coleção curada* funciona; para *catálogo aberto*, exige o mesmo
filtro de licença de qualquer outra fonte.

## 4. O que o Buildroot já tem (custo marginal zero de receita)

Conferido por caminho na árvore 2024.02.x: `package/solarus` **existe** e
`package/minetest` **existe**. `retroarch`, `libretro`, `love`, `easyrpg-player`
e `tic80` **não existem** (404).

Ou seja: dois jogos/engines saem **com receita pronta**, só ligando símbolo no
defconfig — e o resto exige escrever pacote (com o `APKBUILD` do Alpine como
referência).

---

## Recomendação

Ordem por **esforço × retorno**, dado o teto de 700 MB:

1. **PortMaster** — binários aarch64 prontos, aba já reservada; exige curadoria
   de licença por port. É a fonte mais alinhada ao propósito da Loja.
2. **TIC-80** — 5 MB de engine e cartuchos de KB: o melhor uso do orçamento de
   armazenamento que existe. Ótimo para "muitos jogos pequenos".
3. **Alpine como referência** — usar os `APKBUILD` para levar `love`,
   `easyrpg-player` e `retroarch` ao Buildroot; **testar** binário antes de
   prometer que roda (o risco do §2).
4. **Solarus e Minetest** — ligar no defconfig, custo quase zero.
5. **itch.io** — só como coleção curada de projetos Godot, depois do template
   musl (ver [ITCHIO.md](ITCHIO.md) §3.4).
6. **SuperTux/SuperTuxKart** — **fora**: 244 MB e 717 MB de dados. Não é
   questão de licença, é de espaço.

O que **não** muda: nada disso vira "jogo padrão" sem licença permissiva (ou
permissão do autor) e sem bench — a regra de [GAMES.md](GAMES.md) continua.

## Como reconferir

```sh
# Alpine aarch64: indice oficial, com licenca e tamanho instalado por pacote
curl -sLO https://dl-cdn.alpinelinux.org/alpine/v3.20/community/aarch64/APKINDEX.tar.gz
tar xzf APKINDEX.tar.gz APKINDEX   # campos: P:nome L:licenca I:tam D:deps

# PortMaster: quantos ports e quanto e binario aarch64
curl -sL "https://api.github.com/repos/PortsMaster/PortMaster-New/git/trees/main?recursive=1" -o pm.json
#   contar paths em ports/<nome>; somar blobs *.aarch64

# TIC-80: tamanho do build arm64
curl -s https://api.github.com/repos/nesbox/TIC-80/releases/latest | grep -o '"name": "tic80[^"]*arm64[^"]*"'

# Buildroot tem o pacote? (sondar por CAMINHO, nunca por listagem -- ver AGENTS.md 3.49)
curl -s -o /dev/null -w '%{http_code}\n' \
  https://api.github.com/repos/buildroot/buildroot/contents/package/solarus?ref=2024.02.x
```

**Duas armadilhas de medição, para não repetir:**

- A API de *tree* do GitHub **trunca** e avisa (`truncated: true`) — o PortMaster
  veio truncado, então "488" é **piso**, não total. E ela devolve um JSON de
  ~20 MB: salvar em arquivo antes de ler, senão a resposta corta no meio e o
  `json.load` explode com `Expecting property name` (aconteceu).
- `APKINDEX` usa `<campo>:<valor>` (um caractere), não `chave=valor`. Parsear
  com o separador errado devolve **0 pacotes** sem erro.
