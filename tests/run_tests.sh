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

# Imagens de documentacao: contrato com os assets reais (tema/layout/splash).
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_screenshots) >/dev/null 2>&1; then
        echo "OK     screenshots (python)"
    else
        echo "FALHA  screenshots (python)"
        fail=$((fail + 1))
    fi
fi

# Jogos padrao: licenca, projeto, bench e documentacao (assets/default-games.json).
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_default_games) >/dev/null 2>&1; then
        echo "OK     jogos-padrao (python)"
    else
        echo "FALHA  jogos-padrao (python)"
        fail=$((fail + 1))
    fi
fi

# Layout do cartao (genimage.cfg): o u-boot tem de caber na regiao raw e as
# particoes nao podem se sobrepor. Sem isso o build morre no genimage depois de
# 1-3 h compilando (foi o que aconteceu com a regiao de 8 MiB).
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_image_layout) >/dev/null 2>&1; then
        echo "OK     imagem-layout (python)"
    else
        echo "FALHA  imagem-layout (python)"
        fail=$((fail + 1))
    fi
fi

# Proposta de layout (docs/proposta/): renders nativos das 5 abas nos dois
# paineis (640x480 e 1280x720) + as assercoes geometricas do proprio gerador.
# Sem Pillow a suite se auto-pula (SkipTest), como as outras de imagem.
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_layout_proposal) >/dev/null 2>&1; then
        echo "OK     proposta-layout (python)"
    else
        echo "FALHA  proposta-layout (python)"
        fail=$((fail + 1))
    fi
fi

# Escala do override por aparelho: cada numero de assets/devices/x55/*.json tem
# de ser round(base x 1,5), politica igual ao base, ou um dos especiais do
# painel. Foi a ausencia deste guarda que deixou 7 fatores implicitos (1,28 a
# 2,00) e 4,48 MB de thumbnail contra o teto de 4 MB. Sem Pillow: so stdlib.
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_device_scale) >/dev/null 2>&1; then
        echo "OK     escala-x55 (python)"
    else
        echo "FALHA  escala-x55 (python)"
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
