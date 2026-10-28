#!/bin/sh
# Giochi Konsol OS v1.2 — post-image.sh (S0).
# Monta A/B em SquashFS + particao C (ext4) e empacota o cartao com genimage.
set -eu

BOARD_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BINARIES_DIR=${BINARIES_DIR:-output/images}
GENIMAGE_CFG_ENV=${GENIMAGE_CFG:-}
# GIOCHI_NO_COMPRESS=1 pula o .img.xz/.zst (util em build local iterativo).
GIOCHI_NO_COMPRESS=${GIOCHI_NO_COMPRESS:-0}

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

# --- tamanho das particoes (fonte unica: o genimage.cfg) ----------------
# Antes o tamanho da particao C vinha de USER_SIZE_MB e o das outras de um
# arquivo separado: mudar um sem o outro gerava ext4 maior que a particao (o
# genimage aborta) ou espaco morto. Agora tudo sai do cfg escolhido.
part_size() {
    awk -v want="$2" '
        $1 == "partition" { cur = $2; next }
        cur == want && $1 == "size" { print $3; exit }
    ' "$1"
}

# "512M" -> 512 (MiB). Vazio quando o valor nao tem unidade reconhecida.
to_mb() {
    case "$1" in
        *G) echo $(( ${1%G} * 1024 )) ;;
        *M) echo "${1%M}" ;;
        *K) echo $(( ${1%K} / 1024 )) ;;
        *)  echo "" ;;
    esac
}

# Confere se o artefato cabe na particao declarada. Falhar aqui da o numero
# exato; sem isso o genimage aborta no fim ("image too big for partition") ou,
# pior, as particoes se sobrepoem.
fits() {  # fits <rotulo> <arquivo> <particao>
    _file=$2
    [ -f "$_file" ] || return 0
    _part=$3
    _limit=$(to_mb "$(part_size "$GENIMAGE_CFG" "$_part")")
    [ -n "$_limit" ] || return 0
    _mb=$(( ($(wc -c <"$_file") + 1048575) / 1048576 ))
    if [ "$_mb" -gt "$_limit" ]; then
        echo "post-image: ERRO: $1 ocupa ${_mb} MB, mas a particao '$_part'" >&2
        echo "post-image:       tem ${_limit} MB em $(basename "$GENIMAGE_CFG")" >&2
        echo "post-image:       aumente 'size' da particao ou reduza o artefato" >&2
        return 1
    fi
    echo "post-image: $1 ${_mb} MB / particao $_part ${_limit} MB"
}

# Particao C vazia (o usuario formata/monta em runtime; S10storage detecta).
# O tamanho tem de ser EXATAMENTE o da particao declarada no cfg.
USER_SIZE_MB=$(to_mb "$(part_size "$GENIMAGE_CFG" user)")
if [ -n "$USER_SIZE_MB" ]; then
    if [ -f "$BINARIES_DIR/user.ext4" ]; then
        have=$(( ($(wc -c <"$BINARIES_DIR/user.ext4") + 1048575) / 1048576 ))
        if [ "$have" -ne "$USER_SIZE_MB" ]; then
            echo "post-image: aviso: user.ext4 com ${have} MB != ${USER_SIZE_MB} MB do cfg; recriando"
            rm -f "$BINARIES_DIR/user.ext4"
        fi
    fi
    if [ ! -f "$BINARIES_DIR/user.ext4" ]; then
        dd if=/dev/zero of="$BINARIES_DIR/user.ext4" bs=1M count=0 \
           seek="$USER_SIZE_MB" status=none
        mkfs.ext4 -q -F -L GIOCHI_USER "$BINARIES_DIR/user.ext4"
    fi
else
    echo "post-image: ERRO: particao 'user' sem 'size' em $GENIMAGE_CFG" >&2
    exit 1
fi

fits u-boot-rockchip.bin "$UB_BIN" u-boot || exit 1
fits rootfs.squashfs "$BINARIES_DIR/rootfs.squashfs" system-a || exit 1

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
# Falha aqui e FATAL: sem boot.vfat o genimage segue e morre com "cannot find
# image boot.vfat", escondendo a causa real (Image ou boot.scr ausente).
# O boot.scr vem de BR2_PACKAGE_HOST_UBOOT_TOOLS_BOOT_SCRIPT (ver defconfig).
if [ -x "$BOARD_DIR/mkbootimg.sh" ]; then
    if ! "$BOARD_DIR/mkbootimg.sh" "$BINARIES_DIR" \
            "$BINARIES_DIR/boot.vfat"; then
        echo "post-image: ERRO: boot.vfat nao foi gerado (ver mensagem acima)" >&2
        echo "post-image: boot.scr exige BR2_PACKAGE_HOST_UBOOT_TOOLS_BOOT_SCRIPT=y" >&2
        echo "post-image: mcopy/mmd/mkfs.vfat exigem HOST_MTOOLS/HOST_DOSFSTOOLS" >&2
        exit 1
    fi
    fits boot.vfat "$BINARIES_DIR/boot.vfat" boot || exit 1
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

IMG="$BINARIES_DIR/sdcard.img"
echo "post-image: sdcard.img pronto em $BINARIES_DIR ($(du -h "$IMG" 2>/dev/null | cut -f1))"

# --- artefato de distribuicao -------------------------------------------
# O sdcard.img tem ~936 MB, mas ~90% sao zeros, e as particoes A/B guardam o
# MESMO SquashFS. O artefato publicado e o comprimido (~28 MB); o .img cru nao
# serve para distribuir (limite pratico de armazenamento/transferencia).
# xz vem primeiro porque balenaEtcher e Raspberry Pi Imager abrem .xz direto;
# zstd/gzip ficam de reserva (o zstd de host vem de BR2_PACKAGE_HOST_ZSTD).
if [ "$GIOCHI_NO_COMPRESS" = "1" ]; then
    echo "post-image: GIOCHI_NO_COMPRESS=1; sem compressao"
else
    comp=""
    for c in xz zstd gzip; do
        command -v "$c" >/dev/null 2>&1 && { comp=$c; break; }
    done
    rm -f "$IMG.xz" "$IMG.zst" "$IMG.gz"
    case "$comp" in
        xz)
            # Janela do LZMA2 maior que a distancia entre A e B: assim o xz
            # referencia a segunda copia do SquashFS em vez de recomprimi-la.
            # Medido no layout real (sistema de 160 MB): 55,1 MB com a janela
            # padrao de 64 MiB -> 27,8 MB com 192 MiB. Custo: ~26 s de CPU e
            # ~192 MB de RAM para DEScomprimir (no PC, nao no console).
            gap=$(to_mb "$(part_size "$GENIMAGE_CFG" system-a)")
            if [ -n "$gap" ]; then
                dict_mb=$((gap + 32))
                [ "$dict_mb" -gt 512 ] && dict_mb=512
                echo "post-image: comprimindo com xz -9 (janela ${dict_mb} MiB)"
                if xz -9 -T0 -k -f --lzma2="dict=${dict_mb}MiB" "$IMG" \
                        2>/dev/null; then
                    :
                else
                    echo "post-image: aviso: este xz recusou a janela grande;" >&2
                    echo "            usando xz -9 padrao (artefato ~2x maior)" >&2
                    xz -9 -T0 -k -f "$IMG"
                fi
            else
                echo "post-image: comprimindo com xz -9"
                xz -9 -T0 -k -f "$IMG"
            fi
            ;;
        zstd)
            # --long=31 (janela de 2 GB) tambem deduplica a copia A/B.
            echo "post-image: comprimindo com zstd -19 --long=31"
            zstd -19 --long=31 -T0 -q -f "$IMG" -o "$IMG.zst"
            ;;
        gzip)
            echo "post-image: comprimindo com gzip -9"
            gzip -9 -k -f "$IMG"
            ;;
        "")
            echo "post-image: aviso: sem xz/zstd/gzip; sdcard.img fica so cru" >&2
            ;;
    esac
    for f in "$IMG.xz" "$IMG.zst" "$IMG.gz"; do
        [ -f "$f" ] && echo "post-image: artefato: $f ($(du -h "$f" | cut -f1))"
    done
fi
