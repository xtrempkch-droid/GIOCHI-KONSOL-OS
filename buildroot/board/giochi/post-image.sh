#!/bin/sh
# Giochi Konsol OS v1.2 — post-image.sh (S0).
# Monta A/B em SquashFS + particao C (ext4) e empacota o cartao com genimage.
set -eu

BOARD_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BINARIES_DIR=${BINARIES_DIR:-output/images}
GENIMAGE_CFG_ENV=${GENIMAGE_CFG:-}
USER_SIZE_MB=${USER_SIZE_MB:-512}

# --- aparelho ---------------------------------------------------------
GIOCHI_DEVICE=${GIOCHI_DEVICE:-rk3326-generic}
DEVICE_CONF="$BOARD_DIR/devices/$GIOCHI_DEVICE.conf"
if [ ! -f "$DEVICE_CONF" ]; then
    echo "post-image: ERRO: aparelho '$GIOCHI_DEVICE' sem $DEVICE_CONF" >&2
    exit 1
fi
# shellcheck disable=SC1090
. "$DEVICE_CONF"
# Override vem do build-image.sh (DTS fallback ou DTB pre-compilado).
DTB_FILE=${GIOCHI_DTB_FILE_OVERRIDE:-${GIOCHI_DTB_FILE:-board.dtb}}
echo "post-image: aparelho=$GIOCHI_DEVICE dtb=$DTB_FILE"

if [ ! -f "$BINARIES_DIR/rootfs.squashfs" ]; then
    echo "post-image: rootfs.squashfs ausente" >&2
    exit 1
fi
# Escolhe o layout: com ou sem particao de bootloader.
if [ -f "$BINARIES_DIR/u-boot-rockchip.bin" ]; then
    UB_BIN="$BINARIES_DIR/u-boot-rockchip.bin"
elif [ -f "$BINARIES_DIR/u-boot.bin" ]; then
    UB_BIN="$BINARIES_DIR/u-boot.bin"
else
    UB_BIN=""
fi

if [ -z "$UB_BIN" ] && [ -n "${GIOCHI_UBOOT_BIN:-}" ] && \
   [ -f "${GIOCHI_UBOOT_BIN}" ]; then
    cp "$GIOCHI_UBOOT_BIN" "$BINARIES_DIR/u-boot-rockchip.bin"
    UB_BIN="$BINARIES_DIR/u-boot-rockchip.bin"
    echo "post-image: bootloader do vendor: $GIOCHI_UBOOT_BIN"
fi

if [ -n "$GENIMAGE_CFG_ENV" ]; then
    GENIMAGE_CFG="$GENIMAGE_CFG_ENV"
elif [ -z "$UB_BIN" ]; then
    GENIMAGE_CFG="$BOARD_DIR/genimage-no-uboot.cfg"
    echo "post-image: sem bootloader; imagem SEM u-boot (use o do vendor)" >&2
else
    GENIMAGE_CFG="$BOARD_DIR/genimage.cfg"
fi

# Particao C vazia (o usuario formata/monta em runtime; S10storage detecta).
if [ ! -f "$BINARIES_DIR/user.ext4" ]; then
    dd if=/dev/zero of="$BINARIES_DIR/user.ext4" bs=1M count=0 \
       seek="$USER_SIZE_MB" status=none
    mkfs.ext4 -q -F -L GIOCHI_USER "$BINARIES_DIR/user.ext4"
fi

# DTB pre-compilado (aparelho sem DTS no kernel): copiado pelo build-image.sh.
if [ -n "${GIOCHI_PRECOMPILED_DTB:-}" ] && [ -f "$GIOCHI_PRECOMPILED_DTB" ]; then
    mkdir -p "$BINARIES_DIR/dtbs"
    cp "$GIOCHI_PRECOMPILED_DTB" "$BINARIES_DIR/dtbs/$DTB_FILE"
fi

# board.dtb: nome generico usado pelo boot.cmd quando fdtfile nao existe.
if [ ! -f "$BINARIES_DIR/board.dtb" ] && [ -f "$BINARIES_DIR/dtbs/$DTB_FILE" ]; then
    cp "$BINARIES_DIR/dtbs/$DTB_FILE" "$BINARIES_DIR/board.dtb"
fi

# DTB de aparelho sem suporte upstream (X35S/X35H/genericos): copiado de
# GIOCHI_DTS_PATH pelo build-image.sh para $BINARIES_DIR/dtbs/.
if [ ! -f "$BINARIES_DIR/dtbs/$DTB_FILE" ]; then
    echo "post-image: ERRO: dtbs/$DTB_FILE ausente (aparelho $GIOCHI_DEVICE)" >&2
    echo "post-image:       forneca o DTS via GIOCHI_DTS_PATH (ver docs/DEVICES.md)" >&2
    exit 1
fi

# extlinux.conf (F1): o DTB escolhe o driver de painel — X55 != X35S.
if [ ! -f "$BINARIES_DIR/extlinux.conf" ]; then
    GIOCHI_DTB_NAME=${GIOCHI_DTB_NAME:-$DTB_FILE}
    cat > "$BINARIES_DIR/extlinux.conf" <<CONF
DEFAULT giochi
LABEL giochi
    KERNEL /Image
    FDT /dtbs/$GIOCHI_DTB_NAME
    APPEND console=ttyS2,1500000n8 earlycon=uart8250,mmio32,0xfe660000 root=PARTUUID=%PARTUUID% rootfstype=squashfs ro rootwait
CONF
fi

# Bootsplash do U-Boot: assets/bootlogo/<device>/splash.bmp -> /splash.bmp.
# Requer CONFIG_SPLASH_SCREEN=y e 'splashimage' no boot.cmd (ver
# buildroot/board/giochi/uboot.fragment e docs/BOOTSPLASH.md).
BOOTLOGO_DIR="$BOARD_DIR/../../../assets/bootlogo/$GIOCHI_DEVICE"
if [ -f "$BOOTLOGO_DIR/splash.bmp" ]; then
    cp "$BOOTLOGO_DIR/splash.bmp" "$BINARIES_DIR/splash.bmp"
    echo "post-image: splash.bmp do aparelho $GIOCHI_DEVICE"
else
    echo "post-image: aviso: sem splash.bmp para $GIOCHI_DEVICE" \
         "(rode 'make bootlogo')" >&2
fi

# Particao A (FAT): Image + dtb + boot.scr.
if [ -x "$BOARD_DIR/mkbootimg.sh" ]; then
    "$BOARD_DIR/mkbootimg.sh" "$BINARIES_DIR" "$BINARIES_DIR/boot.vfat" || {
        echo "post-image: aviso: boot.vfat nao gerado" >&2
    }
fi

if ! command -v genimage >/dev/null 2>&1; then
    echo "post-image: genimage ausente; artefatos individuais em $BINARIES_DIR"
    exit 0
fi

GENIMAGE_TMP="$BINARIES_DIR/genimage.tmp"
rm -rf "$GENIMAGE_TMP"
genimage --rootpath "$TARGET_DIR" --tmppath "$GENIMAGE_TMP" \
         --inputpath "$BINARIES_DIR" --outputpath "$BINARIES_DIR" \
         --config "$GENIMAGE_CFG"

echo "post-image: sdcard.img pronto em $BINARIES_DIR"
