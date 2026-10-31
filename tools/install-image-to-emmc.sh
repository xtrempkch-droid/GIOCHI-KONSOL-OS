#!/bin/sh
# Giochi Konsol OS v1.2 — instala uma imagem no eMMC A PARTIR do sistema no SD.
#
# POR QUE ESTA FERRAMENTA EXISTE
# -----------------------------
# O BootROM do RK3566 procura bootloader em SPI -> eMMC -> SD. Um aparelho com
# firmware de fabrica no eMMC tem bootloader la, e e ele quem decide se olha o
# cartao. Enquanto isso nao estiver resolvido, a unica forma segura de rodar o
# Giochi no aparelho e: (1) o cartao inicia, (2) de dentro dele se instala a
# mesma imagem no eMMC, (3) desliga e REMOVE o cartao.
#
# A imagem e a MESMA para cartao e eMMC: quem resolve a raiz e o
# root=PARTUUID=47494f43-02 do boot.cmd (ver genimage.cfg disk-signature), nao
# mais um /dev/mmcblk1p2 fixo.
#
# Referencia: o mesmo fluxo do projeto Walkmam_RK3566, adaptado ao BusyBox deste
# rootfs -- que NAO tem lsblk, wipefs, partprobe nem stat (medido). Aqui:
# lsblk -> /sys/block e /proc/mounts; wipefs -> dd de zeros no inicio e no fim;
# stat  -> wc -c.
#
# Uso (no console, com o sistema rodando do SD):
#   install-image-to-emmc.sh /boot/giochi-x55-sdcard.img
#   install-image-to-emmc.sh /boot/giochi-x55-sdcard.img.xz    (descompacta)
set -eu

IMG=${1:-}
EMMC=${GIOCHI_EMMC:-/dev/mmcblk0}

die() { echo "install-image-to-emmc: ERRO: $*" >&2; exit 1; }
say() { echo "install-image-to-emmc: $*"; }

[ -n "$IMG" ] || die "informe a imagem (.img, .img.xz ou .img.gz)"
[ -r "$IMG" ] || die "nao consigo ler '$IMG'"
[ "$(id -u)" -eq 0 ] || die "precisa ser root"
[ -d /sys/block ] || die "/sys nao esta montado (precisa de sysfs)"

# --- 1) o alvo e o eMMC, e nao o cartao que esta rodando -------------------
_basename=$(basename "$EMMC")
[ -d "/sys/block/$_basename" ] || die "$EMMC nao existe"
# /dev/mmcblk0 e um no de devtmpfs, nao symlink: a controladora so aparece no
# sysfs. fe310000.mmc = eMMC; fe2b0000.mmc = SD1; fe000000.mmc = SD2.
_real=$(readlink -f "/sys/block/$_basename" 2>/dev/null || echo "")
case "$_real" in
    *fe310000.mmc*) ;;
    *) die "$EMMC nao parece ser o eMMC (sysfs: ${_real:-nada}).
       Esperado algo sob fe310000.mmc. O cartao do sistema e *fe2b0000.mmc*.
       Se voce sabe o que esta fazendo: GIOCHI_EMMC=/dev/mmcblkX" ;;
esac

# O sistema nao pode estar rodando DO eMMC alvo: dd no disco em uso destroi a
# instalacao em andamento (e nao da para remontar depois).
_rootdev=$(awk '$2 == "/" { print $1; exit }' /proc/mounts)
case "$_rootdev" in
    /dev/root|"") ;;                       # kernel sem /proc/mounts util
    "$EMMC"*) die "o sistema esta rodando DO $EMMC; grave a partir do cartao SD" ;;
esac

# --- 2) espaco: a imagem cabe no disco? ------------------------------------
_sect=$(cat "/sys/block/$_basename/size" 2>/dev/null || echo 0)
[ "$_sect" -gt 0 ] 2>/dev/null || die "nao consegui ler o tamanho de $EMMC"
_emmc_mb=$(( _sect * 512 / 1024 / 1024 ))

case "$IMG" in
    *.xz)
        # xz -l imprime "unpacked size" na 4a linha de dados; nao depende de
        # 200 MB de RAM para descompactar so para medir.
        _bytes=$(xz -l "$IMG" 2>/dev/null | awk 'NR==4 { print $5 * 1024 }') ;;
    *.gz)
        _bytes=$(gzip -l "$IMG" 2>/dev/null | awk 'NR==2 { print $2 }') ;;
    *)
        _bytes=$(wc -c < "$IMG") ;;
esac
[ -n "${_bytes:-}" ] && [ "$_bytes" -gt 0 ] 2>/dev/null || \
    die "nao consegui medir o tamanho de '$IMG' (arquivo truncado?)"
_img_mb=$(( _bytes / 1024 / 1024 ))

say "eMMC ($EMMC): ${_emmc_mb} MiB"
say "imagem ($IMG): ${_img_mb} MiB"
[ "$_img_mb" -le "$_emmc_mb" ] || die "a imagem NAO cabe no eMMC (${_img_mb} MiB > ${_emmc_mb} MiB).
       Gere uma imagem menor (o layout tem 936 MB; o eMMC da X55/X35H tem ~3776 MiB)."

# --- 3) confirmacao explicita ---------------------------------------------
echo
echo ">>> TUDO em $EMMC sera APAGADO, inclusive o firmware de fabrica."
echo ">>> O cartao SD em uso NAO e tocado."
printf 'Digite APAGAR para continuar: '
read -r _r || die "cancelado"
[ "$_r" = "APAGAR" ] || die "cancelado"

# --- 4) desmonta o que estiver em cima do eMMC -----------------------------
for _p in "$EMMC"p*; do
    [ -b "$_p" ] || continue
    if grep -q "^$_p " /proc/mounts; then
        say "desmontando $_p"
        umount "$_p" || die "nao consegui desmontar $_p"
    fi
done

# --- 5) zera as tabelas antigas (o firmware de fabrica usa GPT) ------------
# Nosso layout e MBR. A GPT de backup vive nos ULTIMOS setores e sobrevive a um
# dd que so escreve o comeco: o kernel depois reclama "GPT PMBR size mismatch"
# e pode "consertar" a tabela por cima da nossa. Zerar as duas pontas.
say "limpando tabelas de particao antigas"
dd if=/dev/zero of="$EMMC" bs=1M count=8 conv=fsync status=none
_seek=$(( _emmc_mb - 8 ))
if [ "$_seek" -gt 0 ]; then
    dd if=/dev/zero of="$EMMC" bs=1M seek="$_seek" count=8 conv=fsync status=none
fi

# --- 6) escreve ------------------------------------------------------------
say "gravando (alguns minutos)"
case "$IMG" in
    *.xz) xz -dc "$IMG" | dd of="$EMMC" bs=4M conv=fsync ;;
    *.gz) gzip -dc "$IMG" | dd of="$EMMC" bs=4M conv=fsync ;;
    *)    dd if="$IMG" of="$EMMC" bs=4M conv=fsync ;;
esac
sync

# --- 7) confere ------------------------------------------------------------
echo
say "pronto. O que ficou no eMMC:"
# Sem lsblk no BusyBox: le a tabela do proprio disco pelas particoes.
for _p in "$EMMC"p*; do
    [ -b "$_p" ] || continue
    _sz=$(cat "/sys/block/$_basename/$(basename "$_p")/size" 2>/dev/null || echo 0)
    printf '  %-16s %6s MiB  %s\n' "$_p" "$(( _sz * 512 / 1024 / 1024 ))" \
        "$(blkid "$_p" 2>/dev/null | sed 's/^[^:]*: //' || true)"
done
echo
say "proximo passo: desligar, REMOVER o cartao SD e ligar."
say "o bootloader procura SD1, depois eMMC (BOOT_TARGETS)."
