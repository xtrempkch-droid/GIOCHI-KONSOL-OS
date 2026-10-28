#!/bin/sh
# Instala os projetos dos jogos padrao no rootfs, um diretorio por id.
#
#   install-games.sh MANIFESTO ORIGEM DESTINO
#
# Le assets/default-games.json (fonte unica) e copia apenas o que o manifesto
# declara, sem .godot/ (cache do editor) e sem os laudos de bench. Nao ha
# logica de licenca aqui: quem barra licenca indevida e
# tools/check-default-games.py, no lint, antes de qualquer build.
set -eu

MANIFEST=${1:-}
SRC=${2:-}
DEST=${3:-}

[ -n "$MANIFEST" ] && [ -n "$SRC" ] && [ -n "$DEST" ] || {
    echo "uso: install-games.sh MANIFESTO ORIGEM DESTINO" >&2
    exit 2
}
[ -f "$MANIFEST" ] || { echo "install-games: '$MANIFEST' ausente" >&2; exit 1; }
[ -d "$SRC" ] || { echo "install-games: origem '$SRC' ausente" >&2; exit 1; }

# Lista "id<TAB>projeto" sem depender de jq (nao empacotado no rootfs).
LIST=$(python3 - "$MANIFEST" <<'PY' 2>/dev/null || echo ""
import json, sys
with open(sys.argv[1], encoding="utf-8") as handle:
    data = json.load(handle)
for game in data["games"]:
    print("%s\t%s" % (game["id"], game["project"]))
PY
)

if [ -z "$LIST" ]; then
    # Sem python3 no host do Buildroot, cai no extrator de texto simples.
    LIST=$(sed -n 's/.*"id": "\([a-z0-9-]*\)".*/\1/p' "$MANIFEST" | while read -r id; do
        printf '%s\t%s\n' "$id" "$id"
    done)
fi

[ -n "$LIST" ] || { echo "install-games: manifesto sem jogos" >&2; exit 1; }

mkdir -p "$DEST"
COUNT=0
while IFS="$(printf '\t')" read -r id project; do
    [ -n "$id" ] || continue

    # No aparelho o projeto e instalado por id; aceita caminho do repo tambem.
    if [ -d "$SRC/$project" ]; then
        FROM="$SRC/$project"
    elif [ -d "$SRC/$id" ]; then
        FROM="$SRC/$id"
    else
        echo "install-games: projeto de '$id' nao encontrado em $SRC" >&2
        exit 1
    fi

    mkdir -p "$DEST/$id"
    # -a preserva modo; exclui cache do editor e saidas de medicao.
    (cd "$FROM" && tar -cf - \
        --exclude=.godot \
        --exclude='*.import' \
        --exclude=bench \
        --exclude='*.json.tmp' .) | (cd "$DEST/$id" && tar -xf -)

    COUNT=$((COUNT + 1))
done <<LIST_EOF
$LIST
LIST_EOF

echo "giochi-games: $COUNT jogo(s) instalado(s) em $DEST"
