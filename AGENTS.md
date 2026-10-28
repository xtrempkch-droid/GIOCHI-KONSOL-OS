# AGENTS.md — direcionamentos para agentes de IA

Este arquivo existe para que agentes de IA (e pessoas) que mexem neste
repositório **deixem rastro**: o que foi feito, o que foi verificado, o que é
hipótese e quais erros não devem ser repetidos. Leia antes de editar e
**acrescente uma entrada** em [Registro de agentes](#registro-de-agentes) ao
terminar.

Idioma do repositório: **português (pt-BR)** — código, comentários, commits,
documentação e mensagens de erro.

---

## 0. ONDE PARAMOS

Estado detalhado por sessão em [docs/HANDOFF.md](docs/HANDOFF.md); o que falta e
em que ordem em [docs/roadmap.md](docs/roadmap.md); inventário de cada app
(console × PC) e o que não existe em [docs/APPS.md](docs/APPS.md).

| O quê | Estado |
|---|---|
| Núcleo em C/C++ (supervisor, sockets, SHA-256, Ed25519, JSON, hotkey) | **pronto e testado** |
| Buildroot `BR2_EXTERNAL`, 5 aparelhos, genimage A/B | **config validado** contra Buildroot 2024.02.13 real |
| Imagem de cartão: `.img` 936 MB → **`.img.xz` 27,8 MB** | **medido** no layout real (§3.21); nunca bootada |
| U-Boot mainline na X55 (`powkiddy-x55-rk3566` + BL31/TPL do rkbin) | **config validado** |
| USB: gadget composto (MTP + rede/SSH + Mass Storage) | **pronto**, testado contra configfs falso |
| Modo Desenvolvedor (§4.6.1): SSH via USB desligado por padrão | **pronto**, testado |
| Launcher: motor de layout, tema, `settings.json`, overlay 2 níveis, biblioteca | **pronto** |
| Launcher: **desenho dos widgets** no backend SDL2 | **esboço** (`TODO(v1.2)` em `render.cpp`) |
| `giochi-musicd`: protocolo e socket | **pronto**; decodificação PCM/ALSA **pendente** |
| Imagem gerada e bootada em hardware | **nunca** — ver [§4](#4-o-que-não-foi-verificado) |

**Nunca repita (custou tempo de build ou passou silencioso):** leia
[§3 Armadilhas](#3-armadilhas-já-resolvidas-não-repita) antes de qualquer coisa.

---

## 1. Objetivo do projeto

Sistema operacional próprio (não é distro) para portáteis ARM **RK3326/RK3566**,
com foco em **Powkiddy X55/X35S/X35H**. Premissas que não se negociam:

- **Boot rápido, RAM baixa**: 1–2 GB, UI ≤ 60 MB em repouso, VRAM de UI ≤ 4 MB.
- **Sem systemd, sem PulseAudio, sem desktop**: BusyBox init + `giochi-init`,
  ALSA direto (`dmix`), Gamescope/DRM-KMS + SDL2.
- **Sem toque**: entrada é gamepad evdev (`/etc/giochi/buttons.map`).
- **Rootfs imutável**: A/B SquashFS assinado (Ed25519 + SHA-256), `/user` é a
  única partição gravável de dados.

A especificação normativa é [docs/architecture-v1.2.md](docs/architecture-v1.2.md)
(+ [adendo 4.6.1](<docs/Registro na Spec (v1.2 — Adendo à Seção 4.6).md>)).
**Não reescreva a spec**: referencie por ID (`ADR-NN`, `§N.N`).

---

## 2. Mapa do repositório

```text
src/common/     JSON, log, util, sockets, SHA-256, Ed25519, FIFO, hotkey
src/init/       giochi-init: supervisor, control.sock, power, game, device_profile
src/launcher/   layout, theme, settings, overlay, input/evdev, render (SDL2 | null)
src/musicd/     giochi-musicd (socket texto + ALSA)
src/thumbgen/   thumbnails 152x85 RGBA
src/forge/      giochi-forge (PC): perfil, classificação, empacotamento (Python)
assets/         layout.json, theme.json, defaults.json, buttons.map, bootlogo/
assets/devices/<id>/    override de layout por aparelho
schemas/        JSON Schema (metadata, forge, settings, layout)
scripts/        giochi-usb-mode + init.d/{S05..S99}
buildroot/      BR2_EXTERNAL
  board/giochi/
    devices/*.conf           FONTE ÚNICA de dados por aparelho
    defconfig/base-<soc>.defconfig   templates (contêm os placeholders)
    rootfs_overlay/          /etc de fábrica, rcS/rcK, lib.sh
    post-build.sh            popula TARGET_DIR (falha se faltar script de init)
    post-image.sh            genimage A/B, extlinux.conf, boot.vfat
  package/giochi-*           pacotes (init, launcher, musicd, mtpd, assets, tools)
  configs/*_defconfig        GERADOS — não edite
tools/          build-image, gen-defconfigs, bootlogo, screenshots, pack/sign/verify
tests/          unitários C/C++, integração shell, forge e screenshots (Python)
docs/           arquitetura, hardware, USB, build, QA, handoff, roadmap
```

**Regra de ouro:** cada dado de aparelho vive em **um** lugar
(`devices/<id>.conf`). Se você precisa do mesmo dado em dois arquivos, gere o
segundo.

---

## 3. Armadilhas já resolvidas (não repita)

Cada item abaixo aconteceu de verdade neste repositório. Vários são
**silenciosos** — o build passa, o teste passa, e o defeito só aparece no
aparelho ou no CI.

### 3.1 `.gitignore` casa com NOME DE DIRETÓRIO

Um `*.d` "inofensivo" (arquivos de dependência do compilador) engoliu
`scripts/init.d/` **e** `buildroot/.../etc/init.d/` inteiros: 11 arquivos nunca
foram versionados. Localmente tudo funcionava (os arquivos existiam), o CI
recebia um checkout sem a sequência de boot e falhava só no lint de links. Pior:
`post-build.sh` fazia `[ -f x ] && install`, então a imagem compilaria e **não
iniciaria**.

- **Não use** padrões soltos no `.gitignore` (`*.d`, `*.o`, `*.a`, `*.so`).
  Objetos ficam em `build/` e `output/`, já ignorados.
- Guarda: `tools/check-tracked.sh` (em `make lint`) falha se um arquivo do
  projeto estiver ignorado e exige os 11 scripts de init versionados.
- `post-build.sh` agora trata script de init ausente como **erro fatal**.

### 3.2 Kconfig do Buildroot DESCARTA símbolo inexistente em silêncio

`BR2_PACKAGE_LIBMPG123=y` não existe (o pacote é `mpg123`). Nenhum aviso: o
símbolo é descartado, o pacote não é compilado e **a imagem sai sem MP3**.

- Símbolo que virou *legacy* é pior: liga `BR2_LEGACY` e o build morre em
  `Makefile.legacy` com uma mensagem que **não diz qual símbolo** é o culpado.
  Caso real: `BR2_PACKAGE_RFKILL` foi removido → use
  `BR2_PACKAGE_UTIL_LINUX_RFKILL`.
- Guarda: `tools/check-buildroot-symbols.py` (em `make lint`, no workflow de
  imagens e no início de cada build em `build-image.sh`) lista `LEGACY` e
  `INEXISTENTE` por defconfig.
- Versão de referência: **Buildroot 2024.02.x**. Se mudar de versão, rode o
  checker contra a árvore nova.

### 3.3 Gerador de defconfig: marcador de bloco também casava em comentário

O `awk` de `gen-defconfigs.sh` usava `/@DTS_LINES@/` sem âncoras; o comentário
`# Placeholders: ... @DTS_LINES@` no topo do template também casava, e o bloco
de DTS saía **duplicado** (`warning: override: reassigning to symbol`).

- Marcadores de bloco só casam quando estão **sozinhos na linha** (`^...$`).
- Nunca cite o nome de um marcador em comentário de template.
- Guarda: `make lint` exige exatamente **um** bloco de DTS por defconfig.

### 3.4 `set -e` + `$(teste && cmd)` aborta o script

Em `gen-defconfigs.sh`, `x="...$([ -n "$v" ] && printf ...)"` abortava silenciosamente
(exit 1) quando o teste era falso, porque `set -e` vê o status da substituição.
Use `if` explícito.

### 3.5 POSIX: `.` (source) que falha ABORTA shell não-interativo

`. /lib.sh 2>/dev/null || true` **não** protege — o `||` não é avaliado porque o
shell encerra na falha do `.`. Em script de init isso mata o boot script.
Padrão correto, usado em todos os `scripts/init.d/*`:

```sh
if [ -r /etc/giochi/lib.sh ]; then
    . /etc/giochi/lib.sh
fi
```

### 3.6 A raiz é SquashFS: `/etc` é SOMENTE LEITURA

Qualquer script que escreva em `/etc` em runtime falha na imagem (embora
funcione no host). Foi o caso do `S62sshd` instalando `authorized_keys`.

- `post-build.sh` cria `/etc/dropbear` como **symlink** para
  `/user/.ssh/dropbear` (partição gravável).
- Chaves de host e `authorized_keys` vão para `/user`.
- Antes de escrever qualquer coisa em runtime, verifique se o caminho cai em
  `/user` (ou `/run`).

### 3.7 `make test` precisa de `all`

A suíte de integração executa o `giochi-init` **real**, não um stub. Num clone
limpo, `make test` sozinho não o tinha compilado e falhava. O alvo agora é
`test: all $(TEST_BINS)`. Se criar uma suíte que depende de um binário do
sistema, ela entra nessa conta.

### 3.8 U-Boot mainline TEM defconfig para a X55 (desde v2024.07)

Documentação antiga deste repositório afirmava o contrário. Verificado nas tags:
v2024.01 e v2024.04 → **404**; v2024.07, v2024.10, v2025.01 →
`configs/powkiddy-x55-rk3566_defconfig` **existe**. X35S/X35H **não** têm
defconfig em nenhuma delas.

- Sem `BL31` + `DDR/TPL` do rkbin o SPL do RK3566 não inicializa a memória: a
  placa liga e morre, sem mensagem clara.
- Guarda: `tools/check-uboot.py` valida defconfig, símbolos do
  `uboot.fragment` e os blobs de TPL/BL31 por aparelho.

### 3.9 `device_profile.json` descreve ESTADO, não capacidade

`"developer": { "ssh_usb": … }` é lido de `/user/settings.json` em tempo de
execução. `false` quer dizer "o Modo Desenvolvedor está desligado agora", **não**
"o aparelho não suporta". Não inverta essa semântica.

### 3.10 `settings.json` é propriedade do Launcher (§4.9.5)

O `giochi-init` **não** escreve nesse arquivo. O fluxo é: Launcher grava
`settings.json` → envia comando (`DEV_MODE ON`, `USB_MODE MASS`, …) ao
`control.sock` → init relê o arquivo e aplica. Scripts de init releem
`settings.json` por conta própria (é o contrato do gate do Modo Desenvolvedor).

### 3.11 Imagens de documentação NÃO são capturas de tela

`docs/screenshots/*.png` são **renders do layout declarado** nos assets, gerados
por `tools/make-screenshots.py`. O desenho dos widgets no SDL2 ainda é esboço.
Se você escrever material de divulgação, não as apresente como screenshot de
aparelho. `tests/test_screenshots.py` amarra as imagens ao tema e ao splash.

### 3.12 Ferramenta de PC não vai para o rootfs

`buildroot/package/giochi-tools/giochi-tools.mk` instalava `giochi-pack.sh` em
`/usr/bin` da imagem. Ele é ferramenta de **autor/publicador**, e depende de
`GNU tar --format=ustar`, `--owner/--group` (opção inexistente no BusyBox),
`-r` (append, também ausente) e `jq` (não empacotado). Ou seja: um comando que
falhava **sempre**, ocupando espaço no SquashFS.

- **No console** (elegível ao rootfs): `giochi-verify` (§4.3),
  `giochi-extract` e `giochi-thumbgen` (§4.2).
- **No PC**: `giochi-pack.sh`, `giochi-sign.sh`, `giochi-keys.sh`,
  `giochi-forge` (`src/forge`), todos os `tools/*.py`, `build-image.sh` e os
  scripts de stress/bench.
- Guarda: `tools/check-image-contents.sh` (em `make lint`) falha nos dois
  sentidos — ferramenta de PC instalada no alvo **e** binário obrigatório do
  console faltando.
- `giochi-pack.sh` agora recusa rodar com tar não-GNU, com mensagem explícita.

### 3.13 Ambiente local ≠ runner do CI

Nesta máquina (GCC 15, CMake 4.2) o build do Buildroot quebra em pacotes de
host (`host-m4 1.4.19`, `host-lzo`). **Não é bug de configuração**: o runner usa
GCC 13 / CMake 3.28. Não "conserte" isso mexendo no defconfig; valide símbolos
com `check-buildroot-symbols.py` e deixe o build completo para o CI.

---
### 3.14 `BR2_LINUX_KERNEL_DEFCONFIG="defconfig"` não existe

Arm64/ARM têm **um único** defconfig no kernel (`arch/arm64/configs/defconfig`)
e o Buildroot **acrescenta** `_defconfig` ao valor
(`linux.mk`: `LINUX_KCONFIG_DEFCONFIG = <valor>_defconfig`). Resultado:
`make defconfig_defconfig` e o build morre com *"Can't find default
configuration arch/arm64/configs/defconfig_defconfig"* — **depois** de baixar o
kernel e compilar todo o host (≈40 min no CI).

- Correto: `BR2_LINUX_KERNEL_USE_ARCH_DEFAULT_CONFIG=y` (o help do Buildroot
  diz "such as ARM64") e **nenhum** `BR2_LINUX_KERNEL_DEFCONFIG`.
- `BR2_LINUX_KERNEL_DEFCONFIG` espera o nome **sem** o sufixo (ex.:
  `bcm2711`), e só vale com `USE_DEFCONFIG=y`.
- Guarda: `tools/check-buildroot-symbols.py` valida o **valor** (não só a
  existência do símbolo) e roda em `make lint` mesmo sem `BUILDROOT_DIR`.

### 3.15 Nada gerava o `boot.scr` que o post-image exige

`mkbootimg.sh` monta a partição A e **exige** `boot.scr` (`exit 1` se faltar),
mas nenhum símbolo do defconfig o gerava. O `post-image.sh` só avisava
("boot.vfat nao gerado") e o `genimage` morria depois com *"cannot find image
boot.vfat"* — escondendo a causa real.

- `boot.scr` vem de `BR2_PACKAGE_HOST_UBOOT_TOOLS_BOOT_SCRIPT=y` +
  `..._BOOT_SCRIPT_SOURCE="$(BR2_EXTERNAL_GIOCHI_PATH)/board/giochi/boot.cmd"`.
- `mcopy`/`mmd`/`mkfs.vfat` vêm de `BR2_PACKAGE_HOST_MTOOLS` e
  `BR2_PACKAGE_HOST_DOSFSTOOLS`. O Buildroot põe `$(HOST_DIR)/bin` no `PATH`
  dos scripts (`BR_PATH`), então **não** dependa de `apt` na máquina.
- `post-image.sh` agora trata boot.vfat ausente como **fatal**, com as dicas do
  que configurar.


### 3.16 U-Boot: `mkeficapsule` precisa de uuid e gnutls do host

`make tools` do U-Boot compila `mkeficapsule`, que inclui `uuid/uuid.h` e
`gnutls/gnutls.h`. Em CI isso passa **de graça** porque a imagem do runner tem
`uuid-dev`/`gnutls-dev` instalados; numa imagem limpa o build morre em sequência:

```
tools/mkeficapsule.c:19:10: fatal error: uuid/uuid.h: No such file or directory
tools/mkeficapsule.c:21:10: fatal error: gnutls/gnutls.h: No such file or directory
```

- A correção é declarar as dependências de host no defconfig:
  `BR2_TARGET_UBOOT_NEEDS_UTIL_LINUX=y` e `BR2_TARGET_UBOOT_NEEDS_GNUTLS=y`
  (o `uboot.mk` adiciona `host-util-linux`/`host-gnutls` e os headers entram em
  `HOST_CFLAGS`).
- Emitidos por `gen-defconfigs.sh` em todo aparelho com U-Boot mainline.
- Regra geral: se o build só funciona numa máquina "gorda", falta um símbolo
  `BR2_..._NEEDS_*` no defconfig. Descubra construindo num container limpo
  (`tools/docker-build.sh`).

### 3.17 `git ls-files` cita caminhos não-ASCII (o lint reprovava depois do build)

`tools/check-tracked.sh` comparava cada arquivo ignorado contra uma lista de
prefixos permitidos (`dist/*`, `build/*`, …). O git **cita** caminhos com
caractere não-ASCII — `"dist/.../Prøve.flac"` chega com aspas —, então o
`case` via um caminho começando em `"` e reprovava o lint. Só apareceu depois
do **primeiro build real** (que cria arquivos de teste do gcc/flac com nomes
latinos e hebraicos dentro de `dist/`).

- `git -c core.quotePath=false ls-files …` resolve a citação.
- Enumere com exclusão de pathspec (`':(exclude)dist'`) em vez de filtrar
  depois: o build real deixa ~800 mil arquivos em `dist/`, e varrer isso levava
  minutos a cada `make lint`. Agora roda em ~1 s.

### 3.18 O lint de links não olhava `Jogos/` (e não decodificava `%20`)

`tools/check-links.py` descobria `*.md` da raiz e de `docs/`. Os quatro
documentos de jogo em `Jogos/*/*.md` — com links relativos para
`../Arcade%20Volleyball/…`, para os assets e para a spec — **nunca** foram
verificados, embora o §6.5 deste arquivo dissesse que todo link relativo é. O
lint passava com link quebrado dentro deles.

Ao ligar a varredura apareceram dois problemas de uma vez: (a) a descoberta
faltando e (b) o `%20` não decodificado — o GitHub decodifica antes de resolver,
o script comparava o texto cru e acusava link **correto** de quebrado.

- Varredura agora inclui `Jogos/*/*.md` (25 arquivos, era 21).
- `urllib.parse.unquote` no caminho e na âncora.
- Testado ao contrário: um `_tmp_link.md` com `./nao-existe.md` fez o lint sair
  com 1; removido, volta a 0.

### 3.18 Disco: o runner do CI morre no meio do build, sem mensagem

Sintoma na run 40617882645: 69 minutos compilando, e o log **termina no meio de
uma linha** (uma continuação `\` de compilação do openssl). Nenhum
`No space left`, nenhum `Killed`, e os passos de limpeza (`Post Cache`,
`Post checkout`) **nunca rodaram**. Isso não é erro de build: é o runner
morrendo.

Medido neste repositório (`du -sh` de um build parcial, **antes** do mesa3d):

| Diretório | Tamanho |
|---|---|
| `dist/x55/build/build/` | **13 GB** (linux-6.12 4,1 GB; linux-headers 1,7 GB; gcc-final 2,2 GB; binutils 0,4 GB) |
| `dist/x55/build/host/` | 676 MB |
| `dist/x55/build/target/` | 146 MB |

O runner `ubuntu-24.04` tem ~**14 GB** livres em `/home/runner`. Um build
completo precisa de ~16–18 GB.

- **Libere disco antes de compilar**: remover `/usr/share/dotnet`,
  `/usr/local/lib/android`, `/opt/ghc`, `/opt/hostedtoolcache`, `/usr/share/swift`,
  `/usr/lib/jvm` etc. recupera ~20–25 GB (passo "Liberar espaco em disco" no
  `images.yml`).
- **Sem ccache** no CI: ele guarda uma cópia de cada objeto e custa GBs, para
  ganho pequeno (a chave já é invalidada por `hashFiles('buildroot/**')`).
- `build-image.sh` tem guarda: falha antes de compilar se houver menos de
  `GIOCHI_MIN_FREE_GB` (padrão 15) livres, com o número medido.
- Se o log terminar no meio de uma linha, **é disco** — não perca tempo lendo a
  compilação.

### 3.19 `genimage.cfg` declarava 8 MiB para um arquivo de 8,97 MiB

O `u-boot-rockchip.bin` medido no build real tem **9 401 856 B (8,97 MiB)** — o
`idbloader` vai em 32 KiB e o `u-boot.itb` entra a partir de 6 MiB. A partição
raw dizia `size = 8M`. O `genimage` aborta com *"image too big for partition"*
no **último** passo, depois de 1–3 h compilando U-Boot, kernel e rootfs.

- Região raw agora tem **16 MiB** e a partição de boot (80 MB) começa em
  **24 MiB**; sem isso o próximo arquivo grande volta a bater.
- `post-image.sh` tem `fits()`: compara cada artefato com o `size` do cfg e
  falha **antes** do genimage, dizendo "ocupa N MB, a partição tem M MB".
- `tests/test_image_layout.py` (em `make test` e `make lint`) refaz a conta em
  segundos, sem Buildroot/genimage. Testado ao contrário: voltar para 8M faz o
  teste falhar.
- **Regra geral**: toda partição do `genimage.cfg` é dimensionada pelo
  **artefato medido**, não pelo que parecia suficiente. Anote a medição no
  comentário, como as outras.

### 3.20 Tamanho de partição não pode viver em dois arquivos

O tamanho da partição C vinha de `USER_SIZE_MB=512` (dentro do
`post-image.sh`) e os outros do `genimage.cfg`. Mudar um sem o outro produzia
`user.ext4` maior que a partição (o genimage aborta) ou espaço morto em todo
cartão. Hoje o **cfg é a fonte única**: `part_size()`/`to_mb()` leem o `size`
escolhido, o `user.ext4` sai com exatamente esse tamanho e
`tests/test_image_layout.py` falha se o script ganhar um default próprio.

Vale para qualquer dado: se você precisar do mesmo número em dois lugares,
**gere o segundo** (foi a armadilha do `devices/*.conf` de novo).

### 3.21 O `.img` cru é ~90% zero; publique o comprimido

Com o layout atual o `sdcard.img` tem **936 MB** e não serve para distribuir
(GitHub, download, bom senso). O `post-image.sh` gera o artefato comprimido e o
`images.yml` publica **só** ele + `.sha256`.

Medições desta sessão (imagem com o layout real, kernel de 47 MB e SquashFS de
29 MB em cada slot):

| Compressão | Tamanho |
|---|---|
| `xz -9` (janela padrão de 64 MiB) | 55,1 MB |
| **`xz -9 --lzma2=dict=192MiB`** | **27,8 MB** |
| `zstd -19 --long=31` | equivalente em conceito (janela de 2 GB) |

O ganho **não** é do nível de compressão: é da **janela**. As partições A/B
guardam o mesmo SquashFS, a 160 MB de distância; com janela de 64 MiB o xz não
enxerga a segunda cópia e a recomprime (SquashFS já é comprimido, então ela
custa os 28 MB inteiros). A janela é derivada do `size` de `system-a` no cfg
(+32 MiB, teto de 512 MiB).

- Decomprimir custa ~192 MB de RAM **no PC** — nada disso toca o console.
- `xz -t` + `xz -dc | cmp` no `.img`: conferido, byte a byte igual.
- Fallback: se o `xz` da máquina recusar a janela, o script avisa e cai para
  `xz -9` (artefato ~2× maior) em vez de falhar o build.
- `GIOCHI_NO_COMPRESS=1` desliga (build local iterativo).
- **Não invente economia**: `usr/include`, `usr/share/{doc,man,info}`,
  `pkgconfig`, `*.a/*.la` e o `strip` de binários/.ko **já** saem no
  `target-finalize` do Buildroot 2024.02 (conferido no `Makefile` dele:
  `LINUX_MAKE_FLAGS += INSTALL_MOD_STRIP=1` quando `BR2_STRIP_strip=y`).
  Repetir no `post-build.sh` só gasta tempo — `strip` num `.ko` já stripado
  devolveu 0 byte de diferença.

### 3.22 Gettext: 9,5 MB que nenhuma peça do console usa

`usr/share/locale` era **9,5 MB crus / 2,1 MB no SquashFS**. A UI do Giochi não
usa gettext (as frases estão nos assets e no `locale.gd` dos jogos); sem os
`.mo`, `e2fsprogs`/`util-linux`/`alsa` caem no texto em inglês — e essas
mensagens vão para o `logger`, não para a tela.

O `post-build.sh` remove `usr/share/{locale,i18n,help,gtk-doc}` e loga quantos
KB saíram. `GIOCHI_KEEP_LOCALE=1` mantém (para investigar tradução de
ferramenta).

## 4. O que NÃO foi verificado

Trate como hipótese até alguém colar evidência (log, captura do aparelho):

- **Nenhuma imagem deste repositório foi gravada e bootada em hardware.** A
  cadeia está validada no nível de configuração (kconfig, símbolos, artefatos),
  não no aparelho.
- **X35S e X35H** nunca foram testados por quem escreveu este código. Não têm
  DTS upstream nem defconfig de U-Boot; dependem de `--fallback-dts` (imagem de
  teste) ou de DTS fornecido por você.
- **X35S/X35H: USB não ancora** (`failed to enable ep0out`, F12 em
  [docs/hardware-e-drivers.md](docs/hardware-e-drivers.md)) — provável hardware.
- **Gamepad** depende de `rocknix-joypad`, que **não está empacotado**. Sem ele
  os analógicos não aparecem.
- **Áudio**: decodificação PCM não implementada e o mux do RK817 precisa ficar
  em `HP` (em `SPK` o alto-falante fica mudo).

---

## 5. Como construir e verificar

Sempre rode o menor conjunto que cobre a mudança; antes de commitar, o padrão do
projeto é:

```sh
make            # binários de host
make test       # todas as suítes (unitárias, integração, Python)
make lint       # shell, JSON, defconfigs, links, arquivos versionados,
                # símbolos do Buildroot/U-Boot, py_compile das ferramentas
```

Com as árvores externas disponíveis, o lint fica mais forte (e o CI de imagens
roda isso antes de compilar):

```sh
export BUILDROOT_DIR=~/buildroot      # 2024.02.x
export UBOOT_DIR=~/u-boot             # opcional
export RKBIN_DIR=~/rkbin              # opcional
tools/check-buildroot-symbols.py
tools/check-uboot.py
tools/build-image.sh --check --all --fallback-dts   # preflight sem compilar
```

Defconfigs e imagens de documentação são **gerados**:

```sh
make defconfigs          # buildroot/configs/*_defconfig  (a partir de devices/*.conf)
make screenshots         # docs/screenshots/*.png         (a partir dos assets)
make bootlogo            # assets/bootlogo/*             (a partir de docs/logo.jpg)
```

Nunca edite à mão: `buildroot/configs/*_defconfig`, `assets/bootlogo/bootlogo.rgba`,
`assets/bootlogo/*/splash.bmp`, `docs/screenshots/*.png`.

### Suítes de teste (ver `tests/run_tests.sh`; o número envelhece)

| Suíte | O que cobre |
|---|---|
| `test_json`, `test_sha256` | núcleo comum |
| `test_layout`, `test_metadata`, `test_settings`, `test_buttonsmap` | Launcher |
| `test_hotkey`, `test_transition`, `test_musicd` | init / daemons |
| `lifecycle` (integração) | §2.2: SIGTERM+waitpid, reinício, SIGSTOP/SIGCONT |
| `usb-gadget` (integração) | configfs falso: rede sempre, ffs.mtp, mass, `off` |
| `devmode` (integração) | §4.6.1: gate desligado, IPs, bind só em `10.55.0.2:22`, `DEV_MODE` |
| `bootlogo`, `screenshots` (Python) | contrato dos assets e das imagens |
| `jogos-padrao` (Python) | licença, `bench.gd`, doc e benchmark dos jogos padrão |
| `imagem-layout` (Python) | `genimage.cfg`: u-boot cabe na região raw, partições não se sobrepõem, tamanho vem de uma fonte só |
| `forge` (Python) | perfil, classificação, empacotamento |

A lista canônica é [tests/run_tests.sh](tests/run_tests.sh); se você adicionar
uma suíte, registre-a lá **e** no respectivo alvo do `Makefile`.

---

## 6. Regras antes de editar

1. **Dado de aparelho**: altere `buildroot/board/giochi/devices/<id>.conf` e
   rode `make defconfigs`. PR que edita `buildroot/configs/` é recusado.
2. **Símbolo novo no defconfig**: confirme que existe na versão do Buildroot
   usada (`tools/check-buildroot-symbols.py`). Não confie na memória.
3. **Script de init**: `#!/bin/sh` + POSIX, sem bashisms; `sh -n` antes; `log()`
   com `logger` e fallback; nunca escrever em `/etc`.
4. **C++ do Launcher**: C++17, sem exceções atravessando `main`, `-Wall
   -Wextra` sem warning novo. `settings.json` só o Launcher escreve.
5. **Documentação**: número de suítes, contagem de arquivos e nomes de símbolos
   **envelhecem**. Prefira descrever a listar. Todo link relativo e todo
   `<img src>` são verificados por `tools/check-links.py`.
6. **Medição, não estimativa**: orçamento (RAM, VRAM, SquashFS, boot) só se
   declara com número medido; caso contrário é meta, e diga "meta".
7. **Nada de segredo** no repositório: chave privada, senha, token. O
   `.gitignore` já bloqueia `keys/`, `*.pem`, `pubkey.ed25519`.
8. **Commit** em pt-BR, imperativo, corpo explicando **causa** e **verificação**,
   com o trailer `Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>`
   quando houver assistência de IA.

---

## 7. Protocolo entre agentes

- **Uma entrada por sessão de trabalho** no registro abaixo, no fim.
- Registre: o que mudou, **como foi verificado**, o que ficou aberto e o que
  descobriu de errado na documentação (isso é o mais valioso).
- Se você corrigiu uma afirmação errada deste arquivo ou de outro doc, diga
  qual era a afirmação antiga — o próximo agente precisa saber que ela circulou.
- Se um build falhar, cole o **trecho relevante** do log na entrada; não resuma
  como "deu erro".
- Não declare feito o que não rodou. "Validado em config" ≠ "validado no
  aparelho".

---

## Registro de agentes

### 2026-10-27 (1) — Copilot (núcleo, buildroot, CI, USB)

- Construiu o núcleo (supervisor, launcher, musicd, forge, pacotes, buildroot
  externo com 5 aparelhos, CI, bootsplash).
- Armadilha encontrada: `buttons.map` e `README` como spec; ciclo de vida
  exigia `SIGTERM`+`waitpid` (auto-restart ressuscitava o Launcher no meio do
  jogo) — coberto por `lifecycle_test.sh`.
- Verificação: suítes de host + `build-image.sh --check`.

### 2026-10-27 (2) — Copilot (adendo 4.6.1: SSH via USB)

- Implementou o Modo Desenvolvedor: `developer.ssh_usb_enabled`, menu em dois
  níveis, comando `DEV_MODE ON|OFF`, IPs **console `.2` / PC `.1`** (a
  documentação de hardware tinha os dois invertidos), dropbear só em
  `10.55.0.2:22`, `udhcpd` removido (0 MB em repouso).
- Armadilha: `/etc` é SquashFS somente leitura → symlink `/etc/dropbear` →
  `/user/.ssh/dropbear`.
- Verificação: nova suíte `devmode_test.sh` (19 checagens, incluindo o
  `control.sock` real) + `make test` 15/15.

### 2026-10-27 (3) — Copilot (CI de lint quebrado)

- Sintoma: `link quebrado: README.md -> scripts/init.d/S40dongle` no CI.
- Causa raiz: `*.d` no `.gitignore` casando com **diretório** `init.d/`; 11
  arquivos nunca versionados. Local passava porque os arquivos existiam.
- Correções: `.gitignore`, `tools/check-tracked.sh`, `post-build.sh` fatal,
  `tools/check-links.py` descobrindo os `.md` por glob (a spec normativa não era
  verificada) e validação de `<img src>`.
- Verificação: clone limpo em `/tmp` reproduzindo o CI (`make lint`, `make test`).

### 2026-10-27 (4) — Copilot (build de imagens)

- Sintoma: `Makefile.legacy:9: *** "You have legacy configuration"` sem dizer o
  símbolo.
- Causa: `BR2_PACKAGE_RFKILL` removido do Buildroot. Mais 3 símbolos
  **inexistentes** descartados em silêncio (`LIBMPG123`,
  `LINUX_KERNEL_INSTALL_INTREE`, `SQUASHFS4_NO_XATTR`) — um deles custava o MP3.
- Correções: símbolos, âncoras no `awk` do gerador, `tools/check-buildroot-symbols.py`.
- Verificação: Buildroot 2024.02.13 clonado; kconfig dos 5 aparelhos sem
  `BR2_LEGACY` e sem `override`; `.config` conferido.

### 2026-10-27 (5) — Copilot (README visual + U-Boot da X55)

- README reescrito com galeria; `tools/make-screenshots.py` +
  `tests/test_screenshots.py` + `docs/SCREENSHOTS.md`.
- **Correção de fato**: U-Boot mainline **tem** `powkiddy-x55-rk3566` desde
  v2024.07 (a doc dizia que não existia). X55 passou a
  `GIOCHI_UBOOT_UPSTREAM=yes` com U-Boot 2024.10 + BL31/TPL do rkbin;
  `tools/check-uboot.py` no lint.
- Referência usada: [xtrempkch-droid/Walkmam_RK3566](https://github.com/xtrempkch-droid/Walkmam_RK3566)
  (bring-up dos mesmos aparelhos) — confirmou defconfig, blobs e offsets.
- Bug encontrado ao simular o CI em clone: `make test` não compilava `all`.
- Verificação: `make test` 15/15 e `make lint` com as três árvores externas.

### 2026-10-27 (7) — Copilot (ferramenta de PC na imagem)

- Bug apontado pelo mantenedor: `giochi-tools.mk` instalava `giochi-pack.sh` no
  rootfs. É ferramenta de PC e, na imagem, falharia sempre (BusyBox tar não tem
  `--format`, `--owner/--group` nem `-r`; `jq` não existe no rootfs).
- Removido do `INSTALL_TARGET_CMDS`; o cabeçalho do `.mk` agora lista o que é
  do console e o que é do PC.
- `tools/check-image-contents.sh` (em `make lint`) guarda os dois sentidos:
  ferramenta de PC no alvo e binário obrigatório faltando.
- `giochi-pack.sh` recusa rodar com tar não-GNU (exit 3), com o motivo.
- Verificação: guarda testada ao contrário (reintroduzi o bug → rc=1);
  `make lint`, `make test` 15/15 e pack real gerando tar válido.

### 2026-10-27 (8) — Copilot (inventário de componentes)

- Confronto da lista consolidada de apps da v1.2 com o repositório:
  [docs/roadmap.md](docs/roadmap.md) e novo [docs/APPS.md](docs/APPS.md).
- Lacunas registradas: `giochi-store` e `portmaster` **não existem** (as abas
  apontam para eles); `news.json` sem produtor; `giochi-thumbgen` sem extração
  de imagem; `giochi-forge` sem detecção USB/transferência/export do Godot.
- Defeito corrigido no caminho: app ausente produzia `exit 127` silencioso.
  `spawn_child` agora testa `access(path, X_OK)` antes do `fork` e loga
  `'<caminho>' ausente ou sem permissao de execucao`.
- Divergências da lista × spec registradas: `layout.json` está em
  `/usr/share/giochi` (não `/etc/giochi/layouts/default/`) e o **§4.5.4 não
  existe** na v1.2 do repositório (a lista cita transferência por `scp`).

### 2026-10-27 (9) — Copilot (build de imagens: kernel e boot.scr)

- Erro no runner: `Can't find default configuration
  "arch/arm64/configs/defconfig_defconfig"`. Causa: `USE_DEFCONFIG=y` +
  `BR2_LINUX_KERNEL_DEFCONFIG="defconfig"`, e o Buildroot acrescenta
  `_defconfig` ao valor. Trocado por `USE_ARCH_DEFAULT_CONFIG=y` nos dois
  templates (o help do Buildroot cita ARM64 como o caso de uso).
- Segundo bug achado na varredura do que roda **depois** do kernel: nada gerava
  `boot.scr`, que o `mkbootimg.sh` exige. Adicionados
  `HOST_UBOOT_TOOLS_BOOT_SCRIPT` (+ `_SOURCE` apontando para `boot.cmd`),
  `HOST_MTOOLS` e `HOST_DOSFSTOOLS` (estes dois também removem a dependência do
  `apt` da máquina). `post-image.sh` passou a tratar boot.vfat ausente como
  fatal, em vez de avisar e deixar o genimage falhar sem contexto.
- Verificado com Buildroot 2024.02.13 real: os 5 defconfigs geram `.config`
  sem `BR2_LEGACY` e sem `override`; `LINUX_KCONFIG_DEFCONFIG=defconfig` (o
  comando correto) nos cinco; `BOOT_SCRIPT=y` nos cinco. DTS de X55, RG353V e
  RG351V confirmados em Linux v6.12; layout do `u-boot-rockchip.bin` a 32 KiB
  conferido contra o rock5b do Buildroot (mesmo padrão).
- Guarda nova no `check-buildroot-symbols.py`: validação de **valor** (escolha
  de config do kernel, sufixo duplicado), que roda sem `BUILDROOT_DIR`.

### 2026-10-27 (10) — Copilot (build REAL em container: U-Boot + kernel)

Primeira vez que este repositório compilou de verdade. Container Ubuntu 24.04
(GCC 13, igual ao runner) via `tools/docker-build.sh`, Buildroot 2024.02.13.

**Provado:**
- `u-boot-rockchip.bin` 9,4 MB gerado com `powkiddy-x55-rk3566` + BL31
  (`rk3568_bl31_v1.43.elf`) + DDR (`rk3566_ddr_1056MHz_v1.18.bin`) do rkbin.
- `Image` 47 MB e `rk3566-powkiddy-x55.dtb` 59 KB (DTS mainline do Linux 6.12).
- `boot.scr` 2,2 KB — a correção da armadilha 3.15 funciona.
- `host-lzo` **configurou e compilou** no container: era mesmo o CMake 4.2 do
  host, não problema de configuração.
- `.config`: `USE_ARCH_DEFAULT_CONFIG=y`, `BOOT_SCRIPT=y`, `MTOOLS`,
  `DOSFSTOOLS` — a armadilha 3.14 está fechada.

**Bug novo (armadilha 3.16):** `mkeficapsule` exigia `uuid-dev` e `gnutls-dev`
do host. Passava no CI só porque a imagem do runner os traz instalados.
Resolvido com `BR2_TARGET_UBOOT_NEEDS_UTIL_LINUX` e `..._NEEDS_GNUTLS`.

**O que não terminou:** o build completo (rootfs + pacotes + genimage) parou por
falta de memória da máquina durante o mesa3d. Falta confirmar `sdcard.img`.

### 2026-10-28 (11) — Copilot (runner do CI morria por disco cheio)

- Run 40617882645 (`ee5296c`): U-Boot e kernel compilaram no runner — as
  armadilhas 3.14/3.15/3.16 estão fechadas — e o job morreu 69 min depois, no
  meio do openssl, **sem mensagem** e sem rodar os passos de limpeza.
- Causa medida: `dist/x55/build/build/` = **13 GB** antes do mesa3d; o runner
  tem ~14 GB livres. O log termina numa linha truncada porque o runner não
  conseguiu mais escrever.
- Correções: passo "Liberar espaco em disco" (remove SDKs pré-instalados,
  ~20–25 GB), ccache removido do CI (copia cada objeto), guarda de espaço em
  `build-image.sh` (`GIOCHI_MIN_FREE_GB`, padrão 15) com o número medido na
  mensagem.
- Comparação útil: a run anterior (`48f43c7`) tinha morrido em
  `gnutls/gnutls.h` — o fix do `ee5296c` funcionou, o build passou do U-Boot.

### 2026-10-27 (6) — Copilot (AGENTS.md + roadmap)

- Criou este arquivo e [docs/roadmap.md](docs/roadmap.md), consolidando as
  armadilhas e a ordem de trabalho por risco.
- Abertos registrados no roadmap; nada disso foi testado em hardware.

### 2026-10-27 (10) — Copilot (pasta `Jogos/`)

- `Jogos/Arcade Volleyball/arcade_volleyball.cpp` **nao compilava**: as novas de
  linha tinham sido colapsadas e o `//` de cada cabecalho de secao engolia o
  codigo da linha (`const int WINDOW_WIDTH`, os 4 `SDL_Color`, `struct Player`,
  `class ArcadeVolleyball` e `int main`). O arquivo tambem comecava com um
  titulo markdown (`# Arcade Volleyball ...`), que o `g++` le como diretiva de
  pre-processador invalida. Reescrito mantendo a mesma jogabilidade.
- Bugs de fisica encontrados ao rodar a simulacao (nao estavam visiveis na
  leitura): a bola quicava para sempre na mesma vertical ao bater na cabeca de
  um jogador (`vx` ia a zero) e a partida travava sem ninguem pontuar; e uma
  colisao podia empurrar a bola para fora da tela por um quadro (jogador
  encostado na parede). Corrigidos em `collide_player` e em `update_ball`.
- Ganhos de correcao que faltavam: `#include <algorithm>` (para
  `std::max`/`std::min`), `SDL_CONTROLLERDEVICEADDED`/`REMOVED` com
  `SDL_GameControllerOpen`/`Close` (sem isso `SDL_INIT_GAMECONTROLLER` nao
  entrega botao nenhum), pulo de borda (segurar o botao repicava no chao) e
  `SDL_RenderSetLogicalSize` (a janela era 1280x720 fixos, sem escala para o
  painel 640x480 da X35S/X35H). Placar, que existia so no estado e ia para o
  `stdout`, agora e desenhado em segmentos.
- `Jogos/Arcade Volleyball/Makefile`: build isolado com `pkg-config sdl2`,
  saida em `build/host/bin` (ignorado pelo git; um binario na pasta do jogo
  nao esta na lista de `is_allowed` de `tools/check-tracked.sh`). O jogo **nao**
  entrou no `Makefile` da raiz: SDL2 e opcional no host e isso quebraria o
  `make` de quem nao tem `libsdl2-dev`.
- Documentacao: `Arcade Volleyball.md` e `Lista de jogos.md` reescritos (eram
  saida de chat: "Use o código com cuidado.", bloco de codigo colapsado,
  `makefile` no meio da frase).
- **Correcao de fato** na doc antiga: ela mandava empacotar com `giochi-forge`
  definindo o entrypoint. O `runtime` de `schemas/metadata.schema.json` aceita
  so `godot|retroarch|proton` e `src/launcher/metadata.cpp:98` recusa o resto,
  entao um jogo nativo SDL2 **nao** pode virar `.giochi` hoje; faltaria um
  `runtime: "native"` na spec, no schema e no launcher.
- Verificacao: `g++ -std=c++17 -Wall -Wextra -Wshadow -Wformat=2 -Wundef
  -Werror` limpo (headers do `libsdl2-dev` 2.32.10, runtime 2.32.10 do host);
  `SDL_VIDEODRIVER=dummy ./arcade-volleyball` roda e pontua; `make -C` gera em
  `build/host/bin` e falha com mensagem clara sem SDL2; harness descartavel em
  `/tmp` incluiu o `.cpp` com os membros expostos e rodou 20000 quadros com
  entrada aleatoria (205 pontos, maior intervalo sem ponto 4,6 s, sem NaN, sem
  bola fora da tela, sem bola dentro da rede, sem jogador atravessando a rede).
- Aberto: nada disso foi jogado em aparelho; sem audio, sem pausa e sem partida
  de dois jogadores; o jogo nao aparece no Launcher (nenhum catalogo le a pasta).

### 2026-10-27 (11) — Copilot (jogo em Godot 4.3, orçamento 700 MB/128 MB)

O mantenedor definiu que o runtime do projeto é Godot (ADR-07) e pediu o jogo
"mais moderno possível" dentro de **700 MB de RAM e 128 MB de VRAM** — que são
exatamente os números do RK3566 em `src/init/device_profile.c` (2048 MB / 128 MB;
o RK3326 é 1024 MB / 64 MB).

- Novo projeto em `Jogos/Arcade Volleyball/godot/`: Godot 4.3 com
  `gl_compatibility` e `godot_max = 4.3` do `device_profile.json`. Estrutura:
  `scripts/sim/` (regras puras: `match.gd`, `fixed_step.gd`), `scripts/`
  (views, hud, audio, art, quality), `shaders/court.gdshader`, `scenes/`,
  `tests/`.
- **Nenhum asset binário**: personagens e rede são `_draw`/`StyleBoxFlat`, a
  quadra é um fragment shader (1 draw call, 0 textura), o brilho/poeira usam um
  único `GradientTexture2D` 128x128 e o som é sintetizado em `AudioStreamWAV`
  no boot. Motivo: num aparelho com 128 MB de VRAM o gargalo seria textura, e
  asset de áudio ainda teria o custo do import.
- Separar o modelo da cena não foi preciosismo: com `match.gd` puro o fuzz de
  20 000 passos roda headless em segundos e vale igual para o port C++.
- Medido no host com GPU real (Mesa/radeonsi): **RAM 190–192 MB, VRAM 23–27 MB,
  41–51 draw calls**. Texturas: 16,9 MB, dos quais a arte do jogo é < 1 MB (o
  resto é o tema/fonte padrão do motor). Com llvmpipe a mesma cena dá 248 MB de
  RSS — o número do aparelho só o aparelho decide, e para isso existe o painel
  `F3` no jogo.
- `godot/forge.json` + `giochi-forge check`: **Compatível** em `rk3566-2gb` e
  `rk3326-1gb` (inclusive inferindo de `project.godot` sem `forge.json`).

**Armadilhas novas (silenciosas):**

1. `--script` só acha `class_name` depois de uma passada de importação
   (`--headless --editor --quit`); sem ela: *"Could not find type
   VolleyMatch"*. O runner do jogo roda essa etapa sempre.
2. `Tween` roda com delta **escalado** por `Engine.time_scale`, e
   `Tween.set_ignore_time_scale()` **não existe** no 4.3 (conferido com
   `has_method`). Banner animado por tween levava ~1,6 s para aparecer durante
   o hit-stop. HUD agora anima por tempo real (`Time.get_ticks_msec()`).
3. `AudioStreamWAV` tocando no `quit()` deixa o engine avisar *"ObjectDB
   instances leaked at exit"*; `_exit_tree()` no `SfxKit` para os players e
   limpa os streams.
4. Testar "de olho" não pega erro de posicionamento: `PlayerView` nunca
   atribuía `position` (os dois jogadores desenhavam na origem) e a sombra do
   piso usava `Vector2(y, 0)`. Só apareceu conferindo coordenadas de pixel no
   PNG capturado.
5. `GPUParticles2D` foi evitado de propósito: `CPUParticles2D` funciona nos três
   renderizadores e a quantidade já é limitada por preset.

**Integração:** `godot/.godot/` entrou no `.gitignore` e na lista de exceções de
`tools/check-tracked.sh` (testado ao contrário: sem a exceção o lint reprova
com 9 arquivos). O runner entrou em `sh -n` do `make lint` e há alvo próprio
`make godot-test` — **fora** de `make test`, porque o motor não é dependência do
repositório (mesmo critério de SDL2/ALSA/Pillow). O port C++ recebeu os mesmos
dois empurrões laterais da bola (fuzz depois: 219 pontos, maior espera 1,9 s).

**Aberto:** nada rodado em aparelho (Mali-G52/Panfrost ≠ Mesa); sem export
template musl (ADR-03) e sem export pelo `giochi-forge`, então ainda não existe
`.giochi`; RK3326 continua oficialmente em Godot 3.5 LTS (ADR-07), e um projeto
4.3 não abre no 3.5.

### 2026-10-27 (12) — Copilot (Spacewar em Godot 4.3)

O arquivo que existia em `Jogos/Spacewar/` era **só o listing assembly do
PDP-1** (7.522 linhas), sem documentação e sem nada executável no console.
Preservado como `Jogos/Spacewar/Spacewar-1962-listing.pdp1` e o jogo foi
escrito a partir dele, no mesmo padrão do Arcade Volleyball (Godot 4.3 +
`gl_compatibility`, arte vetorial, som sintetizado, modelo separado da cena).

**Sobre o listing (vale registrar, para ninguém tratar como original):** o
cabeçalho diz `fio-dec system, june 1963` e a 1ª página `/spacewar 3.1 24 sep
62`. É a versão 3.1 (set/1962) montada sob o FIO-DEC em jun/1963, **transcrita
com anotações**: linhas com `-` são comentários do transcritor e os símbolos
`ZZnnnn` são gerados pela ferramenta. Não é o paper tape de 1962.

Regras tiradas do listing (linhas do arquivo preservado): controle de 5 bits
(119-120), `hyperspatial uncertancy` (30), `/ display a star` (688), `torpedo
calc routine` (1690), `hyperspace routines` (1731-1790), `check if torpedos
exhausted` (2075), `spaceships into star` / `now go bang` (2163-2164).

**Quatro bugs encontrados MEDINDO (nenhum aparece em 10 s de jogo olhando):**

1. O tubo de torpedos **não recarregava**: a carga só voltava ao renascer e,
   depois do quarto tiro, o navio (e a IA) ficava sem torpedo para sempre. Soak
   de 12 000 quadros: `partidas=321 abates=2`. Corrigido com paiol (1 torpedo a
   cada 2,4 s).
2. **Respawn fora da arena**: era radial e num canto caía em `y = -39`. Quem
   pegou foi o invariante do fuzz (`navio 1 fora da arena`), não o olho.
3. **IA invencível**: desviava de 100% dos torpedos e a partida não terminava
   (2 abates em 333 s). Corrigido com tempo de reação de 0,30 s e scan em
   intervalos: 4-7 abates por minuto.
4. **IA suicida**: perseguia o adversário direto para dentro do star (2 abates
   em 60 s, os dois por suicídio). Corrigido simulando 0,9 s de empuxo antes de
   ligar o motor, e usando intercepção de segunda ordem na mira (o alvo orbita;
   mira ingênua erra dezenas de px).

- Medido no host com GPU real (Mesa/radeonsi): **RAM 186-187 MB, VRAM 22,2 MB,
  36-41 draw calls** — 27% e 17% do teto de 700 MB/128 MB.
- `godot/forge.json` (`godot_version: 4.3`, gles3 com fallback gles2).
- `make godot-test` passou a rodar **os dois jogos** (`GODOT_GAMES` no
  Makefile) e o `sh -n` do lint cobre os dois runners.
- `Spacewar.md` documenta controles, regras, orçamento medido, testes, o que
  falta e o histórico — incluindo a explicação de o que é o `.pdp1`.

**Aberto:** nada rodado em aparelho; sem export template musl (ADR-03) não há
`.giochi`; RK3326 continua em Godot 3.5 LTS pelo ADR-07.

### 2026-10-28 (13) — Copilot (jogos padrão do sistema + benchmark + i18n)

O mantenedor definiu que os jogos da pasta `Jogos/` são **jogos padrão do
sistema**, usados como teste e benchmark dos aparelhos, e que **jogo que não
for domínio público sai da lista**. Duas decisões daí:

1. **Shattered Pixel Dungeon ficou FORA.** Conferido na fonte:
   `00-evan/shattered-pixel-dungeon` é **GPL-3.0** (API do GitHub), não domínio
   público — e GPL é copyleft: distribuir derivado obriga a abrir e manter a
   licença. Não é problema do jogo, é decisão de licenciamento do sistema
   inteiro. Registrado em `assets/default-games.json` → `excluded`, com motivo
   e URL do upstream. **Colossal Cave entrou**: `open-adventure` (ESR) é
   BSD-2-Clause e o autor declara distribuição "com permissão e incentivo" de
   Crowther & Woods; a página também chama a versão de 430 pontos de
   *Adventure 2.5* — origem do nome "2,5D" que o mantenedor usou.
   Spacewar: criado no MIT em 1962, nunca registrado. Arcade Volleyball: arte e
   código escritos aqui (`clean_room`), nenhum binário do original.

2. **A regra virou código, não comentário**: `assets/default-games.json`
   (fonte única) + `schemas/default-games.schema.json` +
   `tools/check-default-games.py` (no `make lint`) + `tests/test_default_games.py`
   (suíte `jogos-padrao`). O checker recusa `copyleft`/`proprietary`, projeto
   sem `scripts/bench.gd`, projeto sem `tests/run_tests.sh`, lista vazia e jogo
   em `games` **e** em `excluded`. O teste também exige **evidência** de licença
   por jogo (para o próximo agente poder conferir sem refazer a pesquisa).

**Benchmark (`--bench`) igual nos três jogos** — `scripts/bench.gd` (cópia em
cada projeto, como `fixed_step.gd`, porque cada jogo é um projeto Godot
isolado): p50/p95/p99, fps médio, tempo de processo/física, RAM (RSS de
`/proc/self/status`), VRAM, draw calls e objetos, contra o teto de 700 MB/128 MB.
Entrada sempre sintética (autoplay), senão a medição não é reprodutível.
`make bench` + `tools/bench-games.sh` lê o manifesto (sem repetir nomes de jogo)
e falha se `budget.passed = false`.

**Colossal Cave em 2,5D, com três idiomas** (pedido do mantenedor):
`scripts/sim/cave.gd` **não tem nenhuma frase** — só `message_key` +
`message_args`; o texto vive em `scripts/i18n/locale.gd` (pt-BR/en/es). Troca de
idioma a quente no menu de pausa (↑↓ escolhe a linha, ←→ ajusta), sem reiniciar
a partida. O teste de idiomas cobra: chave presente nos três, conjuntos de
chaves idênticos (tradução sobrando ou faltando é erro), e que a rota de
referência vence com a mesma pontuação nos três idiomas (trocar idioma não pode
mudar regra).

**Bugs encontrados medindo, não lendo:**

1. **`--bench-out` relativo gravava nada, calado**: `FileAccess` só aceita
   `res://`, `user://` ou caminho absoluto. O resumo era impresso e o laudo
   nunca existia. Agora `bench.gd` converte e avisa; `bench-games.sh` resolve
   `OUT` para absoluto.
2. **`for` sobre lista de projetos parte os caminhos nos espaços**
   ("Colossal Cave Adventure"): a saída agora é `id<TAB>caminho` lida com
   `while IFS=tab read`.
3. **`set --` dentro do laço sobrescrevia `$1`** (o filtro de jogo falhava a
   partir da 2ª iteração). Filtro guardado antes.
4. **O Godot herdava o stdin do laço** e comia o resto da lista: `</dev/null`.
5. **`_get()` no `Locale` colide com `Object._get` nativo** → renomeado
   `_lookup`; e `var table: Dictionary` não aceita `String` no fim do caminho
   (`_walk` com `Variant`).
6. **Duas passagens de mão única no mapa da caverna**: `queda` saía para
   `salao` e `grade`, e nenhuma das duas voltava. Achado pelo teste de simetria
   de saídas, não pelo olho.
7. **`MAX_CARRY = 5` tornava a caverna incompletável** (lâmpada ocupava uma
   mão e são 5 tesouros). Achado pelo teste da rota de referência.

**Pastas com ESPAÇO NO FIM do nome** (`Colossal Cave Adventure `,
`Shattered Pixel Dungeon `) quebraram shell, Makefile e link de markdown três
vezes. Renomeadas sem o espaço final, junto com os `.md` de dentro.

- Medido no host (Mesa/AMD): Spacewar 187 MB/18,2 MB, Colossal Cave 187 MB/24,2 MB,
  Arcade Volleyball 188 MB/19,7 MB — todos < 25% do teto de VRAM.
- `BR2_PACKAGE_GIOCHI_GAMES=y` nos cinco defconfigs (gerados); pacote instala
  `/usr/share/giochi/games/<id>` a partir do manifesto (~420 KB, sem `.godot/`).

**Aberto:** export template do Godot para aarch64/musl (ADR-03) — sem ele o
pacote instala os projetos, não binários prontos, e ainda não existe `.giochi`;
nada rodado em aparelho.

### 2026-10-28 (14) — Copilot (Colossal Cave: leitura da caverna e documentação)

O mantenedor pediu para **não investir mais no Colossal Cave agora** ("precisa
de melhorias, focaremos nele em outra oportunidade"). Então esta entrada não
entrega features: fecha o que estava pela metade e **registra a dívida**.

**Feito:**

1. **Correção da caverna escura.** O shader da caverna
   (`godot/shaders/cavern.gdshader`) foi reescrito: a primeira medição dava
   **luminância média 12,6/255** — o jogador via uma mancha preta. A causa era
   o shader ter uma única faixa de cor (`rock_dark`) para todas as superfícies.
   Agora há quatro faixas (piso, parede de fundo, laterais, teto) mais halo do
   lampião. Medido depois: **38,3/255 de média**, p90 60,3, com piso em
   (128,106,83), parede de fundo (50,43,36) e laterais (30–34). Continua escuro
   de propósito, e continua **abaixo** do que o resto do jogo entrega — está na
   lista de pendências do `.md`.
   A primeira amostragem que fiz lia **y ≥ 486**, que é o painel do HUD, não a
   caverna: média 34,8 vinha misturada com o HUD escuro. Vale como armadilha —
   **ao medir luminância, medir só a área do jogo** (0–480 neste projeto).
2. **`Colossal Cave Adventure.md` deixa de ser nota.** Era o recado de 3 linhas
   do mantenedor (as duas fontes e a intenção de "experiência 3D"); virou o
   documento do jogo (regras, controles, idiomas, orçamento medido, testes,
   integração) **com as fontes preservadas** — `wh0am1-dev/adventure` e
   `esr/open-adventure` — e a intenção de 3D registrada em "Melhorias
   pendentes", item 2. O `assets/default-games.json` aponta para esse `.md`
   desde a entrada 13, mas o arquivo ainda era a nota: qualquer leitor caía
   nela.
3. **O lint de links não olhava `Jogos/`** — ver armadilha 3.18. Ligar a
   varredura reprovou 3 links (que eram corretos, só com `%20`) e expôs que o
   §6.5 deste arquivo afirmava algo falso.
4. **`ObjectDB instances leaked at exit` nos três `budget_probe.gd`.** O probe
   fechava o jogo com um som tocando; o `AudioStreamPlaybackWAV` ficava vivo.
   Corrigido desligando o áudio no quadro 1 do probe (`_sfx.set_enabled(false)`)
   — o probe mede RAM/VRAM, não som. Verificado: o aviso sumiu nos três.
5. **Correção de fato (GPL), a pedido do mantenedor.** O mantenedor levantou que
   "GPL-3 permite uso, mantendo os créditos". A primeira metade está certa
   (§2: permissão ilimitada para *executar*), mas crédito **não** é o que
   autoriza a distribuição. E eu tinha escrito, neste mesmo dia, que "a GPL
   obrigaria a abrir tudo que se liga ao jogo" — **falso**: §5, último
   parágrafo, diz que incluir um trabalho coberto num *agregado* não estende a
   licença às outras partes. Corrigido em três lugares (nota do jogo,
   `docs/GAMES.md`, `reason` do manifesto), com o texto citado.
   O impedimento real, esse sim, é outro: §6 (*Installation Information*) exige
   "métodos, procedimentos, chaves de autorização…" para instalar versão
   modificada em "User Product", e a dispensa só vale para ROM. Rootfs SquashFS
   **assinado** com a chave no projeto = dispensa não vale. O caminho honesto,
   registrado na nota, é instalar o jogo em `/user` (fora da imagem assinada).
   Separado disso: Shattered Pixel Dungeon é Java/libGDX, sem port, sem bench —
   não pode entrar em `games` nem se a licença fosse permissiva.
   **Decisão do mantenedor (2026-10-28): manter fora**, revisando depois "como
   proceder definitivamente". Registrado na nota do jogo, no `reason` do
   manifesto e em [docs/GAMES.md](docs/GAMES.md).

**Verificação (tudo rodado):**

- `make test` → **16/16 suites**.
- `make lint` → jogos padrão (3 + 1 fora), markdown (25 arquivos, era 21), arquivos
  versionados: ok.
- `GODOT=... make godot-test` → os três jogos, `ORCAMENTO OK` em cada.
- `tests/invariants.gd` do Colossal Cave: 14 testes, rota de referência
  **39 turnos, 100 pontos, 5/5 tesouros**.
- `GODOT=... tools/bench-games.sh`: arcade-volleyball 187,9 MB / 19,7 MB /
  47 draws / 832 fps; colossal-cave 186,6 MB / 24,2 MB / 49 draws / 1002 fps;
  spacewar 187,3 MB / 18,2 MB / 38 draws / 1282 fps. Todos `passed=true`.

**Aberto (registrado no `.md` do jogo, item por item):** sem mapa, sem save,
conteúdo de 18 salas/100 pontos contra 430 do original, 3D real (hoje é
perspectiva de ponto de fuga em `Node2D`), sem áudio ambiente, nada rodado em
aparelho, e nenhum dos três jogos vira `.giochi` sem o export template musl do
ADR-03.

### 2026-10-28 (15) — Copilot (tamanho da imagem: 968 MB → 27,8 MB)

O mantenedor relatou que **a imagem estava grande demais para o GitHub** (o
`.img` era publicado cru, com 968 MB). A revisão do caminho de empacotamento
achou três defeitos, dois deles fatais para o build.

**1. O `genimage` abortava no último passo (bug de correção, não de tamanho).**
O `genimage.cfg` declarava `size = 8M` para a região raw do
`u-boot-rockchip.bin`, e o arquivo medido tem **9 401 856 B (8,97 MiB)**: o
`idbloader` vai em 32 KiB e o `u-boot.itb` começa em 6 MiB. Ou seja, a imagem
**nunca** poderia ser gerada com U-Boot mainline — e o erro aparecia só depois
de 1–3 h de compilação. Região agora com 16 MiB; partição de boot 128 → 80 MB
em offset 24 MiB; `post-image.sh` ganhou `fits()`, que compara cada artefato com
o `size` do cfg **antes** do genimage. Armadilha §3.19.

**2. Tamanho de partição em dois arquivos.** `USER_SIZE_MB=512` no
`post-image.sh` e `512M` no cfg. Agora o cfg é a fonte única
(`part_size()`/`to_mb()`), o `user.ext4` sai com o tamanho exato da partição e
há teste que falha se o script voltar a ter default próprio. Armadilha §3.20
(mesma classe do `devices/*.conf`).

**3. O artefato de distribuição.** O `post-image.sh` gera `.img.xz` (fallback
`zstd --long=31` e `gzip -9`, `BR2_PACKAGE_HOST_ZSTD` declarado) e o
`images.yml` publica **só o comprimido** + `.sha256`, com passo que falha se
não houver comprimido. Também ganhou `rm -rf dist/<aparelho>/build` no fim,
porque a árvore de build (~14 GB) ficava no runner entre jobs da matriz.

**O detalhe que valeu a diferença toda:** `xz -9` com a janela padrão de 64 MiB
dava **55,1 MB**; com `--lzma2=dict=192MiB` deu **27,8 MB**. Não é o nível de
compressão, é a **janela**: as partições A/B guardam o mesmo SquashFS a 160 MB
de distância, e só uma janela maior que essa distância deixa o xz referenciar a
segunda cópia. A janela é calculada do `size` de `system-a` no cfg (+32 MiB,
teto 512 MiB). Custo: ~192 MB de RAM **no PC** para descomprimir. Armadilha
§3.21.

**Rootfs: 34,9 → 28,0 MB** sem perder funcionalidade:

| Mudança | Ganho medido |
|---|---|
| `SQUASHFS_BS_256K` + `SQUASHFS_EXTREME_COMP` (zstd 22) nos dois templates | 34,9 → 30,1 MB |
| `post-build.sh` remove `usr/share/locale` (gettext, 9,5 MB crus) | 30,1 → 28,0 MB |

O que **não** foi mexido, porque o `target-finalize` do Buildroot 2024.02 já
faz: `usr/include`, `usr/share/{doc,man,info}`, `pkgconfig`, `*.a/*.la/*.prl`,
e `strip` de binários e de `.ko` (`LINUX_MAKE_FLAGS += INSTALL_MOD_STRIP=1` com
`BR2_STRIP_strip=y`). Conferido lendo o `Makefile` e o `linux.mk` do 2024.02, e
medindo: `strip` num `.ko` da árvore já stripada devolveu **0 byte** de
diferença. A árvore local em `dist/x55/build/target` (146 MB) tem `usr/include`,
`usr/share/doc` e `*.a` porque o build parou no mesa3d, **antes** do
`target-finalize` — é por isso que ela parece maior que a SquashFS final.

**Verificação (tudo rodado nesta sessão):**

- `tools/check-buildroot-symbols.py --buildroot /tmp/buildroot` (2024.02.x
  clonado): `ok (5 defconfigs)`. `make defconfig` real dos dois templates gerou
  `.config` **sem `BR2_LEGACY`** e com `SQUASHFS_BS="256K"`,
  `SQUASHFS_COMP_OPTS="-Xcompression-level 22"` (rk3566) / `-Xhc` (rk3326) e
  `HOST_ZSTD=y`.
- `post-image.sh` executado de verdade contra um ambiente com os artefatos
  reais (`Image`, `boot.scr`, `u-boot-rockchip.bin` de 8,97 MiB, dtb, squashfs)
  e stubs de `mkfs.vfat`/`mcopy`/`mmd`/`genimage` (não há `genimage` nem sudo
  nesta máquina): layout com u-boot e layout sem u-boot, `user.ext4` criado com
  exatamente 512 MB, aviso + recriação quando o arquivo estava com 100 MB,
  `fits()` reprovando um cfg de 4 MB (rc=1) e o artefato final
  **`sdcard.img.xz` de 28 MB**, validado com `xz -t` e `xz -dc | cmp` → byte a
  byte igual ao `.img`.
- `tests/test_image_layout.py` (9 checagens, nova suíte `imagem-layout` em
  `make test` **e** `make lint`): testada ao contrário — voltar para `size = 8M`
  faz falhar com "regiao do u-boot menor que o artefato medido".
- `make test` → **17/17 suites**. `make lint` → ok (com e sem `BUILDROOT_DIR`).
  `check-tracked.sh` testado ao contrário: `.img.xz` na raiz é aceito
  (`is_allowed` atualizado; antes o lint reprovaria).

**Correção de fato que circulava:** o `genimage.cfg` (e o comentário de
`docs/DEVICES.md`) tratavam 8 MiB como suficiente para o `u-boot-rockchip.bin`
do RK3566. Não é: o layout padrão do Rockchip põe o `u-boot.itb` em 6 MiB e o
arquivo medido termina em ~9 MiB. O README cita "8 MiB para `u-boot.itb`" como
offset de gravação — o offset está certo, o **tamanho da região** é que não.

**Aberto:** nada disso foi bootado em hardware (a imagem nunca foi gravada); o
`genimage` real não rodou aqui (sem sudo/apt nesta máquina), então o `.img`
final de 936 MB é estimado por soma de partições — os artefatos individuais e a
compressão são medidos. Encolher o kernel `Image` (47 MB, ~24 MB no `.xz`) é a
próxima economia real e é trabalho de `linux.fragment`, não de empacotamento.
