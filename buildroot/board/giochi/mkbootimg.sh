#!/bin/sh
# Giochi Konsol OS v1.2 — mkbootimg.sh (S2).
# Monta a particao A (FAT) com Image, board.dtb, boot.scr e uboot.env.
set -eu

BOOT_DIR=${1:-output/images}
DEST=${2:-output/images/boot.vfat}
SIZE_MB=${SIZE_MB:-64}

[ -f "$BOOT_DIR/Image" ] || { echo "mkbootimg: Image ausente" >&2; exit 1; }
[ -f "$BOOT_DIR/boot.scr" ] || { echo "mkbootimg: boot.scr ausente" >&2; exit 1; }

rm -f "$DEST"
dd if=/dev/zero of="$DEST" bs=1M count="$SIZE_MB" status=none
mkfs.vfat -F 32 -n GIOCHI_BOOT "$DEST" >/dev/null
mcopy -i "$DEST" "$BOOT_DIR/Image" ::Image

# dtbs/<placa>.dtb + board.dtb (nome usado pelo boot.cmd sem fdtfile).
if [ -d "$BOOT_DIR/dtbs" ]; then
    mmd -i "$DEST" ::dtbs
    mcopy -i "$DEST" "$BOOT_DIR"/dtbs/*.dtb ::dtbs/
fi
if [ -f "$BOOT_DIR/board.dtb" ]; then
    mcopy -i "$DEST" "$BOOT_DIR/board.dtb" ::board.dtb
fi

# extlinux.conf: documenta o DTB de cada placa (F1) e serve de fallback de boot.
if [ -f "$BOOT_DIR/extlinux.conf" ]; then
    mmd -i "$DEST" ::extlinux 2>/dev/null || true
    mcopy -i "$DEST" "$BOOT_DIR/extlinux.conf" ::extlinux/extlinux.conf
fi

# Splash do U-Boot (opcional): sem o arquivo, o boot segue normal.
if [ -f "$BOOT_DIR/splash.bmp" ]; then
    mcopy -i "$DEST" "$BOOT_DIR/splash.bmp" ::splash.bmp
fi

mcopy -i "$DEST" "$BOOT_DIR/boot.scr" ::boot.scr
if [ -f "$BOOT_DIR/uboot.env" ]; then
    mcopy -i "$DEST" "$BOOT_DIR/uboot.env" ::uboot.env
fi
echo "mkbootimg: $DEST ($SIZE_MB MB)"
