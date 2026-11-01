#!/bin/sh
# O caminho de DESENHO do Launcher so' compila com SDL2.
#
# Medido em 2026-11-01: o host desta maquina nao tem SDL2 e o `make` passa
# felizmente — o bloco #ifdef GIOCHI_HAVE_SDL2 de render.cpp simplesmente nao e'
# compilado. Um erro de tipo ali (std::string passado a const char*) so'
# apareceu no build da IMAGEM, depois de ~20 min de Buildroot.
#
# Esta guarda compila (sem linkar) o render.cpp COM SDL2, usando o sysroot do
# alvo quando a arvore de build existe. Sem sysroot e sem SDL2 no host, avisa e
# nao falha: um clone limpo nao tem como compilar o caminho de desenho.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SRC="$ROOT/apps/launcher/render.cpp"
CFLAGS="-std=c++17 -fsyntax-only -Wall -Wextra -Wshadow -Wformat=2 -Wundef"
DEFS="-D_GNU_SOURCE -DGIOCHI_HAVE_SDL2 -DGIOCHI_HAVE_OPENSSL"
INCS="-I$ROOT/src/common -I$ROOT/src/init -I$ROOT/apps/launcher"

[ -f "$SRC" ] || { echo "  SKIP render.cpp ausente"; exit 0; }

# Procura um sysroot de alvo com SDL2 e o compilador correspondente em host/bin.
CXX=
SYSROOT=
for sr in "$ROOT"/dist/*/build/host/*/sysroot; do
    [ -d "$sr/usr/include/SDL2" ] || continue
    tuple=$(basename "$(dirname "$sr")")
    cand="$(dirname "$(dirname "$sr")")/bin/$tuple-g++"
    [ -x "$cand" ] || continue
    CXX=$cand
    SYSROOT=$sr
    break
done

if [ -n "$CXX" ]; then
    # shellcheck disable=SC2086
    $CXX $CFLAGS $DEFS $INCS -isystem "$SYSROOT/usr/include/SDL2" "$SRC"
    echo "  ok (render.cpp com SDL2 do alvo)"
elif command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl2 2>/dev/null; then
    # shellcheck disable=SC2046,SC2086
    g++ $CFLAGS $DEFS $INCS $(pkg-config --cflags sdl2) "$SRC"
    echo "  ok (render.cpp com SDL2 do host)"
else
    echo "  SKIP sem SDL2 no host e sem sysroot de alvo (rode o build uma vez)"
fi
