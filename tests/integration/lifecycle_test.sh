#!/bin/sh
# Giochi Konsol OS v1.2 — teste de integracao do ciclo de vida (secao 2.2/4.7).
#
# Valida, com um giochi-init REAL e actors falsos:
#   1. Ao lancar um jogo, o Launcher recebe SIGTERM e e aguardado (waitpid).
#   2. O jogo roda e, ao terminar, o Launcher e REINICIADO.
#   3. Ao abrir um app externo (Ports), o Launcher e SUSPENSO (SIGSTOP) e
#      retomado (SIGCONT) — nao morto.
#
# Uso: lifecycle_test.sh BIN_DIR
set -u

BIN_DIR=${1:-build/host/bin}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
INIT="$BIN_DIR/giochi-init"
[ -x "$INIT" ] || { echo "lifecycle: $INIT ausente" >&2; exit 1; }

fail=0
check() {
    if [ "$2" = "1" ]; then
        echo "  ok   $1"
    else
        echo "  FALHA $1" >&2
        fail=$((fail + 1))
    fi
}

# --- cenario 1: jogo -----------------------------------------------------
setup() {
    T=$(mktemp -d)
    mkdir -p "$T/run/giochi" "$T/usr/bin" "$T/user"
    LOG="$T/events.log"
    : > "$LOG"

    # Launcher falso: registra inicio e SIGTERM, e fica vivo.
    cat > "$T/usr/bin/giochi-launcher" <<'STUB'
#!/bin/sh
echo "launcher:start pid=$$" >> "$EVENTS"
trap 'echo "launcher:term pid=$$" >> "$EVENTS"; exit 0' TERM INT
trap 'echo "launcher:cont pid=$$" >> "$EVENTS"' CONT
while :; do sleep 0.05; done
STUB

    # Jogo falso: registra execucao e sai em 0,2 s.
    cat > "$T/game.sh" <<'STUB'
#!/bin/sh
echo "game:run pid=$$" >> "$EVENTS"
sleep 0.2
echo "game:exit" >> "$EVENTS"
exit 0
STUB

    # App externo falso: registra e sai em 0,2 s.
    cat > "$T/usr/bin/appstub" <<'STUB'
#!/bin/sh
echo "app:run pid=$$" >> "$EVENTS"
sleep 0.2
exit 0
STUB

    chmod +x "$T/usr/bin/giochi-launcher" "$T/game.sh" "$T/usr/bin/appstub"
    export EVENTS="$LOG"
}

send_transition() {
    # $1 = cmd (1=launch jogo, 2=app), $2 = path
    python3 - "$T/run/giochi/transition" "$1" "$2" <<'PY'
import struct, sys, os

fifo, cmd, path = sys.argv[1], int(sys.argv[2]), sys.argv[3]
# giochi_transition_msg: magic(u32) version(u16) cmd(u16) path[256] meta[1024]
payload = struct.pack('<IHH', 0x31435447, 1, cmd)
payload += path.encode()[:255].ljust(256, b'\0')
payload += b'{}'.ljust(1024, b'\0')
fd = os.open(fifo, os.O_WRONLY | os.O_NONBLOCK)
os.write(fd, payload)
os.close(fd)
PY
}

stop_init() {
    [ -n "${INIT_PID:-}" ] || return 0
    kill "$INIT_PID" 2>/dev/null
    i=0
    while [ "$i" -lt 40 ]; do
        kill -0 "$INIT_PID" 2>/dev/null || return 0
        sleep 0.05
        i=$((i + 1))
    done
    kill -9 "$INIT_PID" 2>/dev/null
}

# --- cenario 1 -----------------------------------------------------------
echo "cenario 1: jogo (SIGTERM + waitpid + reinicio)"
setup
GIOCHI_ROOT="$T" GIOCHI_LOG_LEVEL=error GIOCHI_DRY_RUN=1 \
    "$INIT" --profile "$T/profile.json" >"$T/init.log" 2>&1 &
INIT_PID=$!
sleep 0.7

starts=$(grep -c 'launcher:start' "$LOG" 2>/dev/null || echo 0)
check "launcher iniciado no boot" "$([ "${starts:-0}" -ge 1 ] && echo 1 || echo 0)"

send_transition 1 "$T/game.sh"
sleep 1.2

check "jogo executou" "$(grep -q 'game:run' "$LOG" && echo 1)"
check "launcher recebeu SIGTERM antes do jogo" \
    "$(grep -q 'launcher:term' "$LOG" && echo 1)"
check "jogo terminou" "$(grep -q 'game:exit' "$LOG" && echo 1)"
starts2=$(grep -c 'launcher:start' "$LOG" 2>/dev/null || echo 0)
check "launcher reiniciado apos o jogo" \
    "$([ "${starts2:-0}" -ge 2 ] && echo 1 || echo 0)"
check "ordem: term antes do jogo, reinicio depois do jogo" \
    "$(python3 - "$LOG" <<'PY'
import sys

seq = [l.split()[0] for l in open(sys.argv[1]) if l.split()]
term = seq.index('launcher:term') if 'launcher:term' in seq else -1
game = seq.index('game:run') if 'game:run' in seq else -1
starts = [i for i, v in enumerate(seq) if v == 'launcher:start']
ok = (term >= 0 and game > term
      and any(i > game for i in starts))
if not ok:
    sys.stderr.write("  sequencia: %r\n" % seq)
print(1 if ok else 0)
PY
)"

stop_init
rm -rf "$T"

# --- cenario 2: app externo ---------------------------------------------
echo "cenario 2: app externo (SIGSTOP/SIGCONT, launcher nao morre)"
setup
GIOCHI_ROOT="$T" GIOCHI_LOG_LEVEL=error GIOCHI_DRY_RUN=1 \
    "$INIT" --profile "$T/profile.json" >"$T/init2.log" 2>&1 &
INIT_PID=$!
sleep 0.7

send_transition 2 "APP:appstub"
sleep 1.2

check "app executou" "$(grep -q 'app:run' "$LOG" && echo 1)"
check "launcher NAO recebeu SIGTERM" \
    "$(grep -q 'launcher:term' "$LOG" && echo 0 || echo 1)"
check "launcher retomado (SIGCONT)" "$(grep -q 'launcher:cont' "$LOG" && echo 1)"
starts=$(grep -c 'launcher:start' "$LOG" 2>/dev/null || echo 0)
check "launcher nao foi reiniciado" "$([ "${starts:-0}" -eq 1 ] && echo 1 || echo 0)"

stop_init
rm -rf "$T"

# --- cenario 3: pacote .giochi (extracao + execve) -----------------------
echo "cenario 3: pacote .giochi (giochi-extract + entrypoint)"
setup
cp "$BIN_DIR/giochi-extract" "$T/usr/bin/giochi-extract"

mkdir -p "$T/proj/bin"
cat > "$T/proj/metadata.json" <<'META'
{"title":"Demo","developer":"X","version":"1.0.0","arch":"aarch64",
 "runtime":"godot","graphics_api":"gles3","graphics_api_fallback":"gles2",
 "buttons_map":"default","entrypoint":"bin/demo"}
META
printf '\211PNGfake' > "$T/proj/capa.png"
cat > "$T/proj/bin/demo" <<'STUB'
#!/bin/sh
echo "game:run pid=$$" >> "$EVENTS"
echo "demo" > "$DEMO_MARKER"
STUB
chmod +x "$T/proj/bin/demo"
DEMO_MARKER="$T/demo.ran"
export DEMO_MARKER

if ! sh "$ROOT/tools/giochi-pack.sh" "$T/proj" "$T/demo.giochi" >/dev/null 2>&1; then
    echo "  FALHA giochi-pack"
    fail=$((fail + 1))
fi

GIOCHI_ROOT="$T" GIOCHI_LOG_LEVEL=error GIOCHI_DRY_RUN=0 \
    "$INIT" --profile "$T/profile.json" >"$T/init3.log" 2>&1 &
INIT_PID=$!
sleep 0.7

send_transition 1 "$T/demo.giochi"
sleep 1.5

check "pacote extraido em /run/giochi/<sha>" \
    "$(ls -d "$T"/run/giochi/*/bin/demo >/dev/null 2>&1 && echo 1)"
check "entrypoint executado" \
    "$(grep -q 'game:run' "$LOG" && echo 1)"
check "metadata.json extraido junto" \
    "$(ls "$T"/run/giochi/*/metadata.json >/dev/null 2>&1 && echo 1)"
check "launcher reiniciado apos o jogo" \
    "$([ "$(grep -c 'launcher:start' "$LOG" 2>/dev/null || echo 0)" -ge 2 ] && echo 1)"

stop_init
rm -rf "$T"

echo "----"
[ "$fail" -eq 0 ]
