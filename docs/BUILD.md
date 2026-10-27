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
make test      # 9 suítes (C, C++ e Python)
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

## 4. Kernel (S1)

Base: **Linux 6.12 mainline** (fixado nos dois defconfigs). A referência de
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
