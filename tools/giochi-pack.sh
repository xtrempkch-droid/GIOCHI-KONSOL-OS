#!/bin/sh
# Giochi Konsol OS v1.2 — giochi-pack.sh (S9, ADR-11).
#
# Empacota um diretorio de projeto em .giochi (tar puro) na ordem exigida:
#   metadata.json -> capa.png -> thumb.png (opcional) -> bin/
#
# Uso: giochi-pack.sh PROJETO SAIDA.giochi
set -eu

PROJ=${1:-}
OUT=${2:-}

usage() {
    echo "uso: giochi-pack.sh PROJETO SAIDA.giochi" >&2
    exit 2
}

[ -n "$PROJ" ] && [ -n "$OUT" ] || usage

# Ferramenta de PC. O tar do BusyBox (rootfs do console) nao aceita
# --format=ustar, --owner/--group nem -r (append): falharia no meio do
# empacotamento. Melhor recusar de cara, com o motivo.
if ! tar --version 2>/dev/null | grep -qi "gnu tar"; then
    echo "giochi-pack: precisa de GNU tar (o tar do BusyBox nao serve)" >&2
    echo "             este script roda no PC, nao no console" >&2
    exit 3
fi
[ -d "$PROJ" ] || { echo "giochi-pack: projeto '$PROJ' inexistente" >&2; exit 1; }
[ -f "$PROJ/metadata.json" ] || { echo "giochi-pack: metadata.json ausente" >&2; exit 1; }
[ -f "$PROJ/capa.png" ] || { echo "giochi-pack: capa.png 320x320 obrigatoria" >&2; exit 1; }
[ -d "$PROJ/bin" ] || { echo "giochi-pack: diretorio bin/ ausente" >&2; exit 1; }

# --- validacao de schema (jq no host) ---------------------------------
if command -v jq >/dev/null 2>&1; then
    arch=$(jq -r '.arch // "aarch64"' "$PROJ/metadata.json")
    runtime=$(jq -r '.runtime // "godot"' "$PROJ/metadata.json")
    entry=$(jq -r '.entrypoint // ""' "$PROJ/metadata.json")
    title=$(jq -r '.title // ""' "$PROJ/metadata.json")

    [ -n "$title" ] || { echo "giochi-pack: 'title' obrigatorio" >&2; exit 1; }
    [ "$arch" = "aarch64" ] || {
        echo "giochi-pack: arch '$arch' incompativel (ADR-13)" >&2; exit 1; }

    case "$runtime" in
        godot|retroarch|proton) ;;
        *) echo "giochi-pack: runtime '$runtime' desconhecido" >&2; exit 1 ;;
    esac

    case "$entry" in
        bin/*) ;;
        *) echo "giochi-pack: entrypoint deve comecar com bin/" >&2; exit 1 ;;
    esac
    case "$entry" in
        *..*) echo "giochi-pack: entrypoint com '..' recusado" >&2; exit 1 ;;
    esac
    [ -f "$PROJ/$entry" ] || {
        echo "giochi-pack: entrypoint '$entry' ausente" >&2; exit 1; }
else
    echo "giochi-pack: aviso: jq ausente; schema nao validado" >&2
fi

# --- empacotamento ----------------------------------------------------
rm -f "$OUT"
members="metadata.json capa.png"
[ -f "$PROJ/thumb.png" ] && members="$members thumb.png"

# shellcheck disable=SC2086
tar -C "$PROJ" -cf "$OUT" --format=ustar $members
# bin/ sem recompressao e sem preservar owner (ADR-11).
tar -C "$PROJ" -rf "$OUT" --format=ustar --owner=0 --group=0 bin

sha=$(sha256sum "$OUT" | awk '{print $1}')
echo "giochi-pack: $OUT"
echo "sha256: $sha"
