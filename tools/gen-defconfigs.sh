#!/bin/sh
# Giochi Konsol OS v1.2 — gera buildroot/configs/giochi_<device>_defconfig.
#
# Fonte unica de verdade: buildroot/board/giochi/devices/*.conf (dados do
# aparelho) + buildroot/board/giochi/defconfig/base-<soc>.defconfig.
# Nunca edite os arquivos gerados: rode `make defconfigs`.
#
# Uso:
#   tools/gen-defconfigs.sh            # regenera todos
#   tools/gen-defconfigs.sh --check    # falha se estiverem desatualizados
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DEV_DIR="$ROOT/buildroot/board/giochi/devices"
BASE_DIR="$ROOT/buildroot/board/giochi/defconfig"
OUT_DIR="$ROOT/buildroot/configs"
CHECK=0

[ "${1:-}" = "--check" ] && CHECK=1

[ -d "$DEV_DIR" ] || { echo "gen-defconfigs: $DEV_DIR ausente" >&2; exit 1; }
mkdir -p "$OUT_DIR"

generated=0
stale=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

render() {
    conf=$1
    id=$(sed -n 's/^GIOCHI_ID=//p' "$conf" | head -n1)
    soc=$(sed -n 's/^GIOCHI_SOC=//p' "$conf" | head -n1)
    name=$(sed -n 's/^GIOCHI_NAME=//p' "$conf" | head -n1 | tr -d '"')
    dts=$(sed -n 's/^GIOCHI_DTS=//p' "$conf" | head -n1)
    upstream=$(sed -n 's/^GIOCHI_DTS_UPSTREAM=//p' "$conf" | head -n1)
    defconfig=$(sed -n 's/^GIOCHI_DEFCONFIG=//p' "$conf" | head -n1)
    uboot_defconfig=$(sed -n 's/^GIOCHI_UBOOT_DEFCONFIG=//p' "$conf" | head -n1)
    uboot_upstream=$(sed -n 's/^GIOCHI_UBOOT_UPSTREAM=//p' "$conf" | head -n1)
    uboot_version=$(sed -n 's/^GIOCHI_UBOOT_VERSION=//p' "$conf" | head -n1)
    uboot_tpl=$(sed -n 's/^GIOCHI_UBOOT_TPL=//p' "$conf" | head -n1)
    uboot_bl31=$(sed -n 's/^GIOCHI_UBOOT_BL31=//p' "$conf" | head -n1)
    # Minimo que tem o defconfig da X55 no U-Boot mainline (ver docs/DEVICES.md).
    : "${uboot_version:=2024.10}"

    [ -n "$id" ] && [ -n "$soc" ] && [ -n "$defconfig" ] || {
        echo "gen-defconfigs: $conf incompleto (ID/SOC/DEFCONFIG)" >&2
        exit 1
    }

    base="$BASE_DIR/base-$soc.defconfig"
    [ -f "$base" ] || {
        echo "gen-defconfigs: base ausente para SoC '$soc': $base" >&2
        exit 1
    }

    if [ "$uboot_upstream" = "yes" ] && [ -n "$uboot_defconfig" ]; then
        # BL31/TPL do rkbin: sem eles o SPL do RK3566 nao inicializa a DDR.
        rkbin_lines=""
        if [ -n "$uboot_tpl" ] && [ -n "$uboot_bl31" ]; then
            rkbin_lines="BR2_TARGET_UBOOT_NEEDS_ROCKCHIP_RKBIN=y
BR2_PACKAGE_ROCKCHIP_RKBIN=y
BR2_PACKAGE_ROCKCHIP_RKBIN_TPL_FILENAME=\"$uboot_tpl\"
BR2_PACKAGE_ROCKCHIP_RKBIN_BL31_FILENAME=\"$uboot_bl31\""
        fi

        uboot_lines="# U-Boot mainline $uboot_version (defconfig $uboot_defconfig).
BR2_TARGET_UBOOT=y
BR2_TARGET_UBOOT_BUILD_SYSTEM_KCONFIG=y
BR2_TARGET_UBOOT_CUSTOM_VERSION=y
BR2_TARGET_UBOOT_CUSTOM_VERSION_VALUE=\"$uboot_version\"
BR2_TARGET_UBOOT_BOARD_DEFCONFIG=\"$uboot_defconfig\"
BR2_TARGET_UBOOT_CONFIG_FRAGMENT_FILES=\"\$(BR2_EXTERNAL_GIOCHI_PATH)/board/giochi/uboot.fragment\"
BR2_TARGET_UBOOT_NEEDS_PYTHON3=y
BR2_TARGET_UBOOT_NEEDS_PYLIBFDT=y
BR2_TARGET_UBOOT_NEEDS_OPENSSL=y
BR2_TARGET_UBOOT_NEEDS_PYELFTOOLS=y
# mkeficapsule (compilado em make tools) usa libuuid e gnutls. Sem estas duas
# linhas o build so passa porque a maquina tem uuid-dev/gnutls-dev instalados;
# o runner de CI tem, uma imagem limpa nao. Falhas reais, em sequencia:
#   uuid/uuid.h: No such file or directory
#   gnutls/gnutls.h: No such file or directory
BR2_TARGET_UBOOT_NEEDS_UTIL_LINUX=y
BR2_TARGET_UBOOT_NEEDS_GNUTLS=y"
        # 'set -e' aborta com "$(... && ...)" quando o teste falha, entao a
        # concatenacao e feita em um if explicito.
        if [ -n "$rkbin_lines" ]; then
            uboot_lines="$uboot_lines
$rkbin_lines"
        fi
    else
        # Sem defconfig mainline: o build NAO tenta compilar U-Boot e a imagem
        # sai sem bootloader (use o do vendor). Ver docs/DEVICES.md.
        uboot_lines="# U-Boot do vendor (mainline sem defconfig para este aparelho).
BR2_TARGET_UBOOT=n"
    fi

    if [ "$upstream" = "yes" ]; then
        dts_lines="BR2_LINUX_KERNEL_INTREE_DTS_NAME=\"rockchip/$dts\""
    else
        # DTS externo: build-image.sh injeta BR2_LINUX_KERNEL_CUSTOM_DTS_PATH
        # ao gerar o defconfig de build (ver docs/DEVICES.md).
        dts_lines="# DTS externo ($dts): fornecido por GIOCHI_DTS_PATH em tools/build-image.sh"
    fi

    out="$tmp/$defconfig"
    # awk (e nao sed) porque DTS_LINES/UBOOT_LINES sao blocos multi-linha.
    # As ancoras ^...$ importam: sem elas o comentario "Placeholders: ..." do
    # topo do template tambem casa e o bloco sai DUPLICADO (o Buildroot avisa
    # "override: reassigning to symbol").
    awk -v dev="$id" -v soc="$soc" -v issue="Giochi Konsol OS v1.2 - $name" \
        -v dts="$dts_lines" -v uboot="$uboot_lines" '
        /^[[:space:]]*@DTS_LINES@[[:space:]]*$/   { print dts;   next }
        /^[[:space:]]*@UBOOT_LINES@[[:space:]]*$/ { print uboot; next }
        {
            gsub(/@GIOCHI_DEVICE@/, dev)
            gsub(/@GIOCHI_SOC@/, soc)
            gsub(/@ISSUE@/, issue)
            print
        }
    ' "$base" > "$out"

    # Verificacao: nenhum placeholder pode sobrar.
    if grep -q '@[A-Z_]*@' "$out"; then
        echo "gen-defconfigs: placeholder nao resolvido em $defconfig" >&2
        grep '@[A-Z_]*@' "$out" >&2
        exit 1
    fi

    target="$OUT_DIR/$defconfig"
    if [ "$CHECK" = "1" ]; then
        if ! cmp -s "$out" "$target"; then
            echo "DESATUALIZADO: buildroot/configs/$defconfig" >&2
            stale=$((stale + 1))
        fi
    else
        cp "$out" "$target"
        echo "gerado: buildroot/configs/$defconfig ($id, $soc)"
        generated=$((generated + 1))
    fi
}

for conf in "$DEV_DIR"/*.conf; do
    [ -f "$conf" ] || continue
    render "$conf"
done

# Defconfigs orfaos (aparelho removido) tambem contam como desatualizacao.
if [ "$CHECK" = "1" ]; then
    for target in "$OUT_DIR"/giochi_*_defconfig; do
        [ -f "$target" ] || continue
        found=0
        for conf in "$DEV_DIR"/*.conf; do
            [ -f "$conf" ] || continue
            expected=$(sed -n 's/^GIOCHI_DEFCONFIG=//p' "$conf" | head -n1)
            [ "$expected" = "$(basename "$target")" ] && found=1
        done
        if [ "$found" = "0" ]; then
            echo "ORFAO: buildroot/configs/$(basename "$target")" >&2
            stale=$((stale + 1))
        fi
    done

    if [ "$stale" -gt 0 ]; then
        echo "gen-defconfigs: $stale arquivo(s) desatualizado(s); rode 'make defconfigs'" >&2
        exit 1
    fi
    echo "gen-defconfigs: todos os defconfigs estao atualizados"
else
    echo "gen-defconfigs: $generated defconfig(s) gerado(s)"
fi
