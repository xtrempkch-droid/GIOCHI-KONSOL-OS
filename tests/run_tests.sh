#!/bin/sh
# Giochi Konsol OS v1.2 — executor dos testes de host.
set -u

BIN=${1:-build/host/bin}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
fail=0
total=0

export GIOCHI_LOG_LEVEL=error

for t in test_json test_sha256 test_hotkey test_musicd test_transition \
         test_layout test_metadata test_settings test_buttonsmap; do
    total=$((total + 1))
    if [ ! -x "$BIN/$t" ]; then
        echo "FALTA  $t"
        fail=$((fail + 1))
        continue
    fi
    if "$BIN/$t"; then
        echo "OK     $t"
    else
        echo "FALHA  $t"
        fail=$((fail + 1))
    fi
done

# Integracao: precisa de python3 para escrever na FIFO binaria.
if [ -x "$ROOT/tests/integration/lifecycle_test.sh" ] && \
   command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if "$ROOT/tests/integration/lifecycle_test.sh" "$BIN" >/dev/null 2>&1; then
        echo "OK     lifecycle (integracao)"
    else
        echo "FALHA  lifecycle (integracao)"
        fail=$((fail + 1))
    fi
fi

# Assets de bootsplash (contrato consumido pelo render).
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_bootlogo) >/dev/null 2>&1; then
        echo "OK     bootlogo (python)"
    else
        echo "FALHA  bootlogo (python)"
        fail=$((fail + 1))
    fi
fi

# Gadget USB composto (rede + MTP) contra configfs falso.
if [ -x "$ROOT/tests/integration/usbgadget_test.sh" ]; then
    total=$((total + 1))
    if "$ROOT/tests/integration/usbgadget_test.sh" \
            "$ROOT/scripts/giochi-usb-mode" >/dev/null 2>&1; then
        echo "OK     usb-gadget (integracao)"
    else
        echo "FALHA  usb-gadget (integracao)"
        fail=$((fail + 1))
    fi
fi

# Modo Desenvolvedor / SSH via USB (adendo 4.6.1).
if [ -x "$ROOT/tests/integration/devmode_test.sh" ]; then
    total=$((total + 1))
    if "$ROOT/tests/integration/devmode_test.sh" \
            "$ROOT/scripts/init.d" "$BIN" >/dev/null 2>&1; then
        echo "OK     devmode (integracao)"
    else
        echo "FALHA  devmode (integracao)"
        fail=$((fail + 1))
    fi
fi

if [ -d "$ROOT/src/forge" ] && command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if PYTHONPATH="$ROOT/src/forge" python3 -m unittest discover \
            -s "$ROOT/tests/forge" >/dev/null 2>&1; then
        echo "OK     forge (python)"
    else
        echo "FALHA  forge (python)"
        fail=$((fail + 1))
    fi
fi

echo "----"
echo "$((total - fail))/$total suites OK"
[ "$fail" -eq 0 ]
