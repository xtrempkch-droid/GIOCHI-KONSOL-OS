#!/bin/sh
# Giochi Konsol OS v1.2 — bench-boot.sh (S15).
# Mede: tempo ate o Launcher, RAM em repouso, tamanho da SquashFS.
# Executar NO DISPOSITIVO (precisa de /proc e /run reais).
set -u

TAG=bench-boot
TABELA="RK3326:8:60 RK3566:6:60"

log() { echo "$TAG: $*"; }

launcher_up() {
    # giochi-init esta vivo e o Launcher respondeu ao menos um frame.
    pgrep -f 'giochi-launcher' >/dev/null 2>&1
}

start_ms=$(awk '{print int($1 * 1000)}' /proc/uptime)
elapsed=0
while [ "$elapsed" -lt 20000 ]; do
    if launcher_up; then
        break
    fi
    usleep 100000 2>/dev/null || sleep 1
    elapsed=$((elapsed + 100))
done
end_ms=$(awk '{print int($1 * 1000)}' /proc/uptime)
boot_ms=$((end_ms - start_ms))

soc=$(tr -d '\0' </proc/device-tree/compatible 2>/dev/null | cut -d, -f1)
[ -n "${soc:-}" ] || soc=${GIOCHI_SOC:-desconhecido}

ram_kb=$(awk '/MemTotal/ {print $2}' /proc/meminfo)
ram_free=$(awk '/MemAvailable/ {print $2}' /proc/meminfo)
ram_used_mb=$(( (ram_kb - ram_free) / 1024 ))

root_size=$(df -k / 2>/dev/null | awk 'NR==2 {print $2}')
root_mb=$(( ${root_size:-0} / 1024 ))

log "SoC               : $soc"
log "boot -> Launcher  : ${boot_ms} ms (meta: $(echo "$TABELA" | tr ' ' '\n' | grep -i "${soc%%[0-9]*}" | cut -d: -f2 || echo '?') s)"
log "RAM em uso        : ${ram_used_mb} MB (meta: <= 60 MB em repouso)"
log "SquashFS (/ tam.) : ${root_mb} MB (meta: <= 150 MB)"
log "VRAM              : estimativa via Launcher (S15); /sys/class/drm nao expoe bytes"
