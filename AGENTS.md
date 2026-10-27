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
em que ordem em [docs/roadmap.md](docs/roadmap.md).

| O quê | Estado |
|---|---|
| Núcleo em C/C++ (supervisor, sockets, SHA-256, Ed25519, JSON, hotkey) | **pronto e testado** |
| Buildroot `BR2_EXTERNAL`, 5 aparelhos, genimage A/B | **config validado** contra Buildroot 2024.02.13 real |
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

### Suítes de teste (15)

| Suíte | O que cobre |
|---|---|
| `test_json`, `test_sha256` | núcleo comum |
| `test_layout`, `test_metadata`, `test_settings`, `test_buttonsmap` | Launcher |
| `test_hotkey`, `test_transition`, `test_musicd` | init / daemons |
| `lifecycle` (integração) | §2.2: SIGTERM+waitpid, reinício, SIGSTOP/SIGCONT |
| `usb-gadget` (integração) | configfs falso: rede sempre, ffs.mtp, mass, `off` |
| `devmode` (integração) | §4.6.1: gate desligado, IPs, bind só em `10.55.0.2:22`, `DEV_MODE` |
| `bootlogo`, `screenshots` (Python) | contrato dos assets e das imagens |
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

### 2026-10-27 (6) — Copilot (AGENTS.md + roadmap)

- Criou este arquivo e [docs/roadmap.md](docs/roadmap.md), consolidando as
  armadilhas e a ordem de trabalho por risco.
- Abertos registrados no roadmap; nada disso foi testado em hardware.
