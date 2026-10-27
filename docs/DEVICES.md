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

## Bootloader

| Aparelho | Defconfig U-Boot mainline | Como a imagem sai |
|---|---|---|
| `x55` | **`powkiddy-x55-rk3566`** (upstream desde a v2024.07) | **bootável de fábrica** (U-Boot mainline + BL31/DDR do rkbin) |
| `rk3566-generic` | `quartz64-a-rk3566` | bootável (bring-up; painel/controles do genérico) |
| `x35s`, `x35h` | **não existe** (`powkiddy-x35s`/`x35h` ausentes) | sem bootloader: grave o do vendor antes |
| `rk3326-generic` | **não existe** para RK3326 | sem bootloader: idbloader + `u-boot.itb` do vendor |

> ⚠️ **Correção de versão anterior desta documentação.** Ela afirmava que o
> U-Boot mainline não tinha defconfig para *nenhum* aparelho. Isso é falso para
> a X55 desde o **U-Boot 2024.07** (`configs/powkiddy-x55-rk3566_defconfig`).
> Verificado contra v2024.04 (ausente),
> v2024.07, v2024.10 e v2025.01 (presentes), e por `tools/check-uboot.py`
> (confere defconfig, símbolos e blobs do rkbin).

Para X55 a cadeia completa é montada pelo Buildroot, sem passo manual:

```
rkbin (BL31 + DDR/TPL)  →  U-Boot SPL  →  u-boot-rockchip.bin  →  genimage.cfg (offset 32 KiB)
```

- `BR2_TARGET_UBOOT_CUSTOM_VERSION_VALUE="2024.10"` — 2024.07 é o mínimo; o
  2024.10 é o pinado (LTS seguinte).
- `BR2_PACKAGE_ROCKCHIP_RKBIN_BL31_FILENAME="bin/rk35/rk3568_bl31_v1.43.elf"`
- `BR2_PACKAGE_ROCKCHIP_RKBIN_TPL_FILENAME="bin/rk35/rk3566_ddr_1056MHz_v1.18.bin"`

Sem BL31/TPL o SPL do RK3566 **não inicializa a DDR** — a placa liga e morre.
Esses caminhos existem no commit de rkbin pinado pelo Buildroot 2024.02
(`b4558da0`); se você trocar a versão do Buildroot, reconfira com
`tools/check-buildroot-symbols.py` e com um `ls` no tree do rkbin.

### Aparelhos sem defconfig mainline (X35S, X35H, RK3326)

Nesses o `post-image.sh` cai automaticamente em `genimage-no-uboot.cfg`
(partições p1..p4, sem região de bootloader):

```sh
# A) Bootloader do vendor já compilado
GIOCHI_UBOOT_BIN=/caminho/u-boot-rockchip.bin tools/build-image.sh x35s

# B) Tree do U-Boot do vendor
GIOCHI_UBOOT_DIR=/caminho/u-boot tools/build-image.sh x35s

# C) Sem bootloader: grave o vendor antes no cartão (offset 32 KiB)
tools/build-image.sh x35s
```

`--vendor-uboot` transforma o aviso em **erro no preflight** (útil na CI quando
você exige que a imagem venha com bootloader).

> Sem o bootloader correto a placa **não liga** — não é bug da imagem.

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
