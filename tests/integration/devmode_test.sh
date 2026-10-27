#!/bin/sh
# Giochi Konsol OS v1.2 — teste do Modo Desenvolvedor (adendo 4.6.1).
#
# Verifica o contrato do adendo nos scripts de init, com um PATH falso
# (ip/dropbear/dropbearkey/install/logger/pidof) e um settings.json
# temporario:
#   - desativado por padrao: nenhum processo e nenhum endereco IP;
#   - console em 10.55.0.2/24 (PC em 10.55.0.1/24, estatico sem DHCP);
#   - dropbear escuta SO em 10.55.0.2:22;
#   - sem chave e sem senha o daemon NAO sobe;
#   - com authorized_keys o daemon sobe;
#   - 'stop' derruba o daemon pelo pid file.
#
# Opcionalmente valida o comando DEV_MODE no control.sock do giochi-init real
# (segundo argumento = diretorio de binarios).
#
# Uso: devmode_test.sh [scripts/init.d] [build/host/bin]
set -u

INITDIR=${1:-scripts/init.d}
BIN=${2:-}
NET="$INITDIR/S55usbnet"
SSHD="$INITDIR/S62sshd"

if [ ! -f "$NET" ] || [ ! -f "$SSHD" ]; then
    echo "devmode: scripts nao encontrados em $INITDIR" >&2
    exit 1
fi

fail=0
check() {
    if [ "$2" = "1" ]; then
        echo "  ok   $1"
    else
        echo "  FALHA $1" >&2
        fail=$((fail + 1))
    fi
}

setup() {
    T=$(mktemp -d)
    mkdir -p "$T/bin" "$T/run" "$T/user"

    cat > "$T/bin/ip" <<'EOF'
#!/bin/sh
echo "ip $*" >> "$CALLS"
if [ "$1" = "addr" ] && [ "$2" = "show" ]; then
    [ -n "${IP_SHOW_OUT:-}" ] && echo "$IP_SHOW_OUT"
    exit 0
fi
exit 0
EOF

    cat > "$T/bin/dropbear" <<'EOF'
#!/bin/sh
echo "dropbear $*" >> "$CALLS"
exit 0
EOF

    cat > "$T/bin/dropbearkey" <<'EOF'
#!/bin/sh
# -t <tipo> -f <arquivo>
while [ $# -gt 0 ]; do
    case "$1" in
        -f) : > "$2"; shift ;;
    esac
    shift
done
exit 0
EOF

    cat > "$T/bin/pidof" <<'EOF'
#!/bin/sh
exit 1
EOF

    cat > "$T/bin/install" <<'EOF'
#!/bin/sh
# -d <dir> | -m <modo> <origem> <destino>
last=""
for a in "$@"; do last="$a"; done
case " $* " in
    *" -d "*) mkdir -p "$last" && exit 0 ;;
esac
n=$#
cp "$(eval echo \${$((n - 1))})" "$last" 2>/dev/null
exit 0
EOF

    cat > "$T/bin/logger" <<'EOF'
#!/bin/sh
exit 0
EOF

    cat > "$T/bin/giochi_wait_for" <<'EOF'
#!/bin/sh
exit 0
EOF

    cat > "$T/bin/chpasswd" <<'EOF'
#!/bin/sh
cat >/dev/null
exit 1
EOF

    chmod +x "$T/bin"/*

    CALLS="$T/calls"
    : > "$CALLS"
    SETTINGS="$T/user/settings.json"
    IP_SHOW_OUT=""
    export CALLS SETTINGS IP_SHOW_OUT

    PATH="$T/bin:$PATH"
    export PATH

    # O script procura a chave aqui (USB_SSH_AUTHKEYS sobrescreve o padrao).
    USB_SSH_AUTHKEYS="$T/user/authorized_keys"
    USB_SSH_AUTHKEYS_DIR="$T/etc-dropbear"
    USB_SSH_KEYS="$T/keys"
    USB_SSH_PIDFILE="$T/run/dropbear.pid"
    USB_NET_IFACE=usb0
    USB_SSH_BIND=10.55.0.2
    USB_SSH_PORT=22
    export USB_SSH_AUTHKEYS USB_SSH_AUTHKEYS_DIR USB_SSH_KEYS USB_SSH_PIDFILE
    export USB_NET_IFACE USB_SSH_BIND USB_SSH_PORT
}

settings_with() {
    printf '{"developer":{"ssh_usb_enabled":%s}}\n' "$1" > "$SETTINGS"
}

# ---------------------------------------------------------------------
echo "cenario 1: desativado por padrao (sem settings.json)"
setup
sh "$NET" >/dev/null 2>&1
sh "$SSHD" >/dev/null 2>&1
check "nenhum endereco IP configurado" \
    "$(grep -q 'addr add' "$CALLS" && echo 0 || echo 1)"
check "dropbear nao iniciado" \
    "$(grep -q '^dropbear ' "$CALLS" && echo 0 || echo 1)"
rm -rf "$T"

# ---------------------------------------------------------------------
echo "cenario 2: developer.ssh_usb_enabled = false explicito"
setup
settings_with false
sh "$NET" >/dev/null 2>&1
sh "$SSHD" >/dev/null 2>&1
check "nenhum endereco IP configurado" \
    "$(grep -q 'addr add' "$CALLS" && echo 0 || echo 1)"
check "dropbear nao iniciado" \
    "$(grep -q '^dropbear ' "$CALLS" && echo 0 || echo 1)"
check "endereco residual removido quando presente" \
    "$(IP_SHOW_OUT='inet 10.55.0.2/24 scope global usb0'; \
       sh "$NET" >/dev/null 2>&1; \
       grep -q 'addr del 10.55.0.2/24 dev usb0' "$CALLS" && echo 1)"
rm -rf "$T"

# ---------------------------------------------------------------------
echo "cenario 3: ativo sem chave e sem senha"
setup
settings_with true
sh "$NET" >/dev/null 2>&1
check "console configurado em 10.55.0.2/24" \
    "$(grep -q 'addr add 10.55.0.2/24 dev usb0' "$CALLS" && echo 1)"
check "interface levantada" \
    "$(grep -q 'link set usb0 up' "$CALLS" && echo 1)"
check "PC nao recebe DHCP (nenhum servidor iniciado)" \
    "$(grep -qi 'udhcpd\|dnsmasq' "$CALLS" && echo 0 || echo 1)"
sh "$SSHD" >/dev/null 2>&1
check "dropbear NAO sobe sem chave nem senha" \
    "$(grep -q '^dropbear ' "$CALLS" && echo 0 || echo 1)"
rm -rf "$T"

# ---------------------------------------------------------------------
echo "cenario 4: ativo com authorized_keys"
setup
settings_with true
printf 'ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFake pc@teste\n' \
    > "$USB_SSH_AUTHKEYS"
sh "$NET" >/dev/null 2>&1
sh "$SSHD" >/dev/null 2>&1
check "chaves de host ed25519 e rsa geradas" \
    "$([ -f "$USB_SSH_KEYS/dropbear_ed25519_host_key" ] && \
       [ -f "$USB_SSH_KEYS/dropbear_rsa_host_key" ] && echo 1)"
check "dropbear escuta SO em 10.55.0.2:22" \
    "$(grep -q '^dropbear .*-p 10.55.0.2:22' "$CALLS" && echo 1)"
check "dropbear usa as chaves de host persistentes" \
    "$(grep -q -- '-r .*dropbear_ed25519_host_key' "$CALLS" && echo 1)"
check "authorized_keys instalada para o dropbear" \
    "$([ -s "$USB_SSH_AUTHKEYS_DIR/authorized_keys" ] && echo 1)"
rm -rf "$T"

# ---------------------------------------------------------------------
echo "cenario 5: 'stop' derruba o daemon"
setup
settings_with true
printf 'ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFake pc@teste\n' \
    > "$USB_SSH_AUTHKEYS"
mkdir -p "$T/run"
# dropbear falso: grava o pidfile pedido em -P e fica vivo.
cat > "$T/bin/dropbear" <<'EOF'
#!/bin/sh
echo "dropbear $*" >> "$CALLS"
pidfile=""
prev=""
for a in "$@"; do
    [ "$prev" = "-P" ] && pidfile="$a"
    prev="$a"
done
if [ -n "$pidfile" ]; then
    sleep 300 &
    echo $! > "$pidfile"
fi
exit 0
EOF
chmod +x "$T/bin/dropbear"
sh "$SSHD" >/dev/null 2>&1
check "pid file gravado" "$([ -s "$USB_SSH_PIDFILE" ] && echo 1)"
check "daemon vivo" \
    "$(kill -0 "$(cat "$USB_SSH_PIDFILE" 2>/dev/null)" 2>/dev/null && echo 1)"
settings_with false
sh "$SSHD" >/dev/null 2>&1
check "pid file removido no stop" \
    "$([ -f "$USB_SSH_PIDFILE" ] && echo 0 || echo 1)"
rm -rf "$T"

# ---------------------------------------------------------------------
# Cenario 6: contrato do control.sock (giochi-init real, em dry-run).
if [ -n "$BIN" ] && [ -x "$BIN/giochi-init" ] && \
   command -v python3 >/dev/null 2>&1; then
    echo "cenario 6: comando DEV_MODE no control.sock"
    setup
    mkdir -p "$T/run/giochi" "$T/usr"
    printf '#!/bin/sh\nwhile :; do sleep 0.1; done\n' > "$T/usr/giochi-launcher"
    chmod +x "$T/usr/giochi-launcher"
    GIOCHI_ROOT="$T" GIOCHI_LOG_LEVEL=error GIOCHI_DRY_RUN=1 \
        "$BIN/giochi-init" --profile "$T/profile.json" >"$T/init.log" 2>&1 &
    init_pid=$!
    i=0
    while [ "$i" -lt 60 ] && [ ! -S "$T/run/giochi/control.sock" ]; do
        sleep 0.05
        i=$((i + 1))
    done

    reply=$(python3 - "$T/run/giochi/control.sock" "DEV_MODE BLAH" <<'PY'
import socket, sys

with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
    s.settimeout(3)
    s.connect(sys.argv[1])
    s.sendall(sys.argv[2].encode() + b"\n")
    print(s.recv(256).decode().strip())
PY
)
    check "argumento invalido recusado" \
        "$([ "$reply" = "ERR invalid_mode" ] && echo 1)"

    reply=$(python3 - "$T/run/giochi/control.sock" "DEV_MODE ON" <<'PY'
import socket, sys

with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
    s.settimeout(3)
    s.connect(sys.argv[1])
    s.sendall(sys.argv[2].encode() + b"\n")
    print(s.recv(256).decode().strip())
PY
)
    check "DEV_MODE ON aceito" "$([ "$reply" = "OK dev_mode ON" ] && echo 1)"

    reply=$(python3 - "$T/run/giochi/control.sock" "DEV_MODE OFF" <<'PY'
import socket, sys

with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
    s.settimeout(3)
    s.connect(sys.argv[1])
    s.sendall(sys.argv[2].encode() + b"\n")
    print(s.recv(256).decode().strip())
PY
)
    check "DEV_MODE OFF aceito" "$([ "$reply" = "OK dev_mode OFF" ] && echo 1)"

    python3 -c 'import os,signal,sys; os.kill(int(sys.argv[1]), signal.SIGTERM)' \
        "$init_pid" 2>/dev/null
    wait "$init_pid" 2>/dev/null
    rm -rf "$T"
fi

echo
if [ "$fail" -ne 0 ]; then
    echo "devmode: $fail falha(s)" >&2
    exit 1
fi
echo "devmode: todos os cenarios OK"
