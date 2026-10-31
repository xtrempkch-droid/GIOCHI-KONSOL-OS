#!/bin/sh
# Giochi Konsol OS v1.2 — valida o device_table.txt do Buildroot.
#
# O makedevs le este arquivo no ULTIMO passo do build (depois de compilar
# toolchain, kernel, U-Boot, mesa3d e todos os pacotes) para montar a imagem do
# rootfs. Um campo errado ali derruba o build nesse ponto, com uma mensagem que
# nao aponta a linha:
#
#   makedevs: line 29: Unsupported file type g
#
# Foi o caso real de "/run g ..." — a letra certa e "d" (diretorio). Este script
# valida o formato em milissegundos, no lint, antes de qualquer compilacao.
#
# Formato (documentacao do Buildroot, system/device_table.txt):
#   <nome> <tipo> <modo> <uid> <gid> <major> <minor> <inicio> <inc> <contagem>
# tipo: f (arquivo) d (diretorio) c (char) b (block) p (fifo) l (symlink)
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT" || exit 1

TABLE=buildroot/board/giochi/device_table.txt
[ -f "$TABLE" ] || { echo "check-device-table: $TABLE ausente" >&2; exit 1; }

fail=0
n=0
while IFS= read -r line; do
    n=$((n + 1))
    case "$line" in
        ''|'#'*) continue ;;
    esac
    # O separador no arquivo do Buildroot e TAB; aceitamos espacos tambem,
    # porque o arquivo e editado a mao.
    # shellcheck disable=SC2086
    set -- $line
    if [ "$#" -ne 10 ]; then
        echo "  $TABLE:$n: $# campo(s), esperado 10" >&2
        echo "    $line" >&2
        fail=$((fail + 1))
        continue
    fi
    path=$1
    type=$2
    case "$path" in
        /*) ;;
        *) echo "  $TABLE:$n: caminho '$path' nao e absoluto" >&2
           fail=$((fail + 1)) ;;
    esac
    case "$type" in
        f|d|c|b|p|l) ;;
        *) echo "  $TABLE:$n: tipo '$type' invalido (use f d c b p l)" >&2
           echo "    $line" >&2
           fail=$((fail + 1)) ;;
    esac
done < "$TABLE"

if [ "$fail" -ne 0 ]; then
    echo "check-device-table: $fail problema(s); o makedevs falharia no fim do build" >&2
    exit 1
fi

echo "ok ($n linha(s))"
