# Aparelhos suportados e geração de imagem

Cada aparelho é descrito por um arquivo em
[`buildroot/board/giochi/devices/`](../buildroot/board/giochi/devices/) e tem um
defconfig gerado em `buildroot/configs/`. **Nunca edite o defconfig gerado**:
altere o `base-<soc>.defconfig` ou o `<aparelho>.conf` e rode `make defconfigs`.

## Matriz

| Aparelho | SoC | DTS | Painel | Rotação | Botões | Rádio | USB gadget | Validação em hardware |
|---|---|---|---|---|---|---|---|---|
| `x55` | RK3566 | `rk3566-powkiddy-x55` (**mainline**) | 720×1280 | 270° | `default` | RTL8821CS integrado (Wi-Fi + BT) | sim | **sim** (2026-10) |
| `x35s` | RK3566 | `rk3566-powkiddy-x35s` (**externo**) | 640×480 | 0° | `x35s` | não medido | **não** (F12) | não |
| `x35h` | RK3566 | idem X35S (mesmo DTB) | 640×480 | 0° | `x35s` | nenhum | **não** (F12) | parcial (2026-10-24) |
| `rk3566-generic` | RK3566 | `rk3566-anbernic-rg353v` (mainline) | 640×480 | 0° | `default` | dongle USB | sim | não |
| `rk3326-generic` | RK3326 | `rk3326-anbernic-rg351v` (mainline) | 640×480 | 0° | `default` | dongle USB | sim | não |

Os dois "generic" usam DTS mainline com painel 640×480 apenas como
**referência de bring-up**. Para a sua placa, passe o DTS dela.

## Criar uma imagem

Pré-requisitos: Buildroot LTS baixado, ~25 GB livres, `make`, `gcc`, `python3`,
`rsync`, `cpio`, `unzip`, `bc`, `wget`.

```sh
export BUILDROOT_DIR=/caminho/para/buildroot

# 1. Só valida (rápido, não compila)
tools/build-image.sh --check --all

# 2. Uma placa
tools/build-image.sh x55

# 3. Todas (sequencial; use --jobs)
tools/build-image.sh --all --jobs "$(nproc)"
```

Saída: `dist/<aparelho>/giochi-<aparelho>-sdcard.img`, pronta para
`dd if=... of=/dev/sdX bs=4M conv=fsync` ou Balena Etcher.

`make image DEVICE=x55` e `make images BUILD=1` são atalhos.

## Aparelhos sem DTS upstream

X35S e X35H dependem do DTS da ROCKNIX (não está no kernel mainline). O
preflight falha de propósito até você fornecer o arquivo:

```sh
# opção A: DTS fonte
cp rk3566-powkiddy-x35s.dts buildroot/board/giochi/dts/
tools/build-image.sh x35s

# opção B: caminho avulso
GIOCHI_DTS_PATH=/caminho/rk3566-powkiddy-x35s.dts tools/build-image.sh x35s

# opção C: DTB já compilado
GIOCHI_DTB_PATH=/caminho/rk3566-powkiddy-x35s.dtb tools/build-image.sh x35s
```

### Imagem de teste (fallback)

Para smoke-test sem o DTS da placa:

```sh
tools/build-image.sh x35s --fallback-dts
```

A imagem usa o DTB da RG353V, é gravada com `/etc/giochi/test_image` e
**não é suportada** — painel e controles provavelmente não funcionam. Serve
para validar boot, supervisor, sockets e update.

## Bootloader: mainline não cobre estes aparelhos

| Aparelho | Defconfig U-Boot mainline | Situação |
|---|---|---|
| `x55`, `x35s`, `x35h` | **não existe** (`powkiddy-*`/rk3326 ausentes) | use o U-Boot do vendor (ROCKNIX/Rockchip) |
| `rk3326-generic` | **não existe** para RK3326 | use idbloader + `u-boot.itb` do vendor |
| `rk3566-generic` | `quartz64-a-rk3566` | compila; serve para bring-up |

Por isso o defconfig é gerado com `BR2_TARGET_UBOOT=n` nos aparelhos sem
suporte, e o `post-image.sh` **automaticamente** empacota com
`genimage-no-uboot.cfg` (partições p1..p4, sem região de bootloader).

Três caminhos:

```sh
# A) Bootloader do vendor já compilado (recomendado para X55)
GIOCHI_UBOOT_BIN=/caminho/u-boot-rockchip.bin tools/build-image.sh x55

# B) Tree do U-Boot do vendor (com defconfig powkiddy-x55-rk3566)
GIOCHI_UBOOT_DIR=/caminho/u-boot tools/build-image.sh x55

# C) Sem bootloader: gere a imagem e grave o vendor antes no cartão
tools/build-image.sh x55
# depois, no cartão: dd do idbloader/uboot no offset 32 KiB
```

`--vendor-uboot` transforma o aviso em **erro no preflight** (útil na CI quando
você exige que a imagem venha com bootloader).

> Sem o bootloader correto a placa **não liga** — não é bug da imagem. O
> `extlinux.conf`/`boot.scr` gerados assumem o vendor U-Boot da ROCKNIX.

## Componentes fora do mainline

| Componente | Necessário para | Situação |
|---|---|---|
| `rocknix-joypad` | gamepad de X55/X35S/X35H | não empacotado; análogos dependem dele |
| `panel-generic-dsi` | painel de X35S/X35H | não empacotado |
| DTS `rk3566-powkiddy-x35s` | X35S/X35H | externo (ver acima) |

O preflight avisa quando o aparelho usa `rocknix` e o módulo não está em
`/lib/modules`. Detalhes em [dts/README](../buildroot/board/giochi/dts/README)
e em [hardware-e-drivers.md](./hardware-e-drivers.md).

## Adicionar um aparelho

1. Crie `buildroot/board/giochi/devices/meuplaca.conf` com os campos
   `GIOCHI_ID`, `GIOCHI_NAME`, `GIOCHI_SOC`, `GIOCHI_DEFCONFIG`, `GIOCHI_DTS`,
   `GIOCHI_DTS_UPSTREAM`, `GIOCHI_DTB_FILE`, `GIOCHI_PANEL`,
   `GIOCHI_ROTATION`, `GIOCHI_LAYOUT`, `GIOCHI_BUTTONS_MAP`, `GIOCHI_JOYPAD`,
   `GIOCHI_WIFI`, `GIOCHI_BT`, `GIOCHI_USB_GADGET`, `GIOCHI_TESTED`,
   `GIOCHI_NOTES`.
2. Se o painel não for 640×480, crie `assets/devices/<layout>/` com
   `layout.json` (e variantes) e aponte `GIOCHI_LAYOUT`.
3. Rode `make defconfigs && make lint`.
4. `tools/build-image.sh --check meuplaca`.
5. Abra PR atualizando a matriz acima.

## Orçamento por aparelho

As metas da §8 da especificação valem para todos: SquashFS ≤ 150 MB, RAM em
repouso ≤ 60 MB, VRAM de UI ≤ 4 MB. A X55 opera em 1280×720 (painel rotacionado),
o que dobra a área do grid em relação a 640×480 — por isso o layout dela usa
tiles de 288×162 e teto de VRAM de 2 MB na aba Música
(`assets/devices/x55/music.json`).
