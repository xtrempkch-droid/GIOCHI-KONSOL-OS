# Giochi Konsol OS v1.2 — funcoes comuns dos scripts de init (POSIX/ash).
# Uso: . /etc/giochi/lib.sh
#
# NOTA (docs/hardware-e-drivers.md §2): o cartao SD aparece como mmcblk1 nos
# aparelhos de referencia (eMMC ausente). Nada aqui assume mmcblk0: a particao
# e derivada de /proc/cmdline (root=) com fallback para LABEL.

GIOCHI_RUN_DIR=${GIOCHI_RUN_DIR:-/run/giochi}

giochi_log() {
    _tag=${GIOCHI_LOG_TAG:-giochi}
    logger -t "$_tag" -- "$*" 2>/dev/null || echo "$_tag: $*" >&2
}

giochi_ensure_run_dir() {
    mkdir -p "$GIOCHI_RUN_DIR" 2>/dev/null || true
}

# Espera um path existir por ate N decimos de segundo (padrao 30 = 3 s).
giochi_wait_for() {
    _path=$1
    _tenths=${2:-30}
    _i=0
    while [ "$_i" -lt "$_tenths" ]; do
        [ -e "$_path" ] && return 0
        usleep 100000 2>/dev/null || sleep 1
        _i=$((_i + 1))
    done
    return 1
}

# Particao raiz ativa (ex.: /dev/mmcblk1p2 ou /dev/sda2). Vazio se desconhecida.
giochi_root_part() {
    sed -n 's/.*[ ]root=\([^ ]*\).*/\1/p' /proc/cmdline 2>/dev/null | head -n1
}

# Dispositivo base sem o sufixo de particao (/dev/mmcblk1p2 -> /dev/mmcblk1,
# /dev/sda2 -> /dev/sda).
giochi_disk_of() {
    _part=$1
    case "$_part" in
        *p[0-9])    echo "${_part%p?}" ;;
        *p[0-9][0-9]) echo "${_part%p??}" ;;
        *[0-9])     echo "${_part%?}" ;;
        *)          echo "$_part" ;;
    esac
}

# Disco que NAO contem a raiz (segundo cartao = slot 2, secao 1.3).
giochi_other_disk() {
    _root=$(giochi_root_part)
    _disk=$(giochi_disk_of "$_root")
    for _blk in /sys/block/mmcblk*; do
        [ -e "$_blk" ] || continue
        _dev="/dev/$(basename "$_blk")"
        [ "$_dev" = "$_disk" ] && continue
        [ -b "$_dev" ] || continue
        echo "$_dev"
        return 0
    done
    return 1
}

# Particao C (/user): p4 do mesmo disco da raiz; fallback por LABEL.
giochi_user_part() {
    _root=$(giochi_root_part)
    if [ -n "${_root:-}" ]; then
        _disk=$(giochi_disk_of "$_root" | head -n1)
        case "$_root" in
            *p[0-9]*) echo "${_disk}p4" ;;
            *)        echo "${_disk}4" ;;
        esac
        return 0
    fi
    _label=/dev/disk/by-label/GIOCHI_USER
    [ -e "$_label" ] && readlink -f "$_label"
}

# Slot inativo (A/B): raiz em p2 => p3; raiz em p3 => p2.
giochi_inactive_part() {
    _root=$(giochi_root_part)
    _disk=$(giochi_disk_of "$_root" | head -n1)
    case "$_root" in
        *p2) echo "${_disk}p3" ;;
        *p3) echo "${_disk}p2" ;;
        *)   echo "" ;;
    esac
}

# Ponto de montagem do slot 2 no modo B (/user2), se houver.
giochi_slot2_mount() {
    for _mnt in /user2 /mnt/sd2 /slot2; do
        mountpoint -q "$_mnt" 2>/dev/null && { echo "$_mnt"; return 0; }
    done
    return 1
}

# Swapfile do slot 2 (modo A): particao dedicada montada em /mnt/swap2.
giochi_slot2_swapdir() {
    for _mnt in /mnt/swap2 /mnt/sd2; do
        [ -d "$_mnt" ] && { echo "$_mnt"; return 0; }
    done
    return 1
}

giochi_active_slot() {
    case "$(giochi_root_part)" in
        *p2) echo 2 ;;
        *p3) echo 3 ;;
        *)   sed -n 's/.*giochi\.slot=\([23]\).*/\1/p' /proc/cmdline 2>/dev/null ;;
    esac
}
