# Build — Giochi Konsol OS v1.2

Este documento descreve como compilar os artefatos do repositório. A
especificação normativa é [architecture-v1.2.md](./architecture-v1.2.md); a
orquestração por sessões está em [Prompt.md](../Prompt.md).

## 1. Build de host (validação rápida)

Requer `gcc`/`g++` (C99 / C++17). As bibliotecas opcionais são detectadas por
`pkg-config` e ativam macros `GIOCHI_HAVE_*`; sem elas o código cai nos stubs
previstos na especificação.

```sh
make info      # mostra o que foi detectado
make -j4       # build/host/bin/{giochi-init,giochi-musicd,giochi-launcher,...}
make test      # todas as suítes (C, C++, integração e Python)
```

| Biblioteca | Macro | Sem ela |
|---|---|---|
| SDL2 | `GIOCHI_HAVE_SDL2` | Launcher usa backend headless |
| alsa-lib | `GIOCHI_HAVE_ALSA` | `giochi-musicd` sobe sem áudio |
| libmpg123 / FLAC | `GIOCHI_HAVE_MPG123` / `_FLAC` | protocolo responde `decoder_unavailable` |
| OpenSSL (libcrypto) | `GIOCHI_HAVE_OPENSSL` | `giochi-verify` recusa updates (exit 3) |
| libarchive | `GIOCHI_HAVE_LIBARCHIVE` | `giochi-extract` recusa (exit 3) |
| libpng | `GIOCHI_HAVE_LIBPNG` | `giochi-thumbgen` grava placeholder |

Testes de host usam `GIOCHI_ROOT` para redirecionar caminhos absolutos e
`GIOCHI_DRY_RUN=1` (padrão quando não-root) para não tocar hardware.

## 2. Cross-compile (aarch64 + musl)

```sh
make HOST_BUILD=0 BUILD_DIR=build/target \
     CC=aarch64-linux-musl-gcc CXX=aarch64-linux-musl-g++ \
     EXTRA_CFLAGS="-march=armv8-a+crc -mtune=cortex-a35" -j4
```

`EXTRA_CFLAGS`, `EXTRA_CXXFLAGS` e `EXTRA_LDFLAGS` são os ganchos usados pelos
pacotes Buildroot — não há segunda lista de fontes em `.mk`.

## 3. Buildroot (S0)

O diretório `buildroot/` é uma árvore `BR2_EXTERNAL` completa.

**Não edite `buildroot/configs/*_defconfig`**: eles são gerados a partir de
`buildroot/board/giochi/devices/*.conf` + `defconfig/base-<soc>.defconfig`.
Altere a fonte e rode `make defconfigs`.

### Versão do Buildroot: 2024.02.x

O projeto é validado contra o **LTS 2024.02.x**. Isso importa porque o Kconfig
**descarta símbolos em silêncio** quando eles não existem naquela versão — o
build não avisa e o recurso simplesmente não entra na imagem. Dois casos reais
que já quebraram o build:

| Sintoma | Causa | Correto |
|---|---|---|
| `Makefile.legacy:9: *** "You have legacy configuration"` | símbolo virou *legacy* | `BR2_PACKAGE_RFKILL` → `BR2_PACKAGE_UTIL_LINUX_RFKILL` |
| pacote/recurso ausente, sem aviso | símbolo inexistente, descartado | `BR2_PACKAGE_LIBMPG123` → `BR2_PACKAGE_MPG123` |

Antes de qualquer build (e no CI), valide os símbolos contra a árvore real:

```sh
BUILDROOT_DIR=/caminho/para/buildroot tools/check-buildroot-symbols.py
```

Ele lista `LEGACY` (aborta o build) e `INEXISTENTE` (seria descartado) por
defconfig. `make lint` roda essa checagem automaticamente quando
`BUILDROOT_DIR` está definido, e `tools/build-image.sh` a executa antes de
compilar cada aparelho.

```sh
export BUILDROOT_DIR=/caminho/para/buildroot

tools/build-image.sh --check --all      # valida (sem compilar)
tools/build-image.sh x55                # dist/x55/giochi-x55-sdcard.img
tools/build-image.sh --all --jobs 8
```

| Aparelho | Defconfig | SoC | DTS | SquashFS |
|---|---|---|---|---|
| `x55` | `giochi_x55_defconfig` | RK3566 | mainline `rk3566-powkiddy-x55` | ZSTD |
| `x35s` | `giochi_x35s_defconfig` | RK3566 | externo `rk3566-powkiddy-x35s` | ZSTD |
| `x35h` | `giochi_x35h_defconfig` | RK3566 | idem X35S | ZSTD |
| `rk3566-generic` | `giochi_rk3566_defconfig` | RK3566 | mainline `rk3566-anbernic-rg353v` | ZSTD |
| `rk3326-generic` | `giochi_rk3326_defconfig` | RK3326 | mainline `rk3326-anbernic-rg351v` | LZ4 |

Detalhes de DTS externo, fallback de teste e como adicionar placa:
[docs/DEVICES.md](./DEVICES.md).

`post-build.sh` instala layouts/tema (com override por aparelho em
`assets/devices/<id>/`), scripts de init, `mdev.conf`, o helper de USB e o
`/etc/giochi/device.conf`; `post-image.sh` monta A/B (SquashFS), a partição C
(ext4), o `extlinux.conf` com o DTB do aparelho e empacota o cartão com
`genimage`.

### Build em container (recomendado)

O Buildroot 2024.02 **não compila** em hosts com toolchain muito nova: GCC 15
quebra `host-m4` 1.4.19 e CMake 4.x rejeita o `CMakeLists.txt` do `host-lzo`
(suporte a `< 3.5` foi removido). Não é problema de configuração — é ambiente.

`tools/docker-build.sh` reproduz o runner do CI (Ubuntu 24.04 → GCC 13) e
compila o Buildroot completo dentro do container:

```sh
tools/docker-build.sh x55                    # imagem completa
tools/docker-build.sh x55 --target uboot     # só toolchain + U-Boot
tools/docker-build.sh x55 --target "uboot linux"
tools/docker-build.sh --rebuild-image        # refaz o container
```

O cache de downloads fica em `.cache/buildroot-dl` (fora do container, então
sobrevive a `--rebuild-image`). Os arquivos em `dist/` saem com o UID do host,
não `root`.

> ⚠️ Se o Docker for o **snap**, o contexto de build precisa estar sob `$HOME`:
> o snap não enxerga `/tmp`. O script avisa se o repositório estiver em `/tmp`.
>
> Use `JOBS=n` para limitar o paralelismo — Mesa3D e o kernel comem memória.

### Ferramentas de host que a imagem exige

O `post-image.sh` chama `mkbootimg.sh`, que monta a partição A com
`mkfs.vfat` + `mcopy`/`mmd` e **exige** `boot.scr`. Tudo isso é declarado no
defconfig, e não deixado para o que estiver instalado na máquina:

| Símbolo | Fornece | Sem ele |
|---|---|---|
| `BR2_PACKAGE_HOST_DOSFSTOOLS` | `mkfs.vfat` | post-image falha (antes: só se o `apt` da máquina tivesse `dosfstools`) |
| `BR2_PACKAGE_HOST_MTOOLS` | `mcopy`, `mmd` | idem |
| `BR2_PACKAGE_HOST_UBOOT_TOOLS` + `..._BOOT_SCRIPT` | `boot.scr` de `board/giochi/boot.cmd` | `mkbootimg: boot.scr ausente`, e o genimage morria com "cannot find image boot.vfat" |
| `BR2_TARGET_UBOOT_NEEDS_UTIL_LINUX` | `-luuid` para o `mkeficapsule` do U-Boot | `uuid/uuid.h: No such file or directory` |
| `BR2_TARGET_UBOOT_NEEDS_GNUTLS` | `gnutls` para o mesmo binário | `gnutls/gnutls.h: No such file or directory` |

> As duas últimas só aparecem em máquina **limpa**: o runner do CI já traz
> `uuid-dev` e `gnutls-dev` na imagem, o que escondia a dependência. Foi assim
> que o build em container as encontrou.

O Buildroot põe `$(HOST_DIR)/bin` no `PATH` dos scripts de post-image
(`BR_PATH`), então essas ferramentas funcionam sem instalação no sistema.

## 4. Kernel (S1)

Base: **Linux 6.12 mainline** (fixado nos dois defconfigs).

> ⚠️ **Não troque `BR2_LINUX_KERNEL_USE_ARCH_DEFAULT_CONFIG` por
> `BR2_LINUX_KERNEL_USE_DEFCONFIG`.** O arm64/ARM tem **um único** defconfig no
> kernel (`arch/arm64/configs/defconfig`), e o Buildroot acrescenta o sufixo ao
> valor (`linux.mk`: `LINUX_KCONFIG_DEFCONFIG = <valor>_defconfig`). Com
> `BR2_LINUX_KERNEL_DEFCONFIG="defconfig"` o comando vira `make
> defconfig_defconfig` e o build morre com *"Can't find default configuration
> arch/arm64/configs/defconfig_defconfig"* — depois de baixar o kernel e
> compilar todo o host. A opção correta é a de configuração padrão da
> arquitetura (o help do Buildroot diz: *"useful on architectures that have a
> single defconfig file, such as ARM64"*). `tools/check-buildroot-symbols.py`
> barra essa regressão. A referência de
hardware dos aparelhos RK3566 está em
[hardware-e-drivers.md](./hardware-e-drivers.md) §6 — os símbolos abaixo vêm de
lá e são CONFIRMADOS no aparelho.

```sh
# RK3326 (genérico)
scripts/kconfig/merge_config.sh -m arch/arm64/configs/defconfig \
    buildroot/board/giochi/linux.fragment
make olddefconfig

# RK3566 (Powkiddy X55/X35S/X35H)
scripts/kconfig/merge_config.sh -m arch/arm64/configs/defconfig \
    buildroot/board/giochi/linux.fragment \
    buildroot/board/giochi/linux-zstd.fragment \
    buildroot/board/giochi/linux-powkiddy.fragment
make olddefconfig
buildroot/board/giochi/assert-kernel-config.sh .config
```

O `olddefconfig` **descarta filhos de pais unset sem avisar** (ex.:
`SND_SOC_ROCKCHIP_I2S` se `SND_SOC_ROCKCHIP` estiver unset) — por isso o
`assert-kernel-config.sh` falha o build nesse caso.

Pendências de código fora do mainline (ver §9 do documento de hardware):
`rocknix-joypad` (controles), `panel-generic-dsi` e o DTS da X35S. O DTS da X55
é mainline desde a v6.8.

| Item | RK3326 | RK3566 |
|---|---|---|
| zRAM / SquashFS | LZ4 | ZSTD |
| GPU | Panfrost (Mali-G31) + Lima disponível | Panfrost (Mali-G52) |
| PCI/PCIe | `CONFIG_PCI=n` | `CONFIG_PCI=y` + `PCIE_ROCKCHIP_HOST` |
| Crypto | `armv8-a+crc` | `armv8-a+crc+crypto` |
| Fragmentos | `linux.fragment` | `linux.fragment` + `linux-zstd.fragment` + `linux-powkiddy.fragment` |
| Rádio | dongle USB | **integrado** RTL8821CS (Wi-Fi SDIO + BT H5) na X55; X35H sem rádio |
| Display | painel do `.dtb` do vendor | X55 720×1280 (`rk3566-powkiddy-x55`); X35S/X35H 640×480 |
| Áudio | ALSA genérico | RK817 + I2S TDM (`simple-audio-card` `rk817_ext`) |
| Cartão | `mmcblk0`/`mmcblk1` variável | SD = `mmcblk1` (scripts derivam de `/proc/cmdline`) |

O fragmento remove NVIDIA/AMD, mídia óptica, PCIe genérico (RK3326) e
subsistemas de desktop; inclui USB gadget (MTP/Mass Storage) e os módulos de
dongle como `=m` (não carregam no boot).

## 5. Empacotamento e assinatura (S9/S12)

> **Estas ferramentas rodam no PC, não no console.** `giochi-pack.sh`,
> `giochi-sign.sh`, `giochi-keys.sh` e `giochi-forge` (`src/forge`) pertencem ao
> lado do autor/publicador: o aparelho apenas **valida** (`giochi-verify`),
> **extrai** (`giochi-extract`) e **gera thumbnails** (`giochi-thumbgen`).
>
> Isso não é preferência: `giochi-pack.sh` usa `GNU tar --format=ustar`,
> `--owner/--group`, `-r` e `jq` — nada disso existe no rootfs (BusyBox tar,
> sem `jq`). Instalar esse script na imagem produzia um comando que falhava
> sempre. `tools/check-image-contents.sh` (em `make lint`) barra a regressão.

```sh
sh tools/giochi-pack.sh projeto/ jogo.giochi
sh tools/giochi-keys.sh keys/                       # uma vez
sh tools/giochi-sign.sh jogo.giochi keys/giochi_ed25519.pem 1.0.0
build/host/bin/giochi-verify --image jogo.giochi \
    --sig jogo.giochi.sig --pubkey keys/pubkey.ed25519 \
    --manifest jogo.giochi.manifest.json
```

A chave privada nunca é versionada (`.gitignore`). A pública vai para
`/etc/giochi/pubkey.ed25519` na SquashFS (`ro`).

## 6. giochi-forge (S13)

```sh
PYTHONPATH=src/forge python3 -m giochi_forge check \
    --profile device_profile.json --project caminho/do/projeto
PYTHONPATH=src/forge python3 -m giochi_forge build \
    --project caminho/do/projeto --out jogo.giochi --key keys/giochi_ed25519.pem
```

Sem USB: `--device rk3326-1gb|rk3326-2gb|rk3566-2gb`, ou `--advanced` para
ignorar o filtro (com aviso explícito).
