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
| Imagem de cartão: `.img` 936 MB → **`.img.xz` 41 MB** | **gerada de ponta a ponta** (§3.25–3.28, entrada 16) |
| U-Boot mainline na X55 (`powkiddy-x55-rk3566` + BL31/TPL do rkbin) | **bootou na X55** (serial: DDR → U-Boot → `boot.scr` → kernel) |
| Kernel, DTB e raiz na X55 | **bootou**: `Mounted root (squashfs) on device 179:2` por `root=PARTUUID` |
| Painel do X35S/X35H | **bloqueado** por driver fora da árvore (GPL, licenciamento adiado — §3.43) |
| Gravação do cartão | **verificada byte a byte** (`tools/verify-card.py`); SDXC de 116 GB falhou com `EIO` |
| USB: gadget composto (MTP + rede/SSH + Mass Storage) | **pronto**, testado contra configfs falso |
| Modo Desenvolvedor (§4.6.1): SSH via USB desligado por padrão | **pronto**, testado |
| Launcher: motor de layout, tema, `settings.json`, overlay 2 níveis, biblioteca | **pronto** |
| Launcher: **desenho dos widgets** no backend SDL2 | **esboço** (`TODO(v1.2)` em `render.cpp`) |
| `giochi-musicd`: protocolo e socket | **pronto**; decodificação PCM/ALSA **pendente** |
| Log de boot no cartão (`S00bootlog`) + modo `--debug` com fbcon | **pronto**, testado com mount/syslogd simulados |
| Boot completo até o Launcher em hardware (X55) | **PROVADO**: `giochi-init pronto` + `launcher v1.2 pronto: 5 abas` em **3,9 s** (entrada 19) |
| Vídeo na X55 | **DSI faz bind e `/dev/dri/card0` existe** (provado em hardware); SDL2 estava sendo compilado sem KMSDRM (§3.56) — corrigido, imagem regravada e verificada, **boot pendente** |
| Coldplug de módulos (`S05modules` + regra `$MODALIAS`) | **funcionando** no aparelho (`coldplug concluido`); PHY do DSI é `=y` (§3.50) |

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

### 3.23 Pacote local com SITE na raiz do repo = cópia recursiva de 66 GB

Os pacotes `giochi-*` compilam código deste repositório e usavam
`SITE = $(BR2_EXTERNAL_GIOCHI_PATH)/..` com `SITE_METHOD = local`. O Buildroot
sincroniza isso com `rsync`, e **não exclui nada além de `.git`/`.svn`/…**.
Como `dist/` fica dentro da raiz, o rsync copiava o **build em andamento para
dentro de si mesmo**:

```
dist/x55/build/build/giochi-assets-1.2/
  └── dist/x55/build/build/giochi-assets-1.2/
        └── dist/x55/build/build/giochi-assets-1.2/  ... (recursivo)
```

Medido: **66 GB** num único diretório de pacote. Sintomas, na ordem em que
apareceram:

1. o build **trava** no `>>> giochi-assets 1.2 Syncing from source dir` e não
   sai mais (a cada nível copiado o diretório cresce);
2. o log do CI **termina no meio de uma linha**, sem `No space left` (o próprio
   log já não pode ser gravado);
3. `rm -rf` do diretório leva **muitos minutos** por causa da profundidade;
   `rsync -a --delete` de um diretório vazio é bem mais rápido.

- **Correção**: `build-image.sh` monta um snapshot só das fontes
  (`Makefile src tools assets schemas scripts Jogos` → ~9 MB) em
  `dist/<id>/pkg-src` e exporta `GIOCHI_PKGSRC`. Todo `.mk` de pacote local usa
  `SITE = $(if $(GIOCHI_PKGSRC),$(GIOCHI_PKGSRC),<raiz>)`.
- Guarda: `tools/check-image-contents.sh` (em `make lint`) recusa
  `SITE_METHOD = local` cujo `SITE` não passe por `GIOCHI_PKGSRC`, e exige que o
  `build-image.sh` exporte a variável.
- **Regra geral**: nunca aponte `SITE_METHOD = local` para um diretório que
  contém a saída do build. Se precisar da árvore, copie só o que é fonte.

### 3.24 CRLF no índice vira diff de 2 mil linhas

`.gitattributes` declara `* text=auto eol=lf`, e isso normaliza quando o git
toca no arquivo — mas um blob commitado com CRLF fica assim até alguém
normalizar. Na próxima edição o diff sai como **arquivo inteiro** (2 296 linhas
no caso do `AGENTS.md`), escondendo a mudança real.

- Guarda: `tools/check-tracked.sh` usa `git ls-files --eol` e falha em
  `i/crlf`. Corrija com `git add --renormalize .`.
- **Não use `$'\r'` para detectar CRLF**: é bashism e o script roda com `sh`
  (dash), onde o padrão vira o texto `\r` — uma versão anterior da guarda
  acusou 8 arquivos que estavam corretos. `git ls-files --eol` é portátil e
  reporta índice (`i/`) e cópia de trabalho (`w/`) separados.

### 3.25 `device_table.txt`: um `g` de "grupo" derruba o build no ÚLTIMO passo

O `makedevs` lê `buildroot/board/giochi/device_table.txt` depois de compilar
toolchain, kernel, U-Boot, mesa3d e todos os pacotes — para montar o rootfs.
A linha `/run	g	755	0	0	-	-	-	-	-` usava `g` (de "grupo") no campo
de **tipo de arquivo**, e o tipo válido é `d` (diretório). A mensagem não
aponta o arquivo:

```
makedevs: line 29: Unsupported file type g
make: *** [fs/squashfs/squashfs.mk:56: .../rootfs.squashfs] Error 1
```

- Formato: `<nome> <tipo> <modo> <uid> <gid> <major> <minor> <início> <inc> <contagem>`
  com tipo em `f d c b p l` (arquivo, diretório, char, block, fifo, symlink).
- A "linha 29" é a linha do **arquivo concatenado**
  (`system/device_table.txt` + o nosso), não do nosso arquivo — por isso não
  bate com nenhum editor.
- Guarda: `tools/check-device-table.sh` (em `make lint`) valida campos, tipo e
  caminho absoluto em milissegundos.

### 3.26 `partition-type-uuid` só existe em GPT; com MBR o genimage aborta

Os dois `genimage*.cfg` declaram `partition-table-type = "dos"` e as partições
usavam `partition-type-uuid = "U"` / `"L"`. O genimage aborta:

```
ERROR: hdimage(sdcard.img): part boot: 'partition-type-uuid' is only valid
for gpt and hybrid partition-table-type
```

- Em MBR use o **byte hex**: `partition-type = 0x0c` (W95 FAT32 LBA, que é o
  `mkfs.vfat -F 32` do `mkbootimg.sh`) e `0x83` (Linux).
- `gpt-location` também só vale para gpt — estava no cfg de MBR e era ignorado
  em silêncio (o `image-hd.c` só o lê no caminho GPT). Removido.
- A partição raw do U-Boot (`in-partition-table = "no"`) não leva
  `partition-type` nenhum.
- Guarda: `tests/test_image_layout.py` confere a coerência tabela × tipo nos
  **dois** cfgs. A guarda pegou o mesmo defeito no `genimage-no-uboot.cfg`,
  que o build da X55 não exercita (o caminho sem U-Boot é de X35S/X35H).

### 3.27 `%PARTUUID%` não é substituído por ninguém

O `extlinux.conf` gerado pelo `post-image.sh` saía com o texto literal
`root=PARTUUID=%PARTUUID%`. Essa expansão **não existe** no genimage 17 (não há
`PARTUUID` no fonte; conferido em `image-hd.c`, `genimage.c` e `util.c`) nem em
nenhum script do repositório — o kernel receberia uma string que não resolve e
não montaria o rootfs. Ninguém notava porque o `boot.scr` (que é quem boota)
usa `/dev/mmcblk${giochi_mmc}p2`; o `extlinux.conf` é fallback (F1).

- Use o mesmo caminho de dispositivo do `boot.cmd`.
- Guarda: `tests/test_image_layout.py` proíbe `%PARTUUID%` no código do
  `post-image.sh`.

### 3.28 `[ -f x ] && echo` como último comando = exit 1

O `post-image.sh` terminava com:

```sh
for f in "$IMG.xz" "$IMG.zst" "$IMG.gz"; do
    [ -f "$f" ] && echo "..."
done
```

Quando o arquivo não existe, o `&&` não executa e o **status da lista é 1**.
Sendo o último comando do script, esse 1 vira o exit code do `post-image.sh`
inteiro — e o build acusava:

```
post-image: artefato: .../sdcard.img.xz (41M)
make: *** [Makefile:820: target-post-image] Error 1
```

…com a imagem perfeitamente gerada. O `build-image.sh` até copiava o artefato
(porque ele existia), mas o `make` saía com erro e o CI reprovava.

- Use `if [ -f "$f" ]; then ...; fi` e termine o script com `exit 0` explícito.
- Mesma classe da armadilha §3.4 (`set -e` + `$(teste && cmd)`).
- Guarda: `tests/test_image_layout.py` exige `exit 0` como último comando.

### 3.29 O cartão é `mmc 1`; `mmc 0` é o eMMC (que a X55 não tem)

`uboot.fragment` mandava o U-Boot carregar o `boot.scr` de **`mmc 0:1`**:

```
CONFIG_BOOTCOMMAND="load mmc 0:1 ${scriptaddr} boot.scr; source ${scriptaddr}"
CONFIG_ENV_FAT_DEVICE_AND_PART="0:1"
```

Em `rk3566-powkiddy-x55.dts` (o upstream, que é o DTS que o U-Boot usa):
`mmc0 = &sdhci` (eMMC), `mmc1 = &sdmmc0` (**cartão SD**), `mmc2 = &sdmmc2`,
`mmc3 = &sdmmc1` (SDIO do Wi-Fi). A X55 não tem eMMC soldado, então `mmc 0:1`
não existe: o `load` falha, o `boot.scr` nunca roda, o kernel nunca inicia e a
tela fica preta **sem uma única mensagem**. É a explicação mais provável para
um "boot que não deu nada".

- `BOOTCOMMAND` agora **sonda** `for d in 1 0` e guarda o que funcionou em
  `giochi_mmc` (o `boot.cmd` já respeita essa variável).
- `uboot.env` foi para `1:1` e `CONFIG_SYS_MMC_ENV_DEV=1`: em `mmc 0` o
  `saveenv` falhava, e sem env gravável o rollback por `bootcount` (ADR-01) não
  funciona.
- Guarda: `tests/test_boot_log.py` reprova `load mmc 0:` e exige a sondagem.

### 3.30 `LABEL=` no `fstab` precisa de `FEATURE_MOUNT_LABEL` no BusyBox

O `fstab` monta a partição C com `LABEL=GIOCHI_USER` (correto: o nome do
dispositivo varia, `mmcblk1` × `sda`). Mas o BusyBox da imagem estava com
`# CONFIG_FEATURE_MOUNT_LABEL is not set` — medido no `.config` do 1.36.1 do
build real. Sem essa opção o `mount` trata a string como **nome de arquivo**:
`mount -a` falha, `/user` nunca é montada e o sistema inteiro passa a escrever
num diretório dentro do SquashFS somente-leitura (`settings.json`, `/user/music`,
`/etc/dropbear -> /user/.ssh/dropbear`). Nada no boot avisa.

- `CONFIG_FEATURE_MOUNT_LABEL=y` no `busybox.fragment`.
- Guarda: `tests/test_boot_log.py` exige a opção no fragmento **e** o
  `LABEL=` no `fstab` (se um mudar, o outro tem de mudar junto).
- Lembrete: `/dev/disk/by-label/` não existe sem udev. O `lib.sh` tem um
  fallback por esse caminho em `giochi_user_part`, que só funciona porque o
  caminho principal (derivar do `/proc/cmdline`) resolve antes.

### 3.31 Sem `fbcon`, falha antes do Launcher é invisível

`linux.fragment` declara `CONFIG_FB=n` (decisão por boot rápido: ~1 MB de
kernel e alguns ms). **Isso NÃO desliga o console de vídeo** — ver §3.59: o
`DRM_FBDEV_EMULATION` faz `select FB` e o fbcon existe assim mesmo. A
ferramenta de diagnóstico abaixo continua válida (ela garante fbcon com o
`linux-debug.fragment`), mas a premissa "o kernel não tem console" era errada.

- Imagem de diagnóstico: `tools/build-image.sh <aparelho> --debug` (ou
  `GIOCHI_DEBUG_BOOT=1`) anexa `board/giochi/linux-debug.fragment` à lista de
  fragmentos **derivada** do defconfig (não repetida — §3.20) e escreve em
  `<out>/<aparelho>-debug/` para não confundir com a imagem normal.
- Texto deitado no painel 720×1280/270°: `setenv giochi_fbcon 1; saveenv` no
  U-Boot; o `boot.cmd` acrescenta `fbcon=rotate:1`.
- O `fbcon` só aparece quando o DRM do kernel sobe. Antes disso (BootROM, TPL,
  U-Boot) **só** o serial `ttyS2` em `1500000 8N1` mostra algo — por isso o
  `boot.cmd` ganhou `echo` de progresso.

### 3.32 O log de boot precisa de um ponto de conclusão que exista de verdade

`S00bootlog` (primeiro script de `rcS`) grava o log na partição FAT e deixa
`giochi-boot.inprogress`; se ele ainda estiver lá no próximo boot, o anterior
não terminou. O detalhe que quase passou: **`rcS` nunca retorna** neste sistema
— o `S20graphics` faz `exec /usr/bin/giochi-init`. Se o marcador fosse apagado
no fim do `rcS`, ele nunca sairia e todo boot reportaria falha falsa.

- O marcador é apagado em `giochi_boot_marker_clear` (em `lib.sh`), chamado pelo
  `S20graphics` e pelo `S99launcher` **antes** do `exec`: o critério é "o
  controle chegou ao supervisor", não "o rcS terminou".
- `rcS` roda `S00bootlog` **sozinho e antes do laço** (é ele quem cria o destino
  do `tee`) e depois duplica a saída do resto com `tee -a`.
- Nada aqui escreve em `/etc`; e o `S00bootlog` nunca aborta o boot: sem cartão
  gravável cai para `/user` e depois para `/run`.
- Desligar sem recompilar: `touch nolog` na partição 1.
- Guarda: `tests/test_boot_log.py` (16 checagens), suíte `log-de-boot`.

### 3.33 Editar `boot.cmd` NÃO atualizava o `boot.scr` da imagem

O `boot.scr` é gerado dentro do pacote **host-uboot-tools**
(`uboot-tools.mk`, `HOST_UBOOT_TOOLS_INSTALL_BOOT_SCRIPT`) — ou seja, nas
etapas de *build/install do pacote*. O Buildroot só reconstrói um pacote quando
**ele** muda; o `boot.cmd` é apenas um caminho dentro de uma variável de
configuração. Resultado medido neste repositório: `boot.cmd` de **31/10** e
`boot.scr` de **27/10** dentro da imagem. Quatro dias de mudanças no script de
boot (rollback A/B, carga do dtb, `bootargs`, marcadores de serial) não existiam
no cartão, e nada avisava.

- `post-image.sh` agora **regenera o `boot.scr` sempre**, com o `mkimage` do
  `HOST_DIR`, a partir de `board/giochi/boot.cmd`. O artefato passa a ser função
  do arquivo, não do cache de pacote.
- Vale como regra geral: **artefato derivado de arquivo do projeto dentro de
  pacote do Buildroot é cache velho esperando acontecer.** Se o `post-image.sh`
  depende do artefato, gere-o no `post-image.sh`.
- Guarda: `tests/test_boot_log.py` exige a chamada do `mkimage` no
  `post-image.sh` e os marcadores de serial no `boot.cmd`.

### 3.34 Script de init de PACOTE briga com o nosso — e ganha em silêncio

Cada pacote instala o próprio `/etc/init.d/Sxx`. Dois anulavam decisões deste
projeto, sem erro nenhum no boot:

- **`S01syslogd` / `S02klogd`** (de `busybox.mk`) sobem `syslogd -n` **sem
  `-O`**, gravando em `/var/log/messages` (tmpfs). O `syslogd` do BusyBox
  desvincula `/dev/log` e cria o próprio socket: o `syslogd` que o
  `S00bootlog` subiu **perde o socket** e o log no cartão para de crescer.
- **`S50dropbear`** (de `dropbear.mk`) sobe `sshd` **incondicionalmente, em
  todas as interfaces**, sem olhar `/user/settings.json`. Isso anula o adendo
  4.6.1 e o `S62sshd` (que existe justamente para decidir). SSH ligado por
  padrão num aparelho de bolso é o tipo de defeito que não aparece em teste
  funcional.

- `post-build.sh` remove os três (bloco "scripts de init dos PACOTES que
  conflitam"), com o motivo escrito ao lado de cada um.
- Guarda: `tools/check-image-contents.sh` (seção 6) falha se o bloco sumir, e
  `tests/test_boot_log.py` confere as três remoções **sem comentários** do
  arquivo (checar o texto inteiro passava mesmo com o `rm` removido — testado
  ao contrário).
- Ao adicionar um pacote novo, **liste o `Sxx` dele** e decida: convive com os
  nossos ou sai no `post-build.sh`.

### 3.35 Fallback do DTB gated na condição errada = eMMC boota no lugar da gente

`boot.cmd` escolhia o DTB assim:

```sh
if test -n "${fdtfile}"; then
  if load mmc ${giochi_mmc}:1 ${fdt_addr_r} dtbs/${fdtfile}; then ... fi
fi
if test -z "${fdtfile}"; then            # <-- so cai aqui se fdtfile estiver VAZIO
  load mmc ${giochi_mmc}:1 ${fdt_addr_r} board.dtb
fi
```

`fdtfile` vem do **ambiente do U-Boot de fábrica** (é ele quem boota o cartão
num aparelho com eMMC). Quase nunca está vazio, e o nosso cartão só tem o DTB
do aparelho para o qual a imagem foi gerada. Num aparelho diferente (imagem da
X55 no X35H): o `load dtbs/${fdtfile}` falhava, o fallback **não** acontecia, o
`fdt_addr_r` ficava com lixo e o `booti` falhava — e o U-Boot seguia para o
próximo alvo e bootava o eMMC. O usuário vê o sistema de fábrica e conclui "não
apareceu nada".

- O fallback agora reage ao **resultado do load** (`giochi_fdt_ok`), não a uma
  condição sobre o nome.
- Regra: `X && cmd` dentro de `if test -z` é quase sempre o teste errado —
  teste o resultado da operação, não o estado das entradas.
- Guarda: `tests/test_boot_log.py` proíbe `if test -z "${fdtfile}"` e exige
  `giochi_fdt_ok`.

### 3.36 "Não bootou" pode ser "bootou o firmware de fábrica"

Diagnóstico que resolveu o caso do X35H em 30 s, sem abrir o aparelho: com o
console ligado por USB ao PC, os descritores dizem **quem** está rodando.

```sh
cat /sys/bus/usb/devices/*/manufacturer   # ROCKNIX  <- é o firmware de fábrica
cat /sys/bus/usb/devices/*/product        # x35h Handheld Device
```

- `1d6b:0104` (Linux Foundation Multifunction Composite Gadget) **não**
  identifica nada: todo firmware desses aparelhos usa esse par. O que identifica
  são as strings.
- O BootROM do RK3566 procura bootloader em SPI → eMMC → SD. Um aparelho de
  fábrica **tem** bootloader no eMMC, e é ele que decide se olha o cartão. Se o
  nosso `boot.scr` falha, ele cai para o eMMC sem avisar.
- Consequência: para X35S/X35H (`GIOCHI_UBOOT_UPSTREAM=no`, imagem sem
  bootloader) o caminho normal **é** depender do bootloader do eMMC — e ele
  precisa varrer o cartão.
- Documentado em `docs/BOOT-DEBUG.md` §1.5 e §6.

### 3.37 Imagem de RK3566 não é universal: o cartão só leva um DTB

Cada imagem carrega **o DTB do aparelho do build** (`dtbs/<placa>.dtb` +
`board.dtb`). Gravar a imagem da X55 no X35H dá kernel com painel errado: no
melhor caso tela preta com o sistema vivo, no pior o `booti` falha e o eMMC
ganha (§3.35).

- X55 tem DTS **e** U-Boot mainline; X35S/X35H **não** têm nenhum dos dois
  (`--fallback-dts` usa um DTS de painel 640×480, só para ver se o kernel sobe).
- O melhor DTS para o X35S/X35H é o **DTB real do aparelho**, extraído do
  firmware de fábrica que está rodando: `cat /sys/firmware/fdt > x35h.dtb` e
  depois `GIOCHI_DTB_PATH=/caminho/x35h.dtb tools/build-image.sh x35h`.
- `tools/docker-build.sh` passou a repassar `--fallback-dts`, `--debug`,
  `--vendor-uboot` e `--check` (antes morria em "opcao desconhecida", e a única
  saída era sair do container).

### 3.38 `fdtfile` do U-Boot vem com `rockchip/` — o cartão tinha o nome plano

Medido no **serial do X35H** (primeira evidência de hardware deste projeto):

```
GIOCHI: boot.scr em execucao (mmc=1 slot=2 root=/dev/mmcblk1p2)
Failed to load 'dtbs/rockchip/rk...
47516160 bytes read in 4025 ms          <- Image OK (47.516.160 B = a nossa)
ERROR: Did not find a cmdline Flattened Device Tree
```

O U-Boot exporta `fdtfile` na forma **upstream** (`rockchip/<placa>.dtb`), porque
espelha `arch/arm64/boot/dts/rockchip/`. O nosso `post-image.sh` gravava o DTB
**plano** em `dtbs/`, então `load dtbs/${fdtfile}` procurava
`dtbs/rockchip/...` e falhava.

- O DTB agora é gravado nas **duas** formas (`dtbs/<nome>.dtb` e
  `dtbs/rockchip/<nome>.dtb`); custa ~59 KB no FAT (sobram ~15 MB).
- `mkbootimg.sh` precisa de `mmd ::dtbs/rockchip` + `mcopy` explícitos: o glob
  `dtbs/*.dtb` **não** entra em subdiretório. Verificado no `.img` montado.
- O `boot.cmd` tenta, em ordem: `dtbs/${fdtfile}`, `dtbs/board.dtb`,
  `board.dtb`.
- Guarda: `tests/test_boot_log.py` exige `dtbs/rockchip` no `post-image.sh` e
  `::dtbs/rockchip` no `mkbootimg.sh`.

### 3.39 Outro artefato derivado que não se regenerava: `extlinux.conf`

Mesma classe do §3.33, descoberta porque o `.img` foi conferido em vez de
confiado. O `post-image.sh` só escrevia o `extlinux.conf` `if [ ! -f ... ]`; o
arquivo fica em `BINARIES_DIR`, que **sobrevive ao build incremental**. Depois
de trocar o `root=` para PARTUUID, o `extlinux.conf` da imagem continuava com
`root=/dev/mmcblk1p2`.

- Agora é regerado **sempre**, como o `boot.scr`.
- Regra: **todo** arquivo que o `post-image.sh` gera em `BINARIES_DIR` é
  regerado incondicionalmente. Guarda contra cópia velha: conferir o `.img`
  final (montar o FAT e ler o arquivo), não o arquivo em `BINARIES_DIR`.
- Guarda: `tests/test_boot_log.py` proíbe `if [ ! -f ...extlinux.conf ]`.

### 3.40 `SIGPIPE` no supervisor: um cliente que desconecta matava o `giochi-init`

Encontrado porque a suíte `devmode` começou a falhar de forma **intermitente**
(mas frequente) sob carga — e a investigação mostrou que não era o teste:

```
EXIT rc=141 (sinal=13)      # 128 + SIGPIPE
```

`giochi_sock_write_all` usava `write()`. Se o outro lado fechar antes de ler a
resposta inteira, o `write()` levanta **SIGPIPE** e o processo morre. No
aparelho, quem fala com o `control.sock` é o **Launcher**: fechar o app no
momento errado derrubaria o supervisor da sessão inteira. Nada no log aparece —
o processo simplesmente some.

- `giochi_sock_write_all` agora usa `send(..., MSG_NOSIGNAL)` (devolve `EPIPE`
  em vez de sinalizar).
- `giochi-init` também **ignora** `SIGPIPE` (`sigaction(SIGPIPE, SIG_IGN)`):
  defesa em profundidade, porque há descritores que não passam por
  `giochi_sock_write_all`.
- Regressão coberta: cenário 6b da `devmode_test.sh` — um cliente conecta,
  manda `PING` e **fecha sem ler**; depois outro cliente tem de receber `PONG`.
  Testado ao contrário: revertendo as duas linhas, os dois checks falham.
- **Armadilha de método**: o mesmo sintoma (`Connection refused`) aparecia por
  dois motivos diferentes — (a) o processo morto e (b) a corrida entre `bind()`
  e `listen()`. O cliente do teste passou a **reconectar** (corrige (b)) e a
  suíte ganhou o check de vida do processo (expõe (a)). Se o `connect` falhar
  "às vezes", meça o **exit code** do servidor antes de aumentar timeout.

### 3.41 Teste intermitente: separar corrida de bug antes de "consertar o teste"

A `devmode` falhava em ~1 de cada 5 execuções **e sempre passava isolada**.
Havia duas causas reais:

1. corrida `bind()`/`listen()` — o arquivo do socket existe antes de o
   servidor aceitar; um `connect` nesse intervalo recebe `ECONNREFUSED`.
   Cliente correto tenta de novo.
2. **SIGPIPE** (§3.40) — bug de verdade, no servidor.

Conclusão prática: um teste intermitente é uma pergunta, não um incômodo.
Antes de subir timeout, instrumente o **processo** (wrapper que registra
`$?` — foi assim que `rc=141` apareceu) para saber se o servidor morreu.

### 3.42 `Mounted root` + `No working init found` = cartão, não build

Segundo boot medido no console serial (X35H). O kernel bootou, o `PARTUUID`
resolveu, o SquashFS **montou** — e então:

```
VFS: Mounted root (squashfs filesystem) readonly on device 179:2.
I/O error, dev mmcblk1, sector 213180 op 0x0:(READ) flags 0x800
SQUASHFS error: Failed to read block 0x66: -5
Starting init: /sbin/init exists but couldn't execute it (error -5)
Kernel panic - not syncing: No working init found.
```

`-5` é `EIO`. **Nada disso é erro de build**, e é fácil perder horas nele:

- `unsquashfs -d /tmp/ex rootfs.squashfs` extraiu **tudo** (rc=0) → a imagem
  gerada está íntegra;
- os setores reclamados (`213088`, `213180`, `213184`, offset 104,0–104,1 MiB,
  ou seja 49–98 KiB *dentro* do SquashFS) têm **dados válidos** no arquivo.

Ou seja, o defeito está entre o PC e a mídia. Duas causas com ações opostas:

| Medição | Causa | Ação |
|---|---|---|
| dados diferentes no cartão | gravação não terminou (`dd` sem `conv=fsync`, cartão removido cedo) | regravar |
| `ERRO(S) DE LEITURA` no cartão | setor ruim na mídia | regravar (realoca); repetindo no mesmo setor, trocar o cartão |

- Ferramenta: `tools/verify-card.py` (leitura pura, recusa alvo montado).
  Suíte `verificador-de-cartao`.
- **Regra geral**: depois de gravar, compare. O `dd` termina com sucesso mesmo
  quando o cartão devolve lixo.
- Ordem de diagnóstico quando a tela não acende, do mais barato ao mais caro:
  1. serial (tem linha do tempo?);
  2. `docs/BOOT-DEBUG.md` §1.5 (quem está rodando: nosso sistema ou o de
     fábrica?);
  3. log na partição FAT (o `rcS` chegou a rodar?);
  4. §7 (erro de I/O = cartão).

### 3.43 O painel do X35S/X35H exige um driver FORA da árvore

Investigando por que a tela do X35H não acende, achei o DTS oficial nas fontes
do ROCKNIX (`projects/ROCKNIX/devices/RK3566/patches/linux/0016-...x35s.patch`).
Ele é um delta de **41 linhas** sobre o DTS da X55, e o que ele muda explica
tudo:

```
&panel {
        compatible = "rocknix,generic-dsi";
        panel_description = "G size=70,52 ... I seq=b9f11283", ...;
        rotation = <0>;
};
&hdmi { status = "disabled"; };
```

- O painel do X35S/X35H **não é** o `himax,hx8394` da X55. É um painel MIPI
  genérico, e a ROCKNIX escreveu um driver próprio para lê-lo do DTB
  (`packages/linux-drivers/generic-dsi/sources/panel-generic-dsi.c`, 724
  linhas, `compatible = "rocknix,generic-dsi"`, GPL-2.0, tem guardas de versão
  que cobrem o 6.12). **Sem esse driver o painel não liga, com o DTS certo ou
  não.**
- O mesmo DTS usa `&joypad` (rocknix-joypad), também fora da árvore — a mesma
  lacuna já registrada no §4.
- **decisão adiada pelo mantenedor (2026-10-31):** "sobre o licenciamento será
  revisto mais à frente no projeto". Traduzindo para o código: o driver **não**
  entra agora, e a lacuna continua aberta e registrada — mas por escolha de
  sequenciamento, não por impedimento técnico. Trazer o driver é portar código
  **GPL-2.0** para um repositório **MIT**; o caminho técnico está mapeado em
  [docs/DEVICES.md](docs/DEVICES.md). Quando a revisão de licenciamento
  acontecer, o trabalho é: copiar `panel-generic-dsi.c` para
  `src/kernel/patches/`, um fragmento de Kconfig com
  `CONFIG_DRM_PANEL_GENERIC_DSI=m` e o DTS do X35S como `GIOCHI_DTS_UPSTREAM`.

**O que ficou pronto e é nosso:** `tools/check-dts-panel.py` + suíte
`painel-do-dts`. Ele resolve o DTS que a imagem vai usar (seguindo `#include`,
inclusive `DTS_PATH` out-of-tree) e diz se há painel utilizável:

```
x55    painel: ok (rk3566-powkiddy-x55.dts)
x35h   painel: ? (DTS externo, informe com --dts)
rk3326-generic painel: AUSENTE ... -> a tela fica preta
```

- Roda no `make lint` e no `build-image.sh` (o `--fallback-dts` agora **avisa**
  quando o DTS de fallback não tem painel; antes passava calado e o usuário
  gastava um ciclo de flash).
- Medição que corrige uma suposição minha: o `rk3566-anbernic-rg353v` usado como
  fallback **tem** painel — herdado do `rg353p` por `#include`
  (`newvision,nv3051d`, 640×480, driver mainline). Procurar `panel` só no
  arquivo do aparelho dava falso negativo; é preciso seguir os includes.
- `rk3326-generic` (fallback `rg351v`) **não** tem painel: aquela imagem nunca
  acende. Descoberto pela guarda, não por tentativa.

### 3.44 X55: o DRM não faz bind do DSI (`-22`) e o Launcher cai para `render=null`

Boot completo na X55 (entrada 19) com a tela **apagada**. O Launcher sobe e
escreve `render: SDL2 indisponivel; usando backend headless` — e essa mensagem
é o **sintoma**, não a causa: sem DRM não existe `/dev/dri/card0`, e o SDL2 sem
KMSDRM cai para o backend nulo. Quem falha é o kernel, antes:

```
dw-mipi-dsi-rockchip fe060000.dsi: [drm] *ERROR* Failed to bind: -22
rockchip-drm display-subsystem: failed to bind fe060000.dsi: -22
```

Cadeia lida no fonte (linux 6.12 desta árvore), com os pontos que devolvem
`-EINVAL`:

| Arquivo | Função | O que devolve `-22` |
|---|---|---|
| `rockchip/dw-mipi-dsi-rockchip.c` | `dw_mipi_dsi_rockchip_bind` | só repassa o que `dw_mipi_dsi_bind` devolveu |
| `synopsys/dw-mipi-dsi.c` | `dw_mipi_dsi_bind` | **é uma linha só**: `drm_bridge_attach(enc, &dsi->bridge, NULL, 0)` |
| `drm/drm_bridge.c` | `drm_bridge_attach` | `!encoder \|\| !bridge`, ou `previous` sem `dev`/com outro encoder |
| `synopsys/dw-mipi-dsi.c` | `dw_mipi_dsi_bridge_attach` | repassa `drm_bridge_attach(enc, dsi->panel_bridge, bridge, flags)` |
| `drm/bridge/panel.c` | `panel_bridge_attach` | `drm_connector_init` (tipo de conector inválido) |

O que **já foi descartado** com evidência: drivers misturados `=y`/`=m`
(`CONFIG_DRM_ROCKCHIP=y`, `CONFIG_DRM_DW_MIPI_DSI=y`,
`CONFIG_DRM_PANEL_HIMAX_HX8394=y`, `CONFIG_ROCKCHIP_VOP2=y` no `.config` do
build) e DTS sem painel (o DTB da imagem tem `panel@0` com
`himax,hx8394`, `backlight`, `reset-gpios`, `rotation=270`);
`CONFIG_FEATURE_MOUNT_LABEL` e afins não têm relação.

**O que falta:** dizer **qual** das chamadas devolveu `-22`. O `drm_bridge_attach`
não tem log próprio, então `drm.debug=0x1e` sozinho não resolve — o próximo
passo é uma instrumentação (patch de kernel) ou comparar com o DTS/patch da
ROCKNIX, que roda esses mesmos aparelhos. Enquanto isso, o aparelho **não deve**
ser dado como "sem vídeo por causa do painel": o painel é a etapa seguinte.

> **FECHADO no §3.48.** A resposta estava na pilha do `dmesg`: o
> `component_add()` do DSI bindava o DRM de dentro do probe do próprio DSI, com
> `dsi->dmd` NULL — e `drm_bridge_attach()` devolve `-EINVAL` **sem logar** nesse
> caso. Preservado aqui porque a tabela de pontos de `-EINVAL` continua sendo o
> roteiro de diagnóstico.

- Guarda relacionada: `tools/check-dts-panel.py` (suíte `painel-do-dts`) olha o
  DTS, não o bind. Ele **não** teria pego este defeito.
- Não confunda com §3.43: lá falta **driver** para o painel do X35S/X35H; aqui o
  driver existe e é mainline, e o bind falha.

### 3.45 `"$script" start || "$script"` roda o serviço DUAS vezes

O `rcS` chamava cada `S*` como `"$script" start || "$script"`, pensando em
"scripts que não aceitam argumento". O efeito real, no boot da X55:

```
Usage: /etc/init.d/S10mdev {start|stop|restart}
```

O `start` do `S10mdev` sai `!= 0` (o script termina com `xargs modprobe` e
**não existe modprobe** no rootfs), então o fallback chamava o script **de
novo**, sem argumento, e o `*)` do `case` imprimia o uso. Ou seja: a mensagem
escondia a falha de verdade e ainda executava tudo pela segunda vez.

- `rcS` agora chama `start` uma vez e registra `rcS: <script> start retornou
  erro` no `logger` e no stderr.
- `S10mdev` **saiu da imagem** (`post-build.sh`): `/dev` é `devtmpfs` e os nós
  que faltam vêm do `device_table.txt`; quem carrega módulo sob demanda é o
  `giochi-dongle-hotplug`.
- Guarda: suíte `primeiro-boot` (`tests/test_primeiro_boot.py`).

### 3.46 Os diretórios de `/user` do SquashFS NÃO existem depois do mount

`post-build.sh` cria `/user/{games,music,ports,.giochi,.cache}` no `TARGET_DIR`,
mas `/user` é o ponto de montagem de `user.ext4`, que o `post-image.sh` cria
**vazio**. Depois do `mount`, os diretórios do SquashFS ficam escondidos — e o
Launcher registrou, no aparelho:

```
W: musica: diretorio '/user/music' inacessivel
W: biblioteca: '/user/games' inacessivel
I: biblioteca: 0 pacotes (0 validos)
```

- `S10storage` agora roda `seed_user_tree`: cria a árvore em `/user` (e
  `~/.ssh` em 0700) a cada boot. É idempotente e cobre o caso de o usuário
  formatar a partição depois.
- O teste de escrita é um `: > arquivo`, **não** `mkdir -p`: `mkdir -p` de um
  diretório que já existe no SquashFS devolve 0 mesmo com a partição fora do
  ar, e o log diria "pronta" mentindo.
- Guarda: suíte `primeiro-boot`.

### 3.47 `device.conf` é a fonte da identidade; o `-D` do compilador se perde

Dois sintomas do mesmo dono de dado, os dois silenciosos:

1. `device_profile.json` na X55 saiu `(device=rk3326 soc=rk3326)` — os defines
   `-DGIOCHI_DEVICE`/`-DGIOCHI_SOC` atravessam duas camadas de shell (recipe do
   pacote e recipe do Makefile) e não chegaram na compilação. O código caiu no
   `#ifndef` e usou o default do RK3326.
2. `buttons.map: secao [efault] ausente` — o Launcher procurava
   `"GIOCHI_BUTTONS_MAP="` e pulava **20** caracteres; a chave tem **19**
   (`GIOCHI_` + `BUTTONS_` + `MAP` + `=`). Comia o `d` de `default` e o mapa do
   usuário era descartado em silêncio.

- Novo `src/common/giochi_deviceconf.{c,h}`: `giochi_deviceconf_get(path, key,
  out, size)` casa a chave pelo `strlen` (nunca por número escrito à mão),
  recusa truncar valor e é `extern "C"`.
- `giochi-init` lê `GIOCHI_DEVICE`/`GIOCHI_SOC` de `/etc/giochi/device.conf`
  (`GIOCHI_DEVICE_CONF` sobrescreve, para teste de host); o `-D` virou só
  fallback. Medido: `(device=x55 soc=rk3566)` contra o `device.conf` real da
  imagem.
- O Launcher passou a usar o mesmo helper.
- Guardas: `tests/test_deviceconf.cpp` (15 checagens) e suíte `primeiro-boot`.

**Regra que fica:** dado que existe **dentro da imagem** (`/etc/giochi/*.conf`)
não deve depender de macro de compilação. `devices/<id>.conf` já era a fonte
única no build (§3.20); isto é a mesma regra em runtime.

### 3.48 X55: o DSI fazia o bind do DRM de dentro do próprio probe (`-22`)

Fecha o bloqueio do §3.44. O log do aparelho dava só isto, sem mais nada:

```
dw-mipi-dsi-rockchip fe060000.dsi: [drm:dw_mipi_dsi_rockchip_bind] *ERROR* Failed to bind: -22
rockchip-drm display-subsystem: failed to bind fe060000.dsi (ops ...): -22
```

A serial com o `dmesg` completo mostrou o que faltava — **a pilha do bind**:

```
rockchip_drm_bind
  <- try_to_bring_up_aggregate_device
  <- __component_add <- component_add
  <- dw_mipi_dsi_rockchip_host_attach
  <- dw_mipi_dsi_host_attach <- mipi_dsi_attach <- hx8394_probe
  <- mipi_dsi_device_register_full <- mipi_dsi_host_register
  <- dw_mipi_dsi_probe <- dw_mipi_dsi_rockchip_probe
```

Ou seja: o painel MIPI é criado e sondado **de forma síncrona** dentro de
`dw_mipi_dsi_probe()`, e o `component_add()` do `host_attach` fez o master do
DRM bindar **dentro do probe do DSI**, antes de o probe terminar. Nesse
instante:

- `dsi->dmd` era **NULL** (é o valor de retorno de `dw_mipi_dsi_probe()`, que
  ainda não retornou);
- `dsi->bridge` ainda não tinha `funcs`/`driver_private` (o probe preenche
  depois de `mipi_dsi_host_register()`).

Como `bridge` é o **primeiro** campo de `struct dw_mipi_dsi`, `&NULL->bridge`
é `NULL`, e `drm_bridge_attach()` tem **um único** caminho que devolve
`-EINVAL` sem imprimir nada (`if (!encoder || !bridge) return -EINVAL`). É
por isso que o `-22` aparecia sem a linha
`failed to attach bridge ... to encoder ...: -22` — a ausência dessa linha era
a pista, não um log faltando.

A ordem depende de quem chega primeiro: na X55 o `display-subsystem` já estava
pronto (vop e hdmi bindados) e o DSI estava atrasado esperando o módulo
`phy_rockchip_inno_dsidphy` — então o `component_add` encontrou o conjunto
completo e bindou na hora. O oops que vem em seguida
(`drm_mode_config_cleanup` → `drm_connector_list_iter_next`) é a limpeza do
`rockchip_drm_bind` depois da falha, não a causa.

- Correção: patch local de kernel
  `buildroot/board/giochi/patches/linux/0001-drm-rockchip-dw-mipi-dsi-adia-component-add.patch`
  (+ `BR2_LINUX_KERNEL_PATCH` nos dois templates). Se `dsi->dmd` ainda for
  NULL, o `host_attach` marca `component_pending` e o `component_add` é feito
  **no fim** do probe, com `dmd` e `bridge` completos. O caminho normal e o
  dual-DSI/master-slave não mudam.
- **Regra que fica**: `-22` sem log nenhum é `drm_bridge_attach()` recusando
  ponteiro nulo — antes de instrumentar kernel, **olhe a pilha** do bind
  (o `dmesg` já a tem) e confira se algum campo do bridge estava sendo
  preenchido depois do `component_add`.
- `BR2_LINUX_KERNEL_PATCH` exige forçar o pacote: mudar o defconfig **não**
  re-arma os stamps. `rm build/linux-*/.stamp_{patched,built,installed,
  target_installed,images_installed}` antes do build (ou `make linux-dirclean`,
  que recompila tudo).

### 3.49 A API de conteúdo do GitHub trunca em 1000 entradas — e mente por omissão

Ao avaliar o SuperTux (ver [docs/SUPERTUX.md](docs/SUPERTUX.md)) precisei saber
quais pacotes o Buildroot 2024.02 tem. Pedi a listagem de `package/` e filtrei
por nome — o resultado foi **"supertux: não existe"**, **"sdl2: não existe"**,
"physfs: não existe". Os três existem.

`GET /repos/{dono}/{repo}/contents/{dir}` devolve **no máximo 1000 entradas**, e
`package/` do Buildroot passa de 3.000. A resposta não vem marcada como
incompleta (diferente da API de *tree*, que tem o campo `truncated`): ela
simplesmente acaba. Filtrar essa lista produz **falso negativo silencioso** —
justo o tipo de defeito que este arquivo cataloga. E a conclusão errada
("o Buildroot não empacota SuperTux") teria custado um pacote escrito à mão.

- **Sonde por caminho**, não por listagem: `contents/package/<nome>?ref=<tag>`
  devolve **200** ou **404**, sem ambiguidade.
- Para somar tamanhos de muitos arquivos, use a API de *tree*
  (`git/trees/<ref>?recursive=1`) e **cheque o campo `truncated`** antes de
  acreditar no total.
- Vale para qualquer inventário remoto: se a resposta tem limite de página,
  "não achei" só vale depois de esgotar as páginas.

### 3.50 O kernel não carregava NENHUM módulo: faltava coldplug no userspace

Boot #3 da X55 (imagem com o patch do §3.48, cartão verificado byte a byte):
a tela continuou apagada, mas por um motivo **diferente** e anterior:

```
[   10.998616] dw-mipi-dsi-rockchip fe060000.dsi: failed to get mipi dphy: -517
[   11.009544] platform fe060000.dsi: deferred probe pending: (reason unknown)
[    0.985407] ALSA device list: No soundcards found.
```

`-517` é `-EPROBE_DEFER`: o controlador DSI pediu o PHY, o PHY não existia e o
kernel pediu para tentar depois. Como `/lib/modules/6.12.0/` estava cheio de
`.ko` e **nenhum** havia sido carregado (`/proc/modules` vazio), o PHY
(`phy_rockchip_inno_dsidphy.ko`), o `rtw88` e o `hci_uart` nunca apareceram —
e o `deferred probe` **não re-tenta sozinho**.

Duas causas somadas:

1. **Não existe udev neste sistema.** Quem carrega módulo é o `request_module()`
   do kernel (que executa `/sbin/modprobe`) ou o `mdev` com uma regra de
   `$MODALIAS`. O `mdev.conf` do projeto tinha regras de permissão mas **nenhuma
   de modalias** — nenhum `modprobe` era disparado.
2. O `request_module()` do kernel só funciona **depois** que o rootfs existe, e
   os dispositivos de plataforma (DSI, PHY, áudio, SDIO) nascem por volta de
   0,5 s — antes do `Mounted root` (~1,2 s). Aqueles pedidos morrem com
   `-ENOENT` e nada os repete.

- Correção: `scripts/init.d/S05modules` roda `mdev -s` (coldplug) no início do
  `rcS`, e `buildroot/board/giochi/mdev.conf` ganhou a regra
  `$MODALIAS=.* root:root 660 @modprobe -b $MODALIAS`.
- `CONFIG_PHY_ROCKCHIP_INNO_DSIDPHY=y` (era `=m`): o PHY do DSI é pré-requisito
  do primeiro componente de vídeo; não faz sentido depender de carregar módulo
  para acender a tela.
- Guarda: suíte `primeiro-boot` (`test_coldplug_de_modulos_no_boot` e
  `test_phy_do_dsi_e_embutido`), testadas ao contrário.

**Regra que fica:** `-517` + `/proc/modules` vazio = o sistema não tem coldplug.
Antes de investigar o driver, olhe se algum `.ko` foi carregado.

### 3.51 Pacote local NÃO re-sincroniza: a imagem fica com o binário velho

Este é o defeito mais caro desta sessão, porque **o build termina verde**.

Depois do commit `39d17b1` (que corrigiu `device_profile.c` e o mapa de botões),
a imagem foi gerada, gravada, verificada byte a byte — e o aparelho continuou
registrando:

```
I: device_profile.json gravado ... (device=rk3326 soc=rk3326)
W: buttons.map: secao [efault] ausente
```

O código corrigido estava no repositório; o binário na imagem era o antigo. O
`SITE_METHOD = local` do Buildroot só roda o `rsync` do pacote quando ele é
**reconstruído**, e a versão (1.2) não muda — então o `.stamp_rsynced` de
`build/giochi-launcher-1.2/` nunca expirava.

- `tools/build-image.sh` agora apaga `build/giochi-*/.stamp_*` antes de cada
  build: força rsync + build + install. São segundos, contra um rebuild de
  kernel.
- Guarda: `test_build_ressincroniza_pacote_local` (suíte `primeiro-boot`).
- **Como detectar em 5 segundos**: `strings target/usr/bin/giochi-launcher |
  grep giochi_deviceconf`. Se o símbolo novo não estiver lá, a imagem é velha.

### 3.52 Falha de build anunciada como sucesso (`if !` desliga o `set -e`)

Junto com o §3.51, o log do build dizia:

```
Error: duplicate filename '0001-drm-rockchip-...patch'
make: *** [package/pkg-generic.mk:242: .../.stamp_patched] Error 1
----
imagens geradas:
  x55: .../giochi-x55-sdcard.img (148M)
EXIT=0
```

O `make` falhou **no kernel** e o script seguiu: encontrou o `sdcard.img`
**da execução anterior** em `images/`, copiou e anunciou "imagens geradas" com
`rc=0`. A imagem entregue ao aparelho era a de antes.

Causa: a função é chamada como `if ! build_device "$id"; then`, e **um comando
em contexto de teste desliga o `set -e`** dentro da função. Sem `||` explícito,
o `make` que falha não interrompe nada.

- As duas invocações de `make` (defconfig e build) agora têm `|| { ...; return 1; }`.
- Guarda: `test_build_nao_anuncia_imagem_quando_make_falha`.

### 3.53 `BR2_LINUX_KERNEL_PATCH` conta como "já aplicado" ao re-extrair

Depois de apagar `build/linux-6.12` para forçar re-extração, o build morreu em:

```
Error: duplicate filename '0001-...patch'
  already applied: /work/buildroot/board/giochi/patches/linux/0001-...patch
  to be applied  : /work/buildroot/board/giochi/patches/linux/0001-...patch
```

O `apply-patches.sh` do Buildroot registra no diretório `.patches/` o que já
aplicou, e esse diretório **não** é recriado do zero quando só o fonte é
removido. Use `make linux-dirclean` (ou apague `build/linux-*` inteiro,
incluindo `build/.patches`), não apenas a árvore do kernel.

### 3.54 `compatible` do RK3566 começa por `powkiddy` (fabricante), não por `rk`

`S20graphics` lia o primeiro campo de `/proc/device-tree/compatible` para
descobrir o SoC e registrava:

```
S20graphics: SoC 'powkiddy' nao reconhecido; usando defaults do Mesa
```

O `compatible` da X55 é `powkiddy,x55`, `rockchip,rk3566` — o primeiro campo é o
**fabricante**. Consequência: `MESA_LOADER_DRIVER_OVERRIDE` não era exportado
(na prática o panfrost é escolhido pelo próprio Mesa, então era ruído, mas o log
mentia sobre o aparelho).

- A identidade agora vem de `/etc/giochi/device.conf` (`GIOCHI_SOC=rk3566`); o
  device-tree é fallback, filtrando por `^rk[0-9]`.
- Guarda: `test_s20graphics_le_soc_do_device_conf`.

### 3.55 A API do itch.io devolve **HTTP 200** para erro de autenticação

Ao avaliar o itch.io como catálogo da Loja ([docs/ITCHIO.md](docs/ITCHIO.md)),
`GET https://itch.io/api/1/x/game/1` respondeu **200** e eu quase anotei "a API
não exige chave". O corpo era:

```json
{"errors":["invalid key"]}
```

O itch.io não usa 401/403: erro de chave volta como **200 com `errors` no
corpo**. Qualquer cliente que teste só o status (ou que desserialize ignorando
`errors`) conclui que está autenticado e recebe `null` no lugar dos dados — e a
Loja mostraria catálogo vazio "sem erro". É prima da §3.49 (a API que mente por
omissão), com o sinal trocado.

- **Regra**: ao integrar serviço de terceiro, valide o **corpo**, não o status.
  `curl -o /dev/null -w '%{http_code}'` responde "tudo certo" para esta API.
- Vale para o desenho do `giochi-store`: se ele reportar "0 jogos" sem erro, o
  usuário não distingue catálogo vazio de chave inválida.

### 3.56 `SDL2_KMSDRM` foi descartado em silêncio: SDL2 sem driver de vídeo

**Este é o defeito mais caro da série em horas perdidas**, porque todos os
sinais apontavam para o lado errado.

Estado medido no aparelho: DSI bindado, `/dev/dri/card0` presente, `fb0`
funcionando, tela acesa mostrando o console — e o Launcher dizendo

```
W: render: SDL2 indisponivel; usando backend headless
I: render: backend headless 1280x720
```

Ou seja: **o kernel estava certo**. A causa estava na compilação do SDL2:

```sh
grep SDL2 build/.config      # 8 horas depois do primeiro "não acende"
BR2_PACKAGE_SDL2=y
BR2_PACKAGE_SDL2_IMAGE=y
# BR2_PACKAGE_SDL2_KMSDRM is not set      <-- pedido no defconfig!
```

O defconfig pedia `BR2_PACKAGE_SDL2_KMSDRM=y` desde o início. O `Config.in` do
Buildroot exige **cinco** dependências, e uma delas não estava satisfeita:

```
depends on BR2_TOOLCHAIN_HAS_THREADS
depends on BR2_PACKAGE_HAS_LIBGBM
depends on BR2_PACKAGE_HAS_LIBEGL
depends on BR2_PACKAGE_SDL2_OPENGL || BR2_PACKAGE_SDL2_OPENGLES   <-- faltava
```

Sem `SDL2_OPENGLES` (ou `SDL2_OPENGL`) o Kconfig **descarta** o símbolo sem
uma linha de aviso — a armadilha §3.2, agora com consequência de tela apagada.
O `sdl2.mk` então roda `--disable-video-kmsdrm` e a biblioteca fica sem
**nenhum** driver de vídeo (X11, Wayland, dummy e offscreen também estão
desligados neste build).

- Correção: `BR2_PACKAGE_SDL2_OPENGLES=y` nos dois templates.
- Guarda nova: `tools/check-config-applied.py` compara o defconfig pedido com
  o `.config` aplicado e **o build falha antes de compilar**. Chamado por
  `tools/build-image.sh`; suíte `config-aplicada`.
- `render.cpp` passou a **imprimir o motivo** da queda para headless
  (`SDL_Init: ...`, `SDL_CreateWindow: ...`). Antes o log só dizia
  "indisponivel" e a mensagem era jogada fora, o que mandou a investigação
  para o kernel quando o problema era o usuário.

**Regra que fica:** `render=null` com `/dev/dri/card0` existe = o SDL2 da
imagem não tem KMSDRM. Confira com
`strings /usr/lib/libSDL2-2.0.so.0 | grep -c kmsdrm` — se der 0, é build, não
é driver.

### 3.57 Mudar o `.config` NÃO reconstrói o pacote afetado (`<pkg>-dirclean`)

Consequência direta do §3.56 depois de corrigir o símbolo: o build rodou
inteiro, gerou imagem, foi gravado e verificado byte a byte — e o SDL2 da
imagem continuava **sem KMSDRM**, com stamps de 6 horas antes. O Buildroot não
invalida um pacote quando a configuração dele muda; os `.stamp_*` continuam
válidos.

- `tools/config-delta.py` compara o `.config` aplicado com o da execução
  anterior e lista os pacotes cujos símbolos mudaram (mapeia
  `BR2_PACKAGE_<PKG>_*` → `package/<pkg>`, filtrando pelo que existe na
  árvore).
- `tools/build-image.sh` roda `<pkg>-dirclean` neles antes do `make`.
- **Tem de ser `dirclean`, não apagar os stamps.** Medido: só apagar
  `.stamp_*` reconfigura o pacote mas mantém os `.o` antigos, e o link morre em
  `undefined reference to 'DUMMY_bootstrap'` e `'SDL_EGL_CreateContext'`
  (objetos de backends que o novo configure não compila mais).
- Guarda: `test_build_reconstroi_pacote_quando_a_config_muda` e
  `test_config_delta_mapeia_simbolo_para_pacote` (suíte `config-aplicada`).

### 3.58 `mdev.conf`: `modprobe -b` não existe no BusyBox sem a feature

A primeira versão da regra de modalias (§3.50) usava `@modprobe -b $MODALIAS`,
copiada de receitas antigas. O `modprobe` do BusyBox só aceita `-b` com
`CONFIG_FEATURE_MODPROBE_BLACKLIST`, que **não** está ligado no Buildroot
2024.02 — o comando falharia com "unrecognized option" e nada seria carregado,
recriando o defeito. A linha correta é a que o próprio Buildroot instala:

```
$MODALIAS=.*	root:root 660 @modprobe "$MODALIAS"
```

- Guarda: `test_coldplug_de_modulos_no_boot` recusa `modprobe -b` sem a feature
  no `busybox.fragment`, e exige `FEATURE_MDEV_CONF`/`MDEV`.


### 3.59 `CONFIG_FB=n` no fragmento NÃO desliga o fbcon (`DRM_FBDEV_EMULATION`)

O `linux.fragment` declara `CONFIG_FB=n` desde a v1.2, com a justificativa
"boot rápido, ~1 MB de kernel". A afirmação do §3.31 — de que a imagem não tem
console de vídeo — é **falsa**. Medido no `.config` do build da X55:

```
# CONFIG_FB is not set            <- o fragmento pediu
CONFIG_DRM_FBDEV_EMULATION=y     <- default do DRM
CONFIG_FRAMEBUFFER_CONSOLE=y     <- o que realmente vale
```

`DRM_FBDEV_EMULATION` faz `select FB` e `select FRAMEBUFFER_CONSOLE` quando o
`DRM_KMS_HELPER` está ligado, então o `=n` do fragmento é descartado. O console
de vídeo **sempre existiu** nesta imagem — foi ele que apareceu na tela do
aparelho ("terminal com falha, cursor piscando") antes de o Launcher rodar.

Consequência que atrapalha a UI: o cursor do vt pisca e **repinta** a tela, e
qualquer `printk` novo pinta por cima do que o SDL desenhou — e este kernel
imprime `rk817-charger: Battery voltage 19590 below minimum` **a cada 8 s**.

- Decisão: **manter** o fbcon (é o diagnóstico mais barato que existe, e foi
  ele que mostrou o boot chegando ao userspace) e **entregar o framebuffer**
  em runtime no `S20graphics`: baixa `console_loglevel` para 1 e desvincula o
  `vtcon` do fbcon (`/sys/class/vtconsole/*/bind`), que é o que para o cursor e
  os printk. O log continua no serial (`ttyS2`) e no cartão (S00bootlog).
- `GIOCHI_KEEP_FBCON=1` mantém o console ligado (depurar com a tela).
- Remover de verdade exigiria `CONFIG_DRM_FBDEV_EMULATION=n` — não é o que a
  v1.2 faz.
- Guarda: `test_framebuffer_entregue_a_ui` (suíte `primeiro-boot`).

**Armadilha de método:** o comentário do fragmento dizia uma coisa e o
`.config` dizia outra havia várias sessões, e ninguém notou porque o efeito
(*mais* console, não menos) parece uma melhoria. Comentário de config não é
prova; `grep` no `.config` do build é.


### 3.60 `/var` é somente leitura: `seedrng` falha e a entropia não persiste

A raiz é SquashFS e o `inittab` só criava `/var/log` com `mkdir -p` — que
falhava calado, porque nem o diretório existia na imagem. Resultado medido no
serial da X55:

```
seedrng: can't create directory '/var/lib/seedrng': Read-only file system
...
[   11.005956] random: crng init done
```

O `crng` só ficou pronto aos **11 s** de uptime. Nada neste sistema precisa de
entropia para desenhar, mas ferramentas que usam `getrandom()` bloqueante
(o EGL do Mesa abre um `drm` e pode pedir bytes) ficam paradas — e o Launcher
levou exatamente ~9 s entre iniciar e desistir do vídeo.

- `inittab` monta **tmpfs em `/var`** antes do `rcS` e cria `log`, `lib/seedrng`,
  `run`, `tmp`. `/var` da imagem tem 20 KB de diretórios vazios, então montar
  por cima não esconde nada.
- Guarda: `test_var_e_gravavel` (suíte `primeiro-boot`).

**Regra que fica:** a raiz é somente leitura, então **todo** caminho que algum
programa escreve precisa estar em `/user`, `/run` ou num tmpfs. `/var`, `/tmp`,
`/etc` e `/usr` não são graváveis — e o erro aparece como "Read-only file
system" em um programa que não é o culpado.

### 3.61 "EGL not initialized" não diz nada: sonda antes de adivinhar

Depois de o DRM bindar e o SDL2 passar a ter KMSDRM, o Launcher registrou:

```
MESA-LOADER: failed to open kms_swrast: ... No such file or directory
MESA-LOADER: failed to open swrast: ... No such file or directory
W: render: SDL2 indisponivel (SDL_CreateWindow: EGL not initialized)
```

`EGL not initialized` é string do SDL para "o carregamento do EGL falhou" — não
diz **qual** dos cinco passos falhou, e cada um tem correção diferente: abrir
`/dev/dri/card0`, achar um conector com modo, `gbm_create_device`,
`eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR)`, `eglInitialize`,
`eglChooseConfig`, `eglCreateContext`.

- `src/tools/gpu_probe.c` (`/usr/bin/giochi-gpu-probe`) faz os cinco passos e
  imprime `OK`/`FALHOU` em cada um, com o nome do erro EGL (`EGL_BAD_MATCH`,
  não `0x3009`).
- `S20graphics` roda a sonda **depois** de entregar o framebuffer (com o fbcon
  vinculado o DRM tem outro dono e a sonda mediria outra situação) e cada linha
  vai para o log do cartão como `sonda: ...`. Escape: `GIOCHI_NO_GPU_PROBE=1`.
- Compila em `giochi-tools.mk` e **não** no `Makefile` da raiz: depende de
  libdrm/libgbm/EGL, que só existem no sysroot do Buildroot (mesmo critério do
  Launcher com SDL2).
- Guardas: `test_sonda_de_video_roda_no_boot` (ordem + instalação + escape).

**Armadilha de dependência:** `GIOCHI_TOOLS_DEPENDENCIES` precisa de `libdrm`,
**não** `drm` — o nome do pacote no Buildroot é `libdrm` e o erro é
`No rule to make target 'drm'`, que não sugere o nome certo.


## 4. O que NÃO foi verificado

Trate como hipótese até alguém colar evidência (log, captura do aparelho):

- **O userspace roda na X55, mas só foi visto por log.** Medido em hardware
  (entrada 19): `giochi-init pronto`, `launcher v1.2 pronto: 5 abas` e
  `primeiro frame` em 3,9 s. O que **não** está verificado é a *tela*: o DRM não
  faz bind do DSI (§3.44), então o Launcher desenha no backend nulo. Também não
  foi verificado nada de áudio, gamepad, Wi-Fi, `control.sock` pelo painel, nem
  o overlay de verdade (só o headless).
- **X35S e X35H** nunca foram testados por quem escreveu este código. Não têm
  DTS upstream nem defconfig de U-Boot; dependem de `--fallback-dts` (imagem de
  teste) ou de DTS fornecido por você. E, mesmo com o DTS certo, o **painel
  deles não acende** sem o `panel-generic-dsi` fora da árvore (§3.43) — lacuna
  adiada por decisão de licenciamento, não por falta de diagnóstico.
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

### 2026-10-31 (12) — Copilot (cópia recursiva: a causa real do CI morrer)

- Build local em container (Docker, 12 CPUs / 60 GB) chegou **além de onde o CI
  parou**: Mesa3D compilou (o ponto que morria por memória), e o build travou
  em `>>> giochi-assets 1.2 Syncing from source dir`.
- Causa raiz — **não era disco do runner**: os pacotes locais usavam
  `SITE = $(BR2_EXTERNAL_GIOCHI_PATH)/..` (raiz do repo) com
  `SITE_METHOD = local`, e o rsync do Buildroot copiava `dist/` para dentro de
  `dist/`, recursivamente. Medido: **66 GB** em `giochi-assets-1.2`, com
  `.../giochi-assets-1.2/dist/x55/build/build/giochi-assets-1.2/...` até o
  limite de caminho. Isso explica os três sintomas do CI: build travado, log
  truncado no meio de uma linha, e passos de limpeza que nunca rodaram.
- Correção: `build-image.sh` monta um snapshot só das fontes (~9 MB) em
  `dist/<id>/pkg-src`, exporta `GIOCHI_PKGSRC`, e todos os `.mk` de pacote
  local usam `SITE = $(if $(GIOCHI_PKGSRC),$(GIOCHI_PKGSRC),<raiz>)`.
- Guarda: `check-image-contents.sh` recusa `SITE_METHOD = local` sem
  `GIOCHI_PKGSRC` e exige o `export` no `build-image.sh`. Testada ao contrário
  (reintroduzi o SITE na raiz → rc=1).
- Também: `docker-build.sh` passou a exportar `BUILDROOT_DIR=/opt/buildroot`
  dentro do container (sem isso o wrapper falhava antes de compilar), e o
  Dockerfile ganhou `xz-utils` (o `post-image.sh` comprime o `.img`).
- Nota de operação: remover a árvore recursiva com `rm -rf` leva minutos.
  `rsync -a --delete <dir-vazio>/ <alvo>/` é bem mais rápido.

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

### 2026-10-30 (16) — Copilot (proposta de layout das 5 abas + perfil ×1,5 aplicado)

O mantenedor pediu uma **sugestão de layout documentada**, com **usabilidade
das 5 abas**, mantendo o tema; depois aprovou D1/D2 (perfil ×1,5 nativo e 5
colunas) e escolheu implementar no `render.cpp`.

**Entregue e verificado:**

1. [docs/LAYOUT-PROPOSTA.md](docs/LAYOUT-PROPOSTA.md): tabela de tokens
   640×480 → 1280×720, anatomia (topbar, hint bar por itens medidos, conteúdo),
   uma seção por aba (Home, Biblioteca, Música, Loja, Ports), análise de
   usabilidade e as decisões D1–D5 com estado.
2. `tools/make-layout-proposal.py` + `docs/proposta/` (10 PNGs nativos): desenha
   por painel e **falha (exit 1)** se um texto não cabe, um item de hint bar
   colide, a grade invade topbar/hint bar, a scrollbar encosta na coluna, o chip
   passa da barra, ou as colunas declaradas no asset não cabem na largura.
3. `assets/devices/x55/*.json` **reescritos** para o perfil ×1,5 (topbar 60,
   hint 45, tile 228×128, título 30, scrollbar 6, hero 168×95, recentes 120×68,
   capa 180, controles 75, fonte das abas 21, `columns: 5`). Antes eram 7
   fatores implícitos (1,28 a 2,00) e 24 thumbnails de 288×162 = **4,48 MB**,
   acima do teto de 4 MB da §4.1.6; agora 228×128 = **2,80 MB**.
4. `tests/test_device_scale.py` (suíte `escala-x55`, em `make test` e
   `make lint`): todo inteiro do arquivo do aparelho tem de ser
   `round(base × 1,5)`, política igual ao base, ou especial do painel
   (`screen.*`, `columns`); campo novo fora disso falha. Testado ao contrário
   (`tile 288` → `288 != 228 : esperado round(152 x 1,5)`).
5. `tests/test_layout_proposal.py` (suíte `proposta-layout`, 6 testes).
6. O gerador passou a **ler** os assets do aparelho; os 10 PNGs saíram byte a
   byte idênticos aos de antes (SHA-256), provando que o valor está no asset.

**Defeitos encontrados (medidos):**

- `make-screenshots.py` só tinha caminhos de fonte de Linux → no Windows caía em
  `ImageFont.load_default()` (10 px fixos) e todo texto saía com métrica errada.
  Adicionados Windows/macOS. (O "rótulo colado" que eu reportei antes era isso,
  não a imagem commitada: ela difere da regenerada em 1,6% dos pixels.)
- **Pillow ≥ 11** desenha contorno **branco** em `rectangle`/`rounded_rectangle`
  com `fill=None` e `outline=None`. Guarda em `Sheet.box`; o
  `test_nenhum_pixel_branco` falha se voltar.
- Scrollbar da Biblioteca no `make-screenshots.py` cobre 2 px da grade
  (`634..638` contra grade até `636`).
- §4.7 diz que o app externo reativa o Launcher **"na Home"**, mas o código só
  faz `SIGCONT` (`supervisor.c:429/389`) e o Launcher volta para a aba em que
  estava; e as abas 4/5 (`layout.cpp:42-43`) não têm JSON de layout. Vira D4.
- **Não há fonte no repositório** (`theme.json` pede `builtin_sans`) nem
  `SDL2_ttf`/freetype em nenhum defconfig: `render.cpp` não desenha um rótulo.
  É D5, e é o bloqueio do desenho.
- A polegada dos painéis não está documentada: legibilidade em mm é hipótese.

**Por que o `render.cpp` não foi escrito:** não há compilador nesta máquina
(`cl`, `clang`, `gcc`, `cmake`, `ninja` inexistentes; nenhum `SDL.h` no disco) e
o desenho depende de D5 (texto). Escrever sem poder compilar seria declarar
feito o que não rodou.

**Verificação:** `make-layout-proposal.py` exit 0 / 10 imagens / nenhuma
violação; `test_layout_proposal` 6/6; `test_device_scale` 3/3; `test_bootlogo`
OK, `test_default_games` 7/7, `test_image_layout` 9/9; `check-links.py` ok
(26 arquivos). **Não rodou:** `make`, `make lint`, qualquer compilação.
`test_screenshots` e `forge` falham aqui só por `PermissionError` do `tempfile`
sob o sandbox do Windows — não é bug do repositório.

**Aberto:** D4 (contrato do app externo) e D5 (pipeline de texto, recomendo
atlas gerado no build); desenho no `render.cpp`; `make-screenshots.py` continua
com letterbox; `docs/screenshots/*.png` ainda são o render antigo. Sem `git`
aqui, nada foi commitado.

### 2026-10-31 (16) — Copilot (build REAL da X55: do zero ao `sdcard.img`)

Primeira imagem completa gerada neste repositório: **`sdcard.img` de 936 MB
(147 MB de blocos reais, esparso) com U-Boot mainline, kernel 6.12, DTB do
`rockchip`, SquashFS de 29 MB em A e B, e `user.ext4`**. `make` saiu com
**rc=0** e `make test` **19/19**. Nada disso foi bootado num aparelho.

O caminho até aqui derrubou **quatro** defeitos, cada um no **último** passo do
build (depois de compilar toolchain + kernel + U-Boot + mesa3d + todos os
pacotes), e cada um com mensagem que não aponta o culpado. Todos agora têm
guarda no lint:

| # | Defeito | Onde aparecia | Guarda |
|---|---|---|---|
| §3.25 | `g` no campo de tipo do `device_table.txt` | `makedevs` → `squashfs.mk` | `tools/check-device-table.sh` |
| §3.26 | `partition-type-uuid` com tabela MBR (nos **dois** cfgs) | `genimage` → `target-post-image` | `tests/test_image_layout.py` |
| §3.27 | `%PARTUUID%` literal no `extlinux.conf` | kernel não montaria root | idem |
| §3.28 | `&&` como último comando → exit 1 com a imagem pronta | `target-post-image Error 1` | idem |

Antes disso, a armadilha §3.23 (cópia recursiva) e os dois erros de compilação
do `giochi-init` (defines de texto com aspas atravessando duas camadas de shell)
já haviam sido corrigidos: veja a entrada (15) e o `CHANGELOG.md`.

**Como foi verificado (tudo medido, nada estimado):**

- `fdisk -l` na imagem: MBR `dos`, p1 `0c` FAT32 LBA em 24 MiB / 80 MiB,
  p2/p3 `83` 160 MiB cada, p4 `83` 512 MiB. Total 936 MiB.
- `RKNS` (cabeçalho do boot ROM do Rockchip) em `0x8000`; os
  **9 401 856 B** do `u-boot-rockchip.bin` em 32 KiB são **byte a byte iguais**
  ao arquivo de origem (comparado em Python). O FIT (`d00dfeed`) do mainline
  fica em `0x2b3f8` dentro do blob, **não** em 6 MiB como no BSP do vendor —
  quem for gravar à mão precisa usar o arquivo inteiro, não só o `.itb`.
- `mdir` no `boot.vfat` extraído: `Image` 47 516 160 B, `board.dtb` 59 420 B,
  `dtbs/rk3566-powkiddy-x55.dtb`, `extlinux/extlinux.conf`, `splash.bmp`
  2 764 854 B e `boot.scr` 2 270 B.
- `xz -9 --lzma2=dict=192MiB`: **41 MB** para o `.img` de 936 MB (o mesmo xz do
  §3.21; o número de 27,8 MB era com um SquashFS de 29 MB **e** kernel menor —
  o kernel de 47 MB é hoje o que domina o `.xz`).

**Armadilha de ambiente (nova):** o `tools/docker-build.sh` só passava
`BUILDROOT_DIR`, `BR2_DL_DIR` e `JOBS` para dentro do container. Qualquer
`GIOCHI_*` exportada no host (`GIOCHI_NO_COMPRESS=1`, `GIOCHI_MIN_FREE_GB`) era
**silenciosamente ignorada** — o build comprimia mesmo com
`GIOCHI_NO_COMPRESS=1`. Agora as duas são repassadas com `-e`. Regra: variável
que só o `post-image.sh` lê precisa de `-e` explícito, senão o teste "com a
variável" e "sem a variável" dão o mesmo resultado.

**Para testar no aparelho:** grave `dist/x55/giochi-x55-sdcard.img` cru com
`dd` (ou `balenaEtcher`). O `dist/` fica com ~15 GB de árvore de build;
`dist/x55/pkg-src` (~9 MB) é recriado a cada build e pode ser apagado.

**Correção de fato:** o comentário do `genimage.cfg` e o `docs/DEVICES.md`
diziam que o `u-boot.itb` do mainline entra em 6 MiB. Isso vale para o BSP do
vendor (Rockchip/ROCKNIX) e para o layout que o `genimage` reserva; o
`u-boot-rockchip.bin` **mainline** monta idbloader + FIT **contíguos**
(idbloader em 0, FIT em 0x2b3f8). Só o idbloader é lido pelo boot ROM em
LBA 64; o resto é do SPL. Gravar o arquivo inteiro em 32 KiB funciona nos dois
casos, que é o que este repositório faz.

**Aberto:** a imagem nunca foi gravada nem bootou (é o próximo passo e depende
do aparelho); o `rg351v`/`rg353v` usam o mesmo caminho de DTS upstream e devem
gerar igual, mas só a X55 foi compilada; nada foi testado com U-Boot do vendor
(`genimage-no-uboot.cfg`).

### 2026-10-31 (17) — Copilot (log de boot no cartão e o porquê da tela preta)

O mantenedor relatou que a primeira tentativa de boot no aparelho "parece ter
dado erro" e pediu **log na sdcard** para poder saber onde parou. O pedido
virou também uma caçada: três dos quatro defeitos abaixo **impediam** o boot ou
o log, e nenhum deles produzia mensagem.

**1. O U-Boot carregava o `boot.scr` do lugar errado (§3.29).**
`CONFIG_BOOTCOMMAND="load mmc 0:1 ... boot.scr"`. Em
`rk3566-powkiddy-x55.dts`, `mmc0 = &sdhci` (eMMC) e `mmc1 = &sdmmc0` (cartão).
A X55 não tem eMMC soldado: `load` falha, o `boot.scr` não roda, o kernel não
inicia e a tela fica preta sem uma linha de log. Agora o `BOOTCOMMAND` sonda
`for d in 1 0` e publica o que funcionou em `giochi_mmc`; `uboot.env` foi para
`1:1` (em `0:1` o `saveenv` falhava, e sem env o rollback por `bootcount` do
ADR-01 não funciona).

**2. `LABEL=GIOCHI_USER` no `fstab` não funcionava (§3.30).**
O BusyBox da imagem estava com `# CONFIG_FEATURE_MOUNT_LABEL is not set`
(medido no `.config` do 1.36.1 do build real). Sem essa opção o `mount` trata a
string como nome de arquivo: `mount -a` falha, `/user` nunca é montada e o
sistema inteiro escreve num diretório dentro do SquashFS somente-leitura. Era o
único lugar gravável do sistema.

**3. `boot.scr` de 27/10 numa imagem de 31/10 (§3.33).**
O `boot.scr` é gerado nas etapas de build do **host-uboot-tools**, que não é
reconstruído quando o `boot.cmd` muda. Quatro dias de mudanças no script de
boot nunca chegaram ao cartão. Agora o `post-image.sh` regenera sempre.

**4. Scripts de init de pacote roubando o nosso log (§3.34).**
`S01syslogd`/`S02klogd` (busybox.mk) subiam um segundo `syslogd` **sem `-O`** —
e o `syslogd` do BusyBox desvincula `/dev/log`, então o daemon do `S00bootlog`
perdia o socket e o log no cartão parava de crescer, sem erro. Pior:
`S50dropbear` (dropbear.mk) subia **SSH em todas as interfaces, sempre**,
anulando o adendo 4.6.1 e o `S62sshd`. Os três saem no `post-build.sh`.

**O que foi entregue para o pedido original:**

- `scripts/init.d/S00bootlog` — primeiro script de `rcS`. Monta a partição FAT
  (p1) em `/boot` e grava `giochi-boot.log`, `giochi-boot.1.log` (boot
  anterior), `giochi-boot.dmesg` e `giochi-boot.inprogress`. Se o marcador
  ainda existir no próximo boot, o log abre com "O BOOT ANTERIOR NÃO TERMINOU".
  `syslogd -O` grava com `write()` direto (sem buffer), então sobrevive a
  desligamento bruto; `klogd` repassa o kernel; `rcS` duplica a saída do resto
  com `tee -a`. Fallback: `/user`, depois `/run` — nunca aborta o boot.
  Desligar: `touch nolog` na partição 1.
- `linux-debug.fragment` + `tools/build-image.sh <id> --debug`
  (`GIOCHI_DEBUG_BOOT=1`): liga `fbcon` para o kernel escrever **na tela**.
  A lista de fragmentos é **derivada** do defconfig (não repetida) e a árvore de
  build é a mesma (incremental); só o nome do artefato muda, para
  `...-sdcard-debug.img`.
- `boot.cmd` ganhou `echo` de progresso no serial (`ttyS2`, `1500000 8N1`) e
  `giochi_fbcon` para rotação do console (`fbcon=rotate:N`).
- [docs/BOOT-DEBUG.md](docs/BOOT-DEBUG.md): como coletar, o que cada arquivo
  significa, tabela sintoma → causa, e a limitação honesta (fbcon só aparece
  quando o DRM sobe; antes disso, só serial).

**Verificação (tudo rodado):**

- `S00bootlog` executado num harness com `mount`/`mountpoint`/`syslogd`/`klogd`
  simulados: destino FAT (principal), `/user` (fallback) e `/run` (último
  recurso); rotação `.log` → `.1.log`; detecção de "boot anterior não
  terminou"; `nolog` desliga e não toca no arquivo. O harness revelou um bug
  **meu**: a listagem de `/proc/partitions` com `sed` perdia o *major* e casava
  com o cabeçalho — trocado por `awk`.
- Build real: `rc=0`; na imagem final, `/etc/init.d` tem `S00bootlog` e **não**
  tem `S01syslogd`, `S02klogd` nem `S50dropbear`; `/boot/` existe no
  `rootfs.tar` (a raiz é somente-leitura, não dá para `mkdir` em runtime).
- `.config` do busybox: `CONFIG_FEATURE_MOUNT_LABEL=y`, `SYSLOGD`, `KLOGD`,
  `TEE`. `.config` do U-Boot: `BOOTCOMMAND` com `for d in 1 0`,
  `ENV_FAT_DEVICE_AND_PART="1:1"`, `SYS_MMC_ENV_DEV=1`.
- `boot.scr` de 3171 B (era 2270 B, de 27/10) e, extraído do `.img`, contém os
  marcadores `GIOCHI: boot.scr em execucao` e `booti`. Imagem de debug:
  `CONFIG_FB=y`, `CONFIG_FRAMEBUFFER_CONSOLE=y`, `fbcon.o` compilado.
- `make lint` ok (nova seção "log de boot e dispositivo de boot").
  `make test` **20/20**. As 6 guardas novas foram testadas ao contrário
  (reintroduzir `mmc 0`, `ENV_FAT` em 0, tirar `/boot` da device_table, tirar
  `MOUNT_LABEL`, tirar o `rm` dos scripts de pacote, tirar o `tee` do `rcS`):
  todas reprovam; corrigidas, voltam a OK.

**Artefatos prontos:** `dist/x55/giochi-x55-sdcard.img` (normal) e
`dist/x55/giochi-x55-sdcard-debug.img` (com fbcon). Nenhum dos dois foi
bootado — o aparelho é o próximo passo.

**Aberto:** o `dist/` tem uma árvore de build só (~15 GB); alternar entre
normal e `--debug` recompila o kernel (fragmento diferente), uns 6 min no
container. `S60mtpd` (umtprd) também sobe MTP incondicionalmente — não conflita
com a decisão atual (MTP é o modo padrão), mas entra na mesma revisão de
scripts de pacote quando o mux de USB for fechado. O `S40network`/`S40bluetoothd`
seguem a mesma pergunta em aberto: quem decide, o pacote ou o `giochi-init`?

### 2026-10-31 (18) — Copilot (serial no aparelho: o boot chegou ao kernel)

O mantenedor ligou um adaptador CH340, e o serial respondeu tudo o que oito
tentativas de adivinhação não responderam. Dois boots capturados.

**Boot 1 — imagem antiga (sem as correções):**
`boot.scr` rodou, `Image` leu 47 516 160 B e o `booti` falhou por device tree
(`Failed to load 'dtbs/rockchip/rk...'`). Foi essa linha que fechou o §3.38.

**Boot 2 — imagem corrigida (a que está no cartão agora):**

```
GIOCHI: boot.scr em execucao (mmc=1 slot=2 root=PARTUUID=47494f43-02)
GIOCHI: dtb dtbs/rockchip/rk3566-powkiddy-x55.dtb
GIOCHI: Image carregada de mmc 1:1
GIOCHI: booti kernel=0x02000000 fdt=0x12000000
Starting kernel ...
mmcblk1: mmc1:5048 SD128 116 GiB
 mmcblk1: p1 p2 p3 p4
VFS: Mounted root (squashfs filesystem) readonly on device 179:2.
```

**Todas as correções da entrada (17) estão provadas em hardware**: DTB com
subdiretório, `PARTUUID` resolvendo a raiz, `boot.scr` regerado, kernel subindo.

**O que ainda falha — e não é build (§3.42):** três `I/O error` em
`mmcblk1` (setores 213088/213180/213184, ~104 MiB), `SQUASHFS error: Failed to
read block 0x66: -5` e `No working init found`. Medido que a imagem **está
íntegra** (`unsquashfs` extraiu tudo, rc=0) e que aqueles setores têm dados
válidos no arquivo. Portanto: gravação/mídia. O cartão usado era SDXC de 116 GB.

**Feito a partir disso:**

- `tools/verify-card.py` + suíte `verificador-de-cartao` (8 checagens, testadas
  com arquivos regulares; o caso de EIO é verificado no código, não é
  simulável). Separa "gravação incompleta" de "setor ruim", que exigem ações
  opostas.
- `docs/BOOT-DEBUG.md` §7 com o sintoma, a ordem de diagnóstico e a dica de
  `max-frequency` no `&sdmmc0` para cartão marginal em SDR104.
- Cartão novo (29,7 GB) gravado com `dd conv=fsync` e **verificado byte a byte**:
  `sha256` do cartão = `sha256` da imagem
  (`fd09e7df…5166`); MBR com `43 4f 49 47` (→ `PARTUUID=47494f43-02`) e as 4
  partições (`GIOCHI_BOOT` 80M, system-a/b 160M, `GIOCHI_USER` 512M) conferidas
  no dispositivo, não só na imagem.

**Armadilha de método**: dois containers lendo `/dev/ttyUSB0` ao mesmo tempo
corrompem o log — cada um rouba bytes do outro, e o texto sai truncado como se
fosse problema de baud. Antes de acusar o adaptador, verifique se há **um**
leitor (`docker ps`, `fuser /dev/ttyUSB0`).

**Aberto:** o X35H segue usando imagem de X55 (painel 640×480 × 720×1280); o
build `x35h --fallback-dts` estava em andamento. Se o novo cartão bootar, o
próximo dado é o `giochi-boot.log` na partição FAT — ele existe independente de
painel.

### 2026-10-31 (19) — Copilot (PRIMEIRO BOOT COMPLETO na X55; tela ainda apagada)

Cartão novo (29,7 GB, gravado e verificado byte a byte) na X55. **O sistema
subiu inteiro pela primeira vez em hardware.** Log serial (`tools/serial-capture.py`):

```
GIOCHI: boot.scr em execucao (mmc=1 slot=2 root=PARTUUID=47494f43-02)
GIOCHI: dtb dtbs/rockchip/rk3566-powkiddy-x55.dtb
[    0.000000] Booting Linux on physical CPU 0x0000000000
[    3.270510] rockchip-drm display-subsystem: bound fe040000.vop
[    3.841] I: control.sock ativo em /run/giochi/control.sock
[    3.842] I: Giochi Konsol OS v1.2 - giochi-init pronto (soc=rk3326)
[    3.934] I: launcher v1.2 pronto: 5 abas, default 'home', render=null
[    3.934] I: render: primeiro frame, aba 'home'
```

**3,9 s** do `Starting kernel` ao primeiro quadro. A cadeia toda (DDR → U-Boot →
`boot.scr` → DTB em `dtbs/rockchip/` → kernel → SquashFS por `PARTUUID` →
`rcS` → `giochi-init` → Launcher) está provada em aparelho.

**A tela está apagada e a causa não é o painel nem o SDL2:**

```
dw-mipi-dsi-rockchip fe060000.dsi: [drm] *ERROR* Failed to bind: -22
rockchip-drm display-subsystem: failed to bind fe060000.dsi: -22
```

Sem DRM não existe `/dev/dri/card0`, e o `render: SDL2 indisponivel` do log é
**consequência** disso (o backend cai para `null`). Cadeia lida no fonte 6.12
desta árvore, com os pontos que devolvem `-22`, em §3.44. Descartado com
evidência: drivers `=y`/`=m` misturados (tudo `=y`) e DTS sem painel (o DTB da
imagem tem `panel@0` `himax,hx8394`, backlight, reset-gpios, rotation 270). O
patch da ROCKNIX para a X55 (`0012-...x55...patch`) **não toca** DSI/painel —
ou seja, eles bootam com o mainline como nós, então a diferença está na nossa
configuração de kernel, não no DTS.

**Quatro defeitos silenciosos do mesmo log, corrigidos nesta entrada:**

1. `device_profile.json` dizia `(device=rk3326 soc=rk3326)` na X55: os `-D` de
   identidade não chegavam na compilação. Agora `device.conf` (que está na
   imagem) manda, e o `-D` é só fallback. Medido com o `device.conf` real:
   `(device=x55 soc=rk3566)`.
2. `buttons.map: secao [efault] ausente` — a chave tem 19 caracteres e o código
   pulava 20. Mapa do usuário descartado em silêncio. Novo
   `src/common/giochi_deviceconf.c` (chave por `strlen`).
3. `Usage: /etc/init.d/S10mdev {start|stop|restart}` — `rcS` chamava o serviço
   duas vezes (`start || sem-argumento`). `S10mdev` saiu da imagem.
4. `/user/music` e `/user/games` inacessíveis (0 pacotes) — os diretórios do
   SquashFS ficam escondidos sob o `user.ext4` vazio. `S10storage` semeia a
   árvore a cada boot.

Armadilhas novas: §3.45, §3.46, §3.47; o bloqueio de vídeo em §3.44.

**Verificação (tudo rodado nesta sessão):**
- `make test` → **24/24** (duas suítes novas: `test_deviceconf`, 15 checagens;
  `primeiro-boot`, 5 testes).
- `make lint` → rc=0.
- `tests/test_primeiro_boot.py` cobre, item por item, os quatro defeitos acima.
- Identidade provada rodando o `giochi-init` de host com
  `GIOCHI_DEVICE_CONF` apontando para o `device.conf` da imagem.

**Aberto (não declarar feito):**
- **Vídeo**: falta dizer qual chamada devolve `-22` no bind do DSI (§3.44). Não
  é a mesma coisa que §3.43 (X35S/X35H sem driver).
- `rtw8821cs: probe failed with error -22` — firmware `rtw88/rtw8821c_fw.bin`
  não empacotado (Wi-Fi morto; BT tem só o `config.bin`).
- `rk817-charger: Battery voltage 17070 below minimum` a cada 8 s (leitura de
  ADC, polui o log) e ausência de `regulatory.db`.
- Áudio, gamepad (`rocknix-joypad` não empacotado), `control.sock` pelo painel
  e o overlay de verdade continuam não verificados.
- Push para o GitHub continua bloqueado por credencial; estado em
  `/home/charles/giochi-builder/pending.bundle`.

### 2026-10-31 (20) — Copilot (vídeo da X55: o `-22` era bind dentro do probe)

O log que o mantenedor trouxe do cartão (`/boot/giochi-boot.log`, 1 141 linhas)
tinha a peça que faltava desde a entrada 19: **a pilha do bind**. O serial
resumido só mostrava `Failed to bind: -22`.

```
rockchip_drm_bind
  <- try_to_bring_up_aggregate_device <- component_add
  <- dw_mipi_dsi_rockchip_host_attach <- dw_mipi_dsi_host_attach
  <- mipi_dsi_attach <- hx8394_probe <- mipi_dsi_device_register_full
  <- mipi_dsi_host_register <- dw_mipi_dsi_probe <- dw_mipi_dsi_rockchip_probe
```

O painel MIPI é sondado **de forma síncrona** dentro de `dw_mipi_dsi_probe()`, e
o `component_add()` do `host_attach` fez o master do DRM bindar **dentro do
probe do DSI**, antes do `return`. Nesse instante `dsi->dmd` era NULL (é o valor
de retorno de `dw_mipi_dsi_probe()`) e `dsi->bridge` ainda não tinha
`funcs`/`driver_private`. `bridge` é o **primeiro** campo de
`struct dw_mipi_dsi`, logo `&NULL->bridge == NULL`, e `drm_bridge_attach()` tem
um único caminho que devolve `-EINVAL` **sem logar**
(`if (!encoder || !bridge) return -EINVAL`). A ausência da linha
`failed to attach bridge ... to encoder ...` era a pista, não um log faltando.

- Correção: `buildroot/board/giochi/patches/linux/0001-drm-rockchip-dw-mipi-dsi-adia-component-add.patch`
  + `BR2_LINUX_KERNEL_PATCH` nos dois templates. Se `dsi->dmd` for NULL, guarda
  `component_pending` e registra o componente no fim do probe.
- Guarda nova: suíte `patch-de-kernel` (`tests/test_kernel_patch.py`, 9 testes) —
  formato do diff, presença nos dois templates **e** nos defconfigs gerados
  (pega "esqueci o `make defconfigs`"), e aplicação limpa na árvore de `dist/`
  quando houver. Testada ao contrário: sem o arquivo, 5 falhas.
- **Armadilha do build**: `BR2_LINUX_KERNEL_PATCH` **não** re-arma os stamps do
  pacote `linux`. Trocado o defconfig, é preciso apagar
  `build/linux-*/.stamp_{patched,built,installed,target_installed,images_installed}`
  (ou `make linux-dirclean`) — senão o build fica verde e a imagem sai sem o
  patch, exatamente o tipo de defeito silencioso desta lista.
- Imagem regravada: `dist/x55/giochi-x55-sdcard.img`, kernel `#3`
  (`Image` `7c75fda…`, `boot.scr` `e5eb7a3…`), `dd conv=fsync` +
  `tools/verify-card.py` → *"idêntico à imagem, byte a byte"*.
- **Achado do mesmo log**: o cartão que estava no aparelho rodava a imagem de
  debug de 18:26, não a de 19:14 — o log tem `(device=rk3326 soc=rk3326)`,
  `secao [efault]` e `'/user/music' inacessivel`. As quatro correções do
  `39d17b1` **nunca foram testadas em hardware**. A imagem nova as leva.
- `make test` → **25/25**. `make lint` → rc=0.
- **Aberto**: o boot com o patch ainda não aconteceu. Se o bind falhar de novo,
  agora ele **imprime** `failed to attach bridge ... to encoder ...: -22`, que
  diz qual bridge recusou — antes esse log não existia porque o ponteiro era
  nulo antes de chegar lá.

### 2026-10-31 (21) — Copilot (viabilidade do SuperTux: 40 fps, tamanho e licença)

O mantenedor perguntou se valia adicionar o **SuperTux** e se **40 fps** não
estraga a jogabilidade. Resposta e decisão em
[docs/SUPERTUX.md](docs/SUPERTUX.md); **nada foi adicionado** (decisão:
"não adicionar agora, só registrar a análise").

**A pergunta dos 40 fps tem resposta no fonte, não em opinião.** O 0.6.3 separa
passo lógico de desenho (`ScreenManager::loop_iter`): `ms_per_step = 15 ms`
(`LOGICAL_FPS = 64.0`, mas o `static_cast<Uint32>(1000/64)` **trunca** para 15,
então o passo real é 66,7 Hz), `steps = elapsed_ticks / ms_per_step` e
`max_steps_per_frame = ceilf((1/fps)/0.015)` limitado a 4. A 40 fps sobram 1,67
passos por quadro e o teto calculado é 2: **a física roda correta e a velocidade
não muda**; o teto só morde abaixo de ~16,7 fps, onde o jogo desacelera de
propósito para o jogador ainda conseguir controlar o Tux. O que 40 fps custa é
fluidez da rolagem, não controle.

**Três bloqueios, nenhum de desempenho:**

1. **Tamanho (o decisivo).** `data/` = **234 MB crus em 4.061 arquivos**;
   compressão medida por amostra (`.ogg` 1,09x, `.png` 1,10x, `.stl` 23,89x,
   `.po` 5,71x, `.wav` 1,99x, `.otf` 1,33x) dá **≈160 MB** no SquashFS. Com os
   28 MB atuais, **188 MB contra a partição `system-a` de 160 MB**. Os 132 MB
   de áudio+imagem já estão comprimidos, então nenhum ajuste de SquashFS
   resolve; só caberia sem música (≈108 MB) ou aumentando a partição — e aí o
   `.xz` publicado sairia de 41 MB para ~170 MB.
2. **Licença.** `GPL-3.0+` no código **e** `CC-BY-SA`/`GPL-2.0+` nas imagens,
   música e sons. Mesmo §6 (*Installation Information*) × raiz assinada já
   discutido na entrada (14); o caminho é `/user`, como lá.
3. **Contrato de jogo padrão.** O schema exige `runtime ∈ {godot, retroarch,
   proton}` e `scripts/bench.gd` + `tests/run_tests.sh`; SuperTux é binário
   nativo C++. Nem entraria em `assets/default-games.json` com licença livre.

**Plataforma, para a próxima tentativa:** o Buildroot 2024.02 **tem** o pacote
(`supertux 0.6.3`), mas o `Config.in` exige `BR2_PACKAGE_XORG7` e
`HAS_LIBGL` (GLX), que não temos, e o `.mk` fixa `-DENABLE_OPENGLES2=OFF`. O
CMake **suporta** GLES2 (`pkg_check_modules(GLESV2)`), que é o que o nosso
Mesa/panfrost + SDL2 KMSDRM entrega — o patch é habilitar GLES2 e cair
`XORG7`/`libgl`/`libglew`. A versão 0.7.0 está fora: depende de **SDL3**, que
não existe no Buildroot 2024.02.

**Armadilha nova (§3.49):** a API de conteúdo do GitHub trunca em 1000 entradas
e, filtrando a listagem de `package/`, cheguei a "supertux/sdl2 não existem" —
ambos existem. Sondar por caminho (200/404), e checar `truncated` na API de
tree.

**Verificação (tudo medido, nada estimado sem dizer que é estimativa):**
tamanho e razões de compressão por amostra local com `zstd -19`; exigências e
versão lidas do `supertux.mk`/`Config.in` do Buildroot 2024.02.x; passo lógico
lido do `screen_manager.cpp`/`constants.hpp` no tag `v0.6.3`. **Não rodou:**
build do SuperTux, e **nada em aparelho** — o fps real no Mali-G52/G31 continua
desconhecido.

### 2026-10-31 (21) — Copilot (o patch entrou, mas nenhum módulo carregava)

Boot #4 da X55 com o patch do DSI (cartão verificado byte a byte). A tela
continuou apagada, e o log mostrou um bloqueio **diferente e anterior**:

```
[   10.998616] dw-mipi-dsi-rockchip fe060000.dsi: failed to get mipi dphy: -517
[   11.009544] platform fe060000.dsi: deferred probe pending: (reason unknown)
[    0.985407] ALSA device list: No soundcards found.
```

`-517` = `-EPROBE_DEFER`. Medido: `/lib/modules/6.12.0/` cheio de `.ko` e
**nenhum** carregado. Três defeitos independentes, todos silenciosos:

1. **Não havia coldplug (§3.50).** Sem udev, quem carrega módulo é
   `request_module()` do kernel (que só funciona depois do rootfs) ou o `mdev`
   com regra `$MODALIAS` — e o `mdev.conf` do projeto não tinha essa regra.
   Novo `scripts/init.d/S05modules` faz `mdev -s`; a regra entrou no
   `mdev.conf`. `CONFIG_PHY_ROCKCHIP_INNO_DSIDPHY` passou a `=y` (o PHY do DSI
   é pré-requisito do primeiro componente de vídeo).
2. **Pacote local não re-sincronizava (§3.51).** Era por isso que o aparelho
   continuava dizendo `(device=rk3326 soc=rk3326)` e `secao [efault]` mesmo
   depois do commit `39d17b1`: o `.stamp_rsynced` do Buildroot nunca expirava.
   `tools/build-image.sh` agora apaga os stamps de `build/giochi-*`.
3. **Build falho anunciado como sucesso (§3.52).** O `make` morreu no kernel
   (`duplicate filename` do patch) e o script seguiu, achou o `sdcard.img`
   anterior e imprimiu "imagens geradas" com `rc=0`. Contexto `if !` desliga o
   `set -e`; agora cada `make` tem `||`.

Também: `S20graphics` lia `powkiddy` como SoC (§3.54) e o Wi-Fi ganhou
`rtw88/rtw8821c_fw.bin` + `regulatory.db` (antes: `probe with driver rtw_8821cs
failed with error -22`).

**Verificação (tudo rodado):**
- `make test` → **25/25**; `make lint` → rc=0. Guardas novas em
  `primeiro-boot` (coldplug, PHY embutido, resync do pacote, `make` com `||`,
  SoC do `device.conf`), testadas ao contrário.
- Imagem: launcher **com** `giochi_deviceconf` (`strings | grep`), `S05modules`
  instalado, `mdev.conf` com a regra, `rtw88/rtw8821c_fw.bin` presente,
  `phy-rockchip-inno-dsidphy.ko` **ausente** (agora `=y`).
- `dd conv=fsync` + `verify-card.py` → *"idêntico à imagem, byte a byte"*
  (`sha256` `3530bb1b…`). Kernel `#4` (`Image` `e60baa04…`).

**Aberto:** o boot com o coldplug ainda não aconteceu. Se o DSI reclamar de novo,
o `-517` deve desaparecer; se aparecer outro erro, agora ele é **de driver**, não
de módulo faltando.

### 2026-10-31 (22) — Copilot (itch.io como fonte da Loja: não serve, e a prova é um `enum`)

O mantenedor quis usar o **itch.io** para adquirir jogos grátis e pôr no app da
Loja. Análise em [docs/ITCHIO.md](docs/ITCHIO.md); **nada foi adotado**.

**O dado que fecha a questão não é licença, é modelagem.** O modelo de dados
oficial do itch.io (`go-itchio`, a lib do `butler` e do app oficial) define
arquitetura como:

```go
type Architectures string
const (
    ArchitecturesAll   = "all"
    Architectures386   = "386"
    ArchitecturesAmd64 = "amd64"
)
```

**Não existe `arm64`.** O itch.io é um mundo x86 por modelagem: quem compila
para ARM só pode marcar "Linux", e a arquitetura é inexpressável. Logo a Loja
**não consegue filtrar o que roda em aarch64 antes de baixar** — teria que
baixar cada candidato e ler o cabeçalho ELF (`e_machine == 0xB7`).

**Oferta medida:** busca no catálogo inteiro devolve **18 jogos** para `arm64`
e **0** para `aarch64`, contra ≥54 (satura a página) para `linux` e `godot`, num
catálogo de **1.718.158** títulos. Amostra de 36 jogos do "top free games":
Linux 23 (64%), Windows 33 (92%), "Play in browser" 14 (39%), Android 13 (36%).
Os 39% de HTML5 exigiriam motor de navegador com WebGL2 (maior que o orçamento
inteiro da UI); os APKs arm64 exigiriam runtime Android (Waydroid) num aparelho
de 1–2 GB — incompatível com a premissa. E rodar x86 por emulação é **proibido
pela ADR-13** ("sem QEMU/Box86 na base", ver `docs/architecture-v1.1.md`).

**Licença:** "grátis" no itch.io é custo zero, não licença livre. Os ToS dão ao
usuário licença para usar/reproduzir/distribuir o conteúdo
"**as permitted through the functionality of the Service**" — ou seja, pelo
cliente, no próprio aparelho. Pré-instalar jogos na imagem é **fora** disso, e
os "Publishers retain all ownership rights": autorizar redistribuição é caso a
caso, com cada autor.

**O que funciona:** `butler` (CLI oficial) é **MIT** e publica
`butler-linux-arm64.zip` (9,3 MB); o login **não precisa de navegador**
(`BUTLER_API_KEY`, `BUTLER_MANUAL_OAUTH=1`). Então um cliente de download é
viável — só não resolve arquitetura, que é o problema real.

**Recomendação registrada:** priorizar o **PortMaster** (`PortsMaster/PortMaster-New`,
MIT, 384 estrelas) como fonte da aba Ports/Loja — é ARM por construção, e o
repositório **já reservou a aba para ele** (`APP:portmaster` em `layout.json`).
Segundo caminho: jogos do itch.io publicados como **projeto Godot** (re-exportar
para aarch64, sem problema de arquitetura, dependendo do template musl do
ADR-03 e de licença que permita recompilar).

**Armadilha nova (§3.55):** a API do itch.io devolve **HTTP 200** com
`{"errors":["invalid key"]}` para erro de autenticação. Quem testa só o status
conclui que está autenticado.

**Verificação:** tudo medido em 2026-10-31 — `go-itchio/types.go`,
`butler` releases v15.32.0, ToS em `itch.io/docs/legal/terms`, busca e amostra
do catálogo. **Não rodou:** nenhum build de loja, e o `giochi-store` continua
não existindo (a aba Loja segue com `ERR launch_failed`).

### 2026-10-31 (22) — Copilot (a tela ACENDEU; o `render=null` era o SDL2 sem KMSDRM)

O mantenedor relatou **"apareceu imagem na tela"** com a imagem que estava no
aparelho. Sim: com o coldplug (§3.50) e o PHY do DSI `=y`, o bind aconteceu:

```
[drm] Initialized rockchip 1.0.0 for display-subsystem on minor 0
Console: switching to colour frame buffer device 90x80
rockchip-drm display-subsystem: [drm] fb0: rockchipdrmfb frame buffer device
```

`fe060000.dsi` está na lista de `bound`. O kernel **está resolvido** — os
`-22` e `-517` acabaram. O que sobrou é do userspace:

```
W: render: SDL2 indisponivel; usando backend headless
```

E a causa estava no `.config` desde o primeiro build: `BR2_PACKAGE_SDL2_KMSDRM`
foi **descartado em silêncio** pelo Kconfig porque falta `SDL2_OPENGLES` (§3.56).
O SDL2 da imagem não tinha **nenhum** driver de vídeo.

Três correções + duas ferramentas novas:

| O quê | Onde |
|---|---|
| `BR2_PACKAGE_SDL2_OPENGLES=y` (destrava o KMSDRM) | dois templates |
| `BR2_PACKAGE_BUSYBOX_SHOW_OTHERS=y` (destrava `KMOD_TOOLS`) | dois templates |
| `render.cpp` imprime **o motivo** da queda para headless | `src/launcher/render.cpp` |
| `tools/check-config-applied.py` — pedido × aplicado, falha o build | novo |
| `tools/config-delta.py` — quais pacotes a mudança de config afeta | novo |

**Armadilha do caminho (a que mais custou):** corrigir o símbolo não bastou. O
Buildroot não reconstrói o pacote quando a config muda, e apagar só os stamps
leva a `undefined reference to 'DUMMY_bootstrap'` (os `.o` antigos ficam). Tem
de ser `<pkg>-dirclean` (§3.57). Depois disso o `libSDL2` foi de 0 para 6
ocorrências de `kmsdrm`, com `drmModeAddFB` e `gbm_create_device`.

**Verificação (tudo rodado):**
- `make test` → **26/26** (suíte nova `config-aplicada`, 7 testes, incluindo o
  par defconfig/.config sintético que prova a detecção).
- `make lint` → rc=0.
- `tools/check-config-applied.py` contra o `.config` real: falhava com
  `SDL2_KMSDRM AUSENTE` e `KMOD_TOOLS AUSENTE`; agora `ok`.
- `strings target/usr/lib/libSDL2-2.0.so.0 | grep -c kmsdrm` → **6** (era 0).
- Imagem regravada (`dd conv=fsync`) + `verify-card.py` → *"idêntico à imagem,
  byte a byte"* (`sha256` `19a7f5bd…`).

**Aberto:** o boot com o SDL2 correto ainda não aconteceu. Se a tela mostrar o
Launcher, o próximo item é o desenho dos widgets (`TODO(v1.2)` em
`render.cpp`). Se cair para headless de novo, agora o log diz **por quê**.

### 2026-10-31 (23) — Copilot (correção: fonte sim, mas o sujeito é o autor — e o template é glibc)

O mantenedor apontou um furo na entrada anterior: *"se o itch.io liberar o código
fonte não seria possível compilar em arm64?"*. **Está certo, e o §3 do
[docs/ITCHIO.md](docs/ITCHIO.md) era mais estreito do que o texto sugeria** —
ele vale para **binários publicados**, não para o jogo. Corrigido com uma seção
nova (§3.4) e o veredito do topo.

Três separações que faltavam:

1. **O itch.io não é dono do código dos jogos.** Ele hospeda; quem libera fonte
   é cada desenvolvedor. Não existe "o itch.io libera o código" — o que o
   itch.io tem aberto é o **cliente** (`itchio/itch`, MIT) e o `butler` (MIT),
   e isso não alcança os jogos.
2. **Existe oferta real de fonte**, sim: a tag `tag-open-source` traz Mindustry,
   Battle for Wesnoth, SuperTuxKart, Friday Night Funkin'.
3. **Mas a oferta está concentrada justamente onde o recompile é caro:**
   Mindustry GPL-3.0/Java, Wesnoth GPL-2.0/C++, SuperTuxKart NOASSERTION/C++,
   FNF Psych Apache-2.0/Haxe. "Compilar em arm64" para esses é portar engine e
   dependências para musl — o mesmo trabalho de port que o projeto já faz à mão
   — e a licença volta a bater no §6 (anti-tivoization).

**O elo técnico que faltava (medido, baixando o template):** o Godot **tem**
template de exportação aarch64 — `Godot_v4.3-stable_linux.arm64.zip`, 47,1 MB
(103,6 MB descomprimido), ELF64 com `e_machine = 0xB7` (AArch64, confirmado no
cabeçalho). Mas ele é **glibc**: referencia `GLIBC_2.17…2.28` e `libc.so.6`;
`ld-musl`/`libc.musl` **ausentes**. Nosso rootfs é musl
(`BR2_TOOLCHAIN_BUILDROOT_MUSL=y`, ADR-03), então o template oficial **não
roda** na imagem.

 Isso não invalida o caminho — é o que o ADR-03 já manda (recompilar todo
binário de runtime a partir do fonte), e a spec já anota o item:
`| musl × Godot export template | Recompilação obrigatória (ADR-03) |`.

**Alavancagem que isso revela:** recompilar o template do Godot contra musl
destrava **duas** coisas de uma vez — os jogos do itch.io publicados como
projeto Godot **e** os três jogos padrão do repositório (Arcade Volleyball,
Spacewar, Colossal Cave), que hoje instalam projeto em vez de binário pelo
mesmo motivo. Virou o item 4 da recomendação em `docs/ITCHIO.md`.

**Modelo corrigido:** o itch.io não serve como *catálogo automático* (o dado de
arquitetura não existe), mas serve como **coleção curada** — por jogo,
perguntar *"é Godot?"* e *"a licença permite distribuir?"*. Duas respostas "sim"
= minutos; qualquer "não" = um port.

**Verificação:** template arm64 baixado e inspecionado (ELF + strings de libc);
licenças e linguagens conferidas via API do GitHub (`Anuken/Mindustry`,
`wesnoth/wesnoth`, `supertuxkart/stk-code`, `ShadowMario/FNF-PsychEngine`);
`check-links` ok (29 arquivos) e `make lint` rc=0. **Não rodou:** nenhum export
do Godot, e o build musl do template continua pendente.

### 2026-10-31 (23) — Copilot (sonda de vídeo: parar de adivinhar o EGL)

O mantenedor reportou "ainda não sobe o launcher". O log do cartão mostrou que
o SDL2 **tem** KMSDRM agora (chegou a tentar criar a janela):

```
MESA-LOADER: failed to open kms_swrast: ... No such file or directory
MESA-LOADER: failed to open swrast: ... No such file or directory
W: render: SDL2 indisponivel (SDL_CreateWindow: EGL not initialized)
```

Investigação descartou, com evidência, as hipóteses baratas:

| Hipótese | Medição |
|---|---|
| falta o driver Mesa do RK3566 | `rockchip_dri.so` e `panfrost_dri.so` existem |
| kmsro não foi compilado | `GALLIUM_KMSRO` e `__driDriverGetExtensions_rockchip` estão no binário |
| backend GBM ausente | `gbm_dri.c` é compilado dentro do `libgbm` (`build.ninja`) |
| plataforma EGL errada | Mesa reporta `Platforms: surfaceless drm`, `gbm: enabled` |

Ou seja: **o build está correto** e o que falta é do runtime. Como cada passo
tem uma correção diferente, parei de adivinhar e escrevi o instrumento.

**Entregue:**

- `src/tools/gpu_probe.c` → `/usr/bin/giochi-gpu-probe`, 5 passos com
  `OK`/`FALHOU` e o **nome** do erro EGL. Chamado pelo `S20graphics` **depois**
  do handover do framebuffer; cada linha vai ao log do cartão como `sonda: ...`.
- `S20graphics` entrega o framebuffer à UI: baixa `console_loglevel` para 1 e
  desvincula o `vtcon` do fbcon (cursor piscando + `printk` de bateria a cada
  8 s pintavam por cima). `GIOCHI_KEEP_FBCON=1` mantém.
- `inittab` monta **tmpfs em `/var`** (§3.60): o `seedrng` falhava com
  "Read-only file system" e o `crng init` só vinha aos 11 s.
- `linux.fragment`: corrigido o comentário que dizia que `CONFIG_FB=n` remove o
  console (não remove — `DRM_FBDEV_EMULATION` faz `select FB`, §3.59).

**Verificação:** `make test` **26/26** (suítes com 13 guardas em
`primeiro-boot`), `make lint` rc=0. Guardas novas testadas ao contrário (tirar
o probe do `.mk` e tirar o tmpfs de `/var`: as duas reprovam). Probe
cross-compilado contra o sysroot real (`-Wall -Wextra`, link com
`-ldrm -lgbm -lEGL -lGLESv2`) e presente na imagem. Cartão gravado com
`dd conv=fsync` e conferido byte a byte (`sha256` `edbeda36…`).

**Aberto:** o boot com a sonda ainda não aconteceu. **Próximo dado é o log**:
as linhas `sonda: OK/FALHOU` dizem qual passo do EGL falha, e aí a correção é
pontual em vez de tentativa.
