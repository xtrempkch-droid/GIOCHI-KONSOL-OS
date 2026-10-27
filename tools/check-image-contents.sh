#!/bin/sh
# Giochi Konsol OS v1.2 — guarda do conteudo da imagem.
#
# Duas classes de erro aconteceram (ou quase) neste repositorio:
#
#   1. Ferramenta de PC indo parar no rootfs. `giochi-pack.sh` era instalado em
#      /usr/bin pelo pacote giochi-tools — script de autor/publicador, que
#      depende de GNU tar (--format=ustar, --owner/--group, -r) e de jq. No
#      console nao existe nem um nem outro: o script falharia sempre, e
#      ocupava espaco no SquashFS sem uso possivel.
#
#   2. Binario de console esquecido fora da imagem. `giochi-thumbgen` e
#      `giochi-extract` precisam existir no aparelho (§4.2) e
#      `giochi-verify` tambem (§4.3).
#
# Este script roda em `make lint` e falha nos dois sentidos.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT" || exit 1

fail=0

# --- 1. nada de ferramenta de PC no TARGET --------------------------------
# Estes arquivos pertencem ao lado do PC (autor/publicador) por definicao.
PC_ONLY="giochi-pack.sh giochi-sign.sh giochi-keys.sh giochi-forge"

for mk in buildroot/package/*/*.mk; do
    [ -f "$mk" ] || continue
    # Considera apenas o bloco que instala no alvo (nao o de host).
    block=$(awk '/INSTALL_TARGET_CMDS/,/^endef/' "$mk" 2>/dev/null)
    [ -n "$block" ] || continue
    for tool in $PC_ONLY; do
        case "$block" in
            *"$tool"*)
                echo "  ERRO $mk instala '$tool' no rootfs (ferramenta de PC)" >&2
                fail=$((fail + 1))
                ;;
        esac
    done
    # src/forge (app de PC em Python) tambem nao vai ao aparelho.
    case "$block" in
        *src/forge*)
            echo "  ERRO $mk instala src/forge no rootfs (app de PC)" >&2
            fail=$((fail + 1))
            ;;
    esac
done

# --- 2. binarios obrigatorios no console ----------------------------------
TOOLS_MK=buildroot/package/giochi-tools/giochi-tools.mk
if [ -f "$TOOLS_MK" ]; then
    target_block=$(awk '/INSTALL_TARGET_CMDS/,/^endef/' "$TOOLS_MK")
    for bin in giochi-verify giochi-extract giochi-thumbgen; do
        case "$target_block" in
            *"$bin"*) ;;
            *)
                echo "  ERRO $bin nao e instalado no rootfs (obrigatorio §4.2/§4.3)" >&2
                fail=$((fail + 1))
                ;;
        esac
    done
fi

# --- 3. `make install` do host: so o que e do console ---------------------
if [ -f Makefile ]; then
    install_block=$(awk '/^install:/,/^[a-z]+:/' Makefile | head -n 20)
    for tool in $PC_ONLY; do
        case "$install_block" in
            *"$tool"*)
                echo "  ERRO Makefile (install) instala '$tool' no destino" >&2
                fail=$((fail + 1))
                ;;
        esac
    done
fi

if [ "$fail" -ne 0 ]; then
    echo "check-image-contents: $fail problema(s) de conteudo da imagem" >&2
    exit 1
fi

echo "ok"
