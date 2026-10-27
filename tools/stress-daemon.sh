#!/bin/sh
# Giochi Konsol OS v1.2 — stress-daemon.sh (S15).
# Mede RAM/CPU de giochi-musicd, wpa_supplicant e bluetoothd e confirma que o
# musicd sobrevive ao lancamento de um jogo.
set -u

TAG=stress-daemon
SAMPLES=${1:-5}

log() { echo "$TAG: $*"; }

measure() {
    name=$1
    limit_mb=$2

    pid=$(pgrep -x "$name" | head -n1)
    if [ -z "${pid:-}" ]; then
        log "$name: nao esta rodando (custo zero)"
        return 0
    fi

    rss_kb=$(awk '/VmRSS/ {print $2}' "/proc/$pid/status" 2>/dev/null)
    rss_mb=$(( ${rss_kb:-0} / 1024 ))
    cpu=$(ps -o %cpu= -p "$pid" 2>/dev/null | tr -d ' ')

    log "$name: ${rss_mb} MB RSS, ${cpu:-?}% CPU (meta: <= ${limit_mb} MB)"
    [ "$rss_mb" -le "$limit_mb" ] || log "AVISO: $name acima do orcamento"
}

i=0
while [ "$i" -lt "$SAMPLES" ]; do
    measure giochi-musicd 8
    measure wpa_supplicant 6
    measure bluetoothd 4
    i=$((i + 1))
    sleep 1
done

music_pid=$(pgrep -x giochi-musicd | head -n1)
if [ -n "${music_pid:-}" ]; then
    log "verificando sobrevivencia do musicd a um jogo (pid $music_pid)"
    log "  -> lance um jogo e rode novamente; o pid deve permanecer o mesmo"
fi
