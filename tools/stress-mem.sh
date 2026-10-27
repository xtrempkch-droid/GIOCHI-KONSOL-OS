#!/bin/sh
# Giochi Konsol OS v1.2 — stress-mem.sh (S15).
# Valida zRAM (prioridade 100) + swap em SD (prioridade 10) com pressao
# controlada; confirma que zRAM e preferido e que nada e OOM-killed.
set -u

TAG=stress-mem
MB=${1:-64}
DURATION_S=${2:-20}

log() { echo "$TAG: $*"; }

command -v stress-ng >/dev/null 2>&1 || {
    log "stress-ng ausente; usando alocacao via awk"
    STRESS_CMD=""
}
STRESS_CMD=${STRESS_CMD:-stress-ng}

log "swap ativo:"
swapon -s 2>/dev/null || cat /proc/swaps

zram_prio=$(awk 'NR>1 && $1 ~ /zram/ {print $5}' /proc/swaps)
swap_prio=$(awk 'NR>1 && $1 !~ /zram/ {print $5}' /proc/swaps)
[ "${zram_prio:-0}" = "100" ] || log "AVISO: zRAM deveria ter prioridade 100"
[ -z "${swap_prio:-}" ] || [ "${swap_prio}" = "10" ] || \
    log "AVISO: swap SD deveria ter prioridade 10"

log "pressao: ${MB} MB por ${DURATION_S}s"
if [ -n "${STRESS_CMD:-}" ]; then
    "$STRESS_CMD" --vm 1 --vm-bytes "${MB}M" --timeout "${DURATION_S}s" \
                  --metrics-brief || log "stress-ng falhou"
else
    awk -v mb="$MB" -v dur="$DURATION_S" 'BEGIN {
        n = mb * 1024 * 16;
        for (i = 0; i < n; i++) buf[i] = i % 251;
        system("sleep " dur);
        print "alocacao concluida (" n " celulas)";
    }'
fi

dmesg 2>/dev/null | grep -i 'killed process' | tail -5 && \
    log "AVISO: OOM killer acionado durante o teste" || \
    log "sem OOM durante o teste"
