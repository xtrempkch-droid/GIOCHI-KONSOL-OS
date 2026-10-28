#!/bin/sh
# Giochi Konsol OS v1.2 — guarda contra arquivos do repositorio ignorados.
#
# O git casou padroes de .gitignore com NOMES DE DIRETORIO: um `*.d` solto
# ignorava scripts/init.d/ e rootfs_overlay/etc/init.d/ inteiros. O build local
# funcionava (os arquivos existiam na maquina) e o checkout do CI saia sem a
# sequencia de boot, falhando so no lint de links.
#
# Este script falha quando um arquivo necessario existe na arvore mas esta
# ignorado. Roda como parte de `make lint` e no CI.
#
# Uso: check-tracked.sh [raiz]
set -u

ROOT=${1:-.}
cd "$ROOT" || { echo "check-tracked: $ROOT inacessivel" >&2; exit 1; }

command -v git >/dev/null 2>&1 || {
    echo "check-tracked: git ausente; pulando"
    exit 0
}
git rev-parse --git-dir >/dev/null 2>&1 || {
    echo "check-tracked: nao e um repositorio git; pulando"
    exit 0
}

# Ignorados legitimos: saidas de build, cache do Python e splash.bmp derivado
# do painel de cada aparelho (assets/bootlogo/*/splash.bmp).
is_allowed() {
    case "$1" in
        build/*|output/*|dist/*|.cache/*) return 0 ;;
        .godot/*|*/.godot/*) return 0 ;;
        */__pycache__/*|__pycache__/*|*.pyc) return 0 ;;
        assets/bootlogo/*/splash.bmp) return 0 ;;
        keys/*|*.pem|pubkey.ed25519) return 0 ;;
        *.giochi|*.giochi.sig|*.manifest.json|*.img|*.vfat|*.squashfs) return 0 ;;
        *.o|*.d|*.a|*.so) return 0 ;;
    esac
    return 1
}

# -c core.quotePath=false: sem isso o git CITA caminhos com caractere nao-ASCII
#   (".../PrÃ¸ve.flac"), e o `case` abaixo nao reconhece o prefixo. Foi o
#   que fez o lint reprovar depois do primeiro build real, apontando arquivos
#   de dentro de dist/ como se fossem do projeto.
# Os `:(exclude)` evitam enumerar centenas de milhares de arquivos de saida a
#   cada lint (o build real cria ~800 mil em dist/).
bad=0
while IFS= read -r f; do
    [ -n "$f" ] || continue
    if ! is_allowed "$f"; then
        echo "  IGNORADO $f" >&2
        bad=$((bad + 1))
    fi
done <<EOF
$(git -c core.quotePath=false ls-files -o -i --exclude-standard -- . \
    ':(exclude)build' ':(exclude)dist' ':(exclude)output')
EOF

if [ "$bad" -ne 0 ]; then
    echo "check-tracked: $bad arquivo(s) do projeto estao no .gitignore" >&2
    echo "               um padrao de .gitignore pode estar casando com um diretorio" >&2
    exit 1
fi

# Os scripts de init precisam estar versionados: sem eles a imagem nao inicia.
for s in S05update S10storage S20graphics S30swap S40dongle S50usb \
         S55usbnet S62sshd S99launcher; do
    if ! git ls-files --error-unmatch "scripts/init.d/$s" >/dev/null 2>&1; then
        echo "check-tracked: scripts/init.d/$s nao esta versionado" >&2
        bad=$((bad + 1))
    fi
done
for s in rcS rcK; do
    p="buildroot/board/giochi/rootfs_overlay/etc/init.d/$s"
    if ! git ls-files --error-unmatch "$p" >/dev/null 2>&1; then
        echo "check-tracked: $p nao esta versionado" >&2
        bad=$((bad + 1))
    fi
done

[ "$bad" -eq 0 ] || exit 1
echo "ok"
