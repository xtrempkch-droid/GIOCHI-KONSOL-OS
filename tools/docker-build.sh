#!/bin/sh
# Giochi Konsol OS v1.2 — build de imagem dentro de container.
#
# Motivo: o Buildroot 2024.02 nao compila em hosts com toolchain muito nova
# (GCC 15 quebra host-m4 1.4.19; CMake 4.x rejeita host-lzo). O runner do CI
# usa ubuntu-24.04 (GCC 13). Este wrapper reproduz o runner, entao o build
# local e o do CI usam o mesmo ambiente.
#
# Uso:
#   tools/docker-build.sh x55                 # imagem completa
#   tools/docker-build.sh x55 --target uboot  # so U-Boot + kernel (rapido)
#   tools/docker-build.sh --list              # aparelhos
#   tools/docker-build.sh --rebuild-image     # refaz a imagem do container
#
# Variaveis:
#   DOCKER_IMAGE   nome da imagem do container (default: giochi-builder:1)
#   BUILDROOT_REF  branch/tag do Buildroot (default: 2024.02.x)
#   JOBS           paralelismo (default: nucleos do host)
#   DL_DIR         cache de downloads (default: .cache/buildroot-dl)
#
# Nota: com o Docker do snap, o contexto de build precisa estar sob $HOME —
# o snap nao enxerga /tmp. Este script usa a raiz do repositorio, que em uso
# normal ja esta no home.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
IMAGE=${DOCKER_IMAGE:-giochi-builder:1}
BUILDROOT_REF=${BUILDROOT_REF:-2024.02.x}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}
DL_DIR=${DL_DIR:-$ROOT/.cache/buildroot-dl}
REBUILD=0
TARGET=image
DEVICE=

while [ $# -gt 0 ]; do
    case "$1" in
        --rebuild-image) REBUILD=1; shift ;;
        --target) TARGET=${2:-image}; shift 2 ;;
        --list) DEVICE=--list; shift ;;
        -h|--help) sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "docker-build: opcao desconhecida '$1'" >&2; exit 2 ;;
        *) DEVICE=${DEVICE:-$1}; shift ;;
    esac
done

command -v docker >/dev/null 2>&1 || {
    echo "docker-build: docker ausente" >&2; exit 1; }
docker info >/dev/null 2>&1 || {
    echo "docker-build: daemon do docker inacessivel" >&2; exit 1; }

if [ "$DEVICE" = "--list" ]; then
    exec sh "$ROOT/tools/build-image.sh" --list
fi

[ -n "$DEVICE" ] || {
    echo "docker-build: informe o aparelho (ou --list)" >&2; exit 2; }

case "$ROOT" in
    /tmp/*) echo "docker-build: aviso: $ROOT esta em /tmp; o docker do snap" \
                   "nao enxerga /tmp. Mova o repositorio para \$HOME." >&2 ;;
esac

if [ "$REBUILD" = "1" ] || ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "docker-build: construindo $IMAGE (Buildroot $BUILDROOT_REF)"
    docker build --build-arg "BUILDROOT_REF=$BUILDROOT_REF" \
        -t "$IMAGE" "$ROOT/tools/docker"
fi

mkdir -p "$DL_DIR" "$ROOT/dist"

# -u $(id -u):$(id -g): os arquivos em dist/ ficam do usuario do host, nao root.
# --init: o Buildroot lanca muitos processos; evita zumbis.
set -- docker run --rm --init \
    -u "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -e BR2_DL_DIR=/dl \
    -e JOBS="$JOBS" \
    -v "$ROOT:/work" \
    -v "$DL_DIR:/dl" \
    -w /work \
    "$IMAGE"

if [ "$TARGET" = "image" ]; then
    exec "$@" sh -c 'exec tools/build-image.sh "$0" --jobs "$JOBS"' "$DEVICE"
fi

# Alvos parciais: util para validar o build sem esperar rootfs e pacotes.
exec "$@" sh -c '
set -eu
mkdir -p "dist/$0"
cp "buildroot/configs/giochi_$0_defconfig" "dist/$0/defconfig"
make -C /opt/buildroot O="/work/dist/$0/build" BR2_EXTERNAL=/work/buildroot \
     defconfig BR2_DEFCONFIG="/work/dist/$0/defconfig"
for t in "$@"; do
    echo "== make $t =="
    make -C /opt/buildroot O="/work/dist/$0/build" \
         BR2_EXTERNAL=/work/buildroot -j"$JOBS" "$t"
done' "$DEVICE" $TARGET
