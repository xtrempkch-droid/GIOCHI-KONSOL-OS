#!/bin/sh
# Bench dos jogos padrao: roda cada um no modo --bench e junta os laudos.
#
#   tools/bench-games.sh                    # todos os jogos da lista
#   tools/bench-games.sh spacewar           # um jogo
#   OUT=/tmp/laudos tools/bench-games.sh    # onde gravar os JSON
#   FRAMES=3600 tools/bench-games.sh        # partida mais longa
#   QUALITY=2 tools/bench-games.sh          # preset fixo (comparavel)
#
# O motor Godot nao e dependencia do repositorio: sem ele o script avisa e sai
# com 0 (mesmo criterio de tests/test_screenshots.py). Use GODOT=/caminho.
#
# Roda no PC e no aparelho. No aparelho, chame com o jogo instalado em
# /usr/share/giochi/games (GAMES_ROOT=/usr/share/giochi/games).
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
MANIFEST=${MANIFEST:-$ROOT/assets/default-games.json}
OUT=${OUT:-$ROOT/build/bench}
FRAMES=${FRAMES:-1800}
QUALITY=${QUALITY:-}
GAMES_ROOT=${GAMES_ROOT:-}
GODOT=${GODOT:-godot}

if ! command -v "$GODOT" >/dev/null 2>&1 && [ ! -x "$GODOT" ]; then
    echo "bench-games: Godot nao encontrado (defina GODOT=/caminho/do/binario); pulando"
    exit 0
fi

command -v python3 >/dev/null 2>&1 || {
    echo "bench-games: python3 necessario para ler o manifesto; pulando"
    exit 0
}

mkdir -p "$OUT" || exit 1
# O Godot so grava em caminho ABSOLUTO (res://, user:// ou /...): relativo
# falha. Resolve aqui para o script funcionar de qualquer diretorio.
OUT=$(cd "$OUT" && pwd) || exit 1

# Projetos e ids vem do manifesto: a lista de jogos padrao e a fonte unica.
LIST=$(python3 - "$MANIFEST" "$GAMES_ROOT" <<'PY'
import json, os, sys
manifest, games_root = sys.argv[1], sys.argv[2]
with open(manifest, encoding="utf-8") as handle:
    data = json.load(handle)
if data.get("excluded") != []:
    pass
for game in data["games"]:
    project = game["project"]
    if games_root:
        # No aparelho o projeto e instalado por id, sem o caminho do repo.
        project = os.path.join(games_root, game["id"])
    # TAB como separador: os caminhos tem ESPACO ("Colossal Cave Adventure ").
    print("%s\t%s" % (game["id"], project))
PY
) || exit 1

# Filtro de linha de comando guardado ANTES do laco: o `set --` abaixo monta os
# argumentos do Godot e sobrescreveria $1 (o filtro falhava na 2a iteracao).
FILTER=" ${1:-} "

status=0
# Le com TAB de separador (os caminhos tem espaco) e mantem o espaco em volta.
while IFS="$(printf '\t')" read -r id project; do
    [ -n "$id" ] || continue

    case "$FILTER" in
        "  ") ;;
        *" $id "*) ;;
        *) continue ;;
    esac

    if [ ! -d "$project" ]; then
        echo "bench-games: projeto de '$id' ausente em $project" >&2
        status=1
        continue
    fi

    echo "== $id =="
    set -- --bench --bench-frames "$FRAMES" --bench-out "$OUT/$id.json"
    if [ -n "$QUALITY" ]; then
        set -- "$@" --bench-quality "$QUALITY"
    fi
    # </dev/null: o Godot herdaria o stdin do laco e comeria o resto da lista.
    if ! "$GODOT" --path "$project" -- "$@" </dev/null 2>&1 | grep -E "^bench "; then
        echo "bench-games: '$id' nao emitiu laudo" >&2
        status=1
        continue
    fi
    # O exit code do Godot vem dentro do laudo: confere o campo, nao o grep.
    python3 - "$OUT/$id.json" <<'PY' || status=1
import json, sys
try:
    with open(sys.argv[1], encoding="utf-8") as handle:
        report = json.load(handle)
except (OSError, json.JSONDecodeError) as exc:
    print(f"bench-games: laudo ilegivel: {exc}", file=sys.stderr)
    raise SystemExit(1)
if not report["budget"]["passed"]:
    print(f"bench-games: orcamento estourado em {report['game']}", file=sys.stderr)
    raise SystemExit(1)
PY
done <<EOF
$LIST
EOF

echo "laudos em $OUT"
exit $status
