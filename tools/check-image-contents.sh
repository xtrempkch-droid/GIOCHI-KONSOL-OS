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

# --- 4. pacote local NAO pode usar a raiz do repositorio como SITE --------
# Os pacotes giochi-* compilam codigo deste repositorio. Se o SITE for a raiz,
# o rsync do Buildroot copia TUDO que estiver la -- inclusive dist/, que e onde
# o proprio build esta sendo escrito. O resultado e uma copia recursiva: um
# giochi-assets-1.2 de 66 GB (medido), build travado e disco cheio. Por isso o
# SITE tem de passar por GIOCHI_PKGSRC (snapshot so das fontes, feito pelo
# build-image.sh).
for mk in buildroot/package/giochi-*/*.mk; do
    [ -f "$mk" ] || continue
    grep -q '_SITE_METHOD *= *local' "$mk" || continue
    site=$(sed -n 's/^[A-Z0-9_]*_SITE *= *//p' "$mk" | head -n1)
    case "$site" in
        *GIOCHI_PKGSRC*) ;;
        *)
            echo "  ERRO $mk: SITE local sem GIOCHI_PKGSRC ($site)" >&2
            echo "       SITE na raiz do repo copia dist/ recursivamente (66 GB)" >&2
            fail=$((fail + 1))
            ;;
    esac
done

# build-image.sh precisa exportar o snapshot.
if [ -f tools/build-image.sh ]; then
    if ! grep -q 'GIOCHI_PKGSRC' tools/build-image.sh; then
        echo "  ERRO tools/build-image.sh nao exporta GIOCHI_PKGSRC" >&2
        fail=$((fail + 1))
    fi
    if ! grep -q 'export GIOCHI_PKGSRC' tools/build-image.sh; then
        echo "  ERRO tools/build-image.sh nao faz 'export GIOCHI_PKGSRC'" >&2
        fail=$((fail + 1))
    fi
fi

# --- 5. define de TEXTO: aspas so no Makefile da raiz -------------------
# Os .mk passam flags em EXTRA_CFLAGS="...", que atravessa DUAS camadas de
# shell (recipe do pacote e recipe do Makefile da raiz). Aspas dentro desse
# valor eram comidas na primeira camada: o compilador recebia
# -DGIOCHI_SOC=rk3566, o *simbolo*, em vez da string "rk3566". O erro aparece
# longe da causa (%s recebendo double e "control reaches end of non-void
# function"). Por isso os defines de texto moram no Makefile da raiz, que os
# aplica numa camada so, e os .mk passam apenas o valor cru.
for mk in buildroot/package/giochi-*/*.mk buildroot/external.mk; do
    [ -f "$mk" ] || continue
    if grep -qE -- '-DGIOCHI_(SOC|DEVICE|OS_VERSION|KERNEL_VERSION)=' "$mk"; then
        echo "  ERRO $mk: define de texto fora do Makefile da raiz" >&2
        grep -nE -- '-DGIOCHI_(SOC|DEVICE|OS_VERSION|KERNEL_VERSION)=' "$mk" |
            sed 's/^/       /' >&2
        echo "       passe GIOCHI_SOC=... como variavel; o Makefile poe as aspas" >&2
        fail=$((fail + 1))
    fi
done

if [ -f Makefile ]; then
    for var in GIOCHI_SOC GIOCHI_DEVICE GIOCHI_OS_VERSION GIOCHI_KERNEL_VERSION; do
        if ! grep -q -- "-D$var='\"" Makefile; then
            echo "  ERRO Makefile: -D$var sem aspas aplicadas no DEFS" >&2
            echo "       esperado: -D$var='\"\$(...)\"'" >&2
            fail=$((fail + 1))
        fi
    done
fi

if [ "$fail" -ne 0 ]; then
    echo "check-image-contents: $fail problema(s) de conteudo da imagem" >&2
    exit 1
fi

echo "ok"
