#!/bin/sh
# Giochi Konsol OS v1.2 — verifica simbolos que o olddefconfig pode derrubar.
# Uso: assert-kernel-config.sh .config
set -eu

KCONFIG=${1:-.config}
[ -f "$KCONFIG" ] || { echo "assert-kernel-config: $KCONFIG ausente" >&2; exit 1; }

status=0
check() {
    if grep -qx "$1=y" "$KCONFIG"; then
        echo "ok   $1"
    else
        echo "FALTA $1" >&2
        status=1
    fi
}

# Audio: o pai precisa estar ligado junto dos filhos (F6).
check CONFIG_SND_SOC_ROCKCHIP
check CONFIG_SND_SOC_ROCKCHIP_I2S
check CONFIG_SND_SOC_ROCKCHIP_I2S_TDM
check CONFIG_SND_SOC_RK817
check CONFIG_SND_SIMPLE_CARD
# Display / entrada / gadget.
check CONFIG_DRM_ROCKCHIP
check CONFIG_DRM_DW_MIPI_DSI
check CONFIG_INPUT_EVDEV
check CONFIG_USB_GADGET
check CONFIG_USB_CONFIGFS

exit $status
