#!/bin/sh
# Giochi Konsol OS v1.2 — grow-user.sh
#
# Cresce a particao de dados (GIOCHI, montada em /user) para ocupar o cartao
# inteiro. Roda ANTES do `mount -a` do inittab, com /user ainda desmontado.
#
# POR QUE ESTE SCRIPT EXISTE
# --------------------------
# A imagem distribuida tem 936 MB: u-boot (16M) + boot (80M) + system-a/b
# (160M cada) + user (512M). Ela precisa caber em qualquer cartao, entao a
# particao de dados nasce pequena. Num cartao de 32 GB isso deixava ~28,8 GB
# inuteis — medido na X55:
#
#   /sys/class/block/mmcblk1/size      62333952 setores  (29,7 GiB)
#   /sys/class/block/mmcblk1p4/start     868352 setores
#   /sys/class/block/mmcblk1p4/size     1048576 setores  (512 MiB)
#   livres depois da p4              60407024 setores     (28,8 GiB)
#
# POR QUE ANTES DO mount -a
# -------------------------
# Mudar a tabela de particoes com o dispositivo em uso obriga o kernel a
# reler (BLKRRPART), e isso falha com a particao montada. Antes do `mount -a`
# ninguem tem o dispositivo aberto: a tabela nova vale no mount seguinte.
# Rodar este script por fora, com /user montado, avisa e nao faz nada.
#
# SEGURANCA
# ---------
#   - nunca encolhe nem move: so' estende a ULTIMA particao;
#   - verifica que a particao eh mesmo a ultima da tabela (nada depois dela);
#   - verifica que o sistema de arquivos eh ext2/3/4 antes de tocar;
#   - qualquer falha deixa o cartao como estava e o boot continua: /user
#     menor e' incomodo, /user quebrado e' perda de dados.
set -u

TAG=grow-user
log() {
    logger -t "$TAG" -- "$*" 2>/dev/null || true
    echo "$TAG: $*" >&2
}

# Abaixo disso nao vale a pena mexer (1 MiB em setores de 512 B).
MIN_SETORES=2048

# --- 1. a particao /user nao pode estar montada -------------------------
if mountpoint -q /user 2>/dev/null; then
    log "aviso: /user montada; rode isto antes do boot ou reinicie"
    log "  (mudar a tabela com o dispositivo em uso exige BLKRRPART, que falha)"
    exit 0
fi

# --- 2. qual disco tem a raiz -------------------------------------------
# root=PARTUUID=<uuid> no /proc/cmdline; o blkid resolve (o kernel nao expoe
# PARTUUID em /sys/class/block/*/uevent nesta configuracao — medido na X55).
# GIOCHI_ROOT_DEV aponta o DISCO a mao (imagem de teste, segundo cartao,
# crescer um .img antes de gravar). Vazio = descobre pelo cmdline, que e' o
# caminho normal no boot. Quando definido, a resolucao do cmdline e' pulada.
disco=
if [ -n "${GIOCHI_ROOT_DEV:-}" ]; then
    disco=$GIOCHI_ROOT_DEV
    raiz_part=$disco
else
    raiz_part=$(sed -n 's/.*[ ]root=\([^ ]*\).*/\1/p' /proc/cmdline 2>/dev/null | head -n1)
fi
case "${raiz_part:-}" in
    /dev/*)   raiz_dev=$raiz_part ;;
    PARTUUID=*|UUID=*|LABEL=*)
        if command -v blkid >/dev/null 2>&1; then
            raiz_dev=$(blkid -t "$raiz_part" -o device 2>/dev/null | head -n1)
        else
            raiz_dev=
        fi
        ;;
    # Caminho absoluto fora de /dev (arquivo de imagem, com GIOCHI_ROOT_DEV).
    # Os testes abaixo (`-b` / `-f`) e' que decidem se da' para usar.
    /*)       raiz_dev=$raiz_part ;;
    *)        raiz_dev= ;;
esac
eh_arquivo=0
if [ -z "${disco:-}" ]; then
    if [ -z "${raiz_dev:-}" ] || [ ! -b "$raiz_dev" ]; then
        log "nao consegui resolver root=${raiz_part:-?} para um no' de bloco; nada a fazer"
        exit 0
    fi
    # /dev/mmcblk1p2 -> /dev/mmcblk1 ; /dev/sda2 -> /dev/sda
    # Mesma ordem do giochi_disk_of: DOIS digitos antes de um, senao
    # /dev/sda10 vira /dev/sda1 e o script mexe no disco errado.
    case "$raiz_dev" in
        *p[0-9][0-9]) disco=${raiz_dev%p??} ;;
        *p[0-9])      disco=${raiz_dev%p?} ;;
        *[0-9][0-9])  disco=${raiz_dev%??} ;;
        *[0-9])       disco=${raiz_dev%?} ;;
        *)            log "raiz em '$raiz_dev' sem numero de particao; nada a fazer"; exit 0 ;;
    esac
elif [ -f "$disco" ]; then
    # Arquivo de imagem: da' para crescer um .img antes de gravar, e o teste
    # roda sem root/losetup.
    eh_arquivo=1
fi

# A particao de dados e' p4 do MESMO disco (layout do genimage.cfg).
if [ "$eh_arquivo" = "1" ]; then
    user_part="${disco}4"      # so' rotulo para o log; nao existe como no'
else
    case "$raiz_dev" in
        *p[0-9]*) user_part="${disco}p4" ;;
        *)        user_part="${disco}4" ;;
    esac
fi

nome=$(basename "$disco")
if [ "$eh_arquivo" = "0" ]; then
    [ -r "/sys/class/block/$nome/size" ] || {
        log "sem /sys/class/block/$nome/size; nada a fazer"; exit 0; }
fi

# --- 3. a particao de dados eh p4 do mesmo disco ------------------------
user_nome=$(basename "$user_part")

if [ "$eh_arquivo" = "0" ] && [ ! -b "$user_part" ]; then
    log "particao de dados $user_part nao existe; nada a fazer"
    exit 0
fi

# --- 4. geometria ------------------------------------------------------
if [ "$eh_arquivo" = "0" ]; then
    setores_disco=$(cat "/sys/class/block/$nome/size" 2>/dev/null)
    p4_inicio=$(cat "/sys/class/block/$user_nome/start" 2>/dev/null)
    p4_tam_atual=$(cat "/sys/class/block/$user_nome/size" 2>/dev/null)
else
    # Arquivo: tamanho em bytes / 512, e a entrada 4 vem do proprio sfdisk.
    _bytes=$(wc -c <"$disco" 2>/dev/null)
    if [ -n "${_bytes:-}" ]; then setores_disco=$(( _bytes / 512 )); fi
    _linha=$(sfdisk --dump "$disco" 2>/dev/null | sed -n 's/.*[^0-9]4 *: *start= *\([0-9]*\).*size= *\([0-9]*\).*/\1 \2/p')
    p4_inicio=${_linha%% *}
    p4_tam_atual=${_linha#* }
fi
for v in setores_disco p4_inicio p4_tam_atual; do
    eval "valor=\${$v:-}"
    case "${valor:-}" in
        ''|*[!0-9]*) log "geometria ilegivel ($v='${valor:-}'); nada a fazer"; exit 0 ;;
    esac
done

# A p4 precisa ser a ULTIMA particao da tabela: estender uma particao do meio
# sobrescreveria a seguinte.
if [ "$eh_arquivo" = "0" ]; then
    for outra in /sys/class/block/"$nome"*/start; do
        [ -r "$outra" ] || continue
        [ "$(basename "$(dirname "$outra")")" = "$user_nome" ] && continue
        inicio_outra=$(cat "$outra" 2>/dev/null)
        case "${inicio_outra:-}" in ''|*[!0-9]*) continue ;; esac
        if [ "$inicio_outra" -gt "$p4_inicio" ]; then
            log "ha' particao depois de $user_part; nao vou estender (risco de sobrepor dados)"
            exit 0
        fi
    done
else
    # `while` em pipe roda em subshell e o `exit` de dentro nao sai do
    # script; o awk decide e o exit fica no nivel de cima.
    _outras=$(sfdisk --dump "$disco" 2>/dev/null | \
        sed -n 's/.*[^0-9]\([0-9][0-9]*\) *: *start= *\([0-9]*\).*/\1 \2/p')
    if printf '%s\n' "$_outras" | awk -v p="$p4_inicio" \
         '$1 != 4 && $2 > p { achou=1 } END { exit !achou }'; then
        log "ha' particao depois de $user_part; nao vou estender (risco de sobrepor dados)"
        exit 0
    fi
fi

livres=$(( setores_disco - p4_inicio - p4_tam_atual ))
if [ "$livres" -lt "$MIN_SETORES" ]; then
    # Estado normal depois do primeiro boot: a particao ja ocupa o cartao.
    exit 0
fi

# --- 5. sistema de arquivos que sabemos crescer ------------------------
tipo=$(blkid -s TYPE -o value "$user_part" 2>/dev/null)
case "${tipo:-}" in
    ext2|ext3|ext4) ;;
    *)
        log "aviso: $user_part com tipo '${tipo:-desconhecido}'; so' cresco ext2/3/4"
        exit 0
        ;;
esac

novo_tam=$(( setores_disco - p4_inicio ))
log "crescendo $user_part: $((p4_tam_atual / 2048)) MiB -> $((novo_tam / 2048)) MiB (livres: $((livres / 2048)) MiB)"

# --- 6. tabela de particoes -------------------------------------------
# `sfdisk -N 4` reescreve SO' a entrada 4; as outras ficam intactas.
#
# O START vai EXPLICITO de proposito. Medido num arquivo com o MESMO layout da
# imagem (tests/test_grow_user.py): com `-N 4`, tanto ",+" quanto "<inicio>,+"
# preservam o offset — o `-N` nao usa o "inicio padrao". Ou seja, o ",+" NAO
# moveria a particao (eu tinha escrito que moveria, sem ter medido). O start
# explicito fica como cinto de seguranca: se alguem remover o `-N` num
# ajuste futuro, `,` passaria a significar "primeiro setor livre" e a p4
# encostaria na p3.
if command -v sfdisk >/dev/null 2>&1; then
    if ! printf '%s,+\n' "$p4_inicio" | sfdisk --force -N 4 "$disco" >/dev/null 2>&1; then
        log "ERRO: sfdisk falhou; cartao segue como estava"
        exit 0
    fi
else
    log "ERRO: sfdisk ausente (BR2_PACKAGE_UTIL_LINUX_BINARIES); nada a fazer"
    exit 0
fi

# O kernel precisa reler a tabela. Com o dispositivo livre o sfdisk ja' pediu
# isso; `blockdev --rereadpt` cobre o caso de a primeira tentativa ter
# falhado por uso momentaneo.
if command -v blockdev >/dev/null 2>&1; then
    blockdev --rereadpt "$disco" 2>/dev/null || \
        log "aviso: rereadpt falhou; o mount pode ver o tamanho antigo"
fi

tam_novo=$(cat "/sys/class/block/$user_nome/size" 2>/dev/null)
case "${tam_novo:-}" in
    ''|*[!0-9]*)
        log "aviso: nao consegui reler o tamanho de $user_part"
        tam_novo=$p4_tam_atual ;;
esac
if [ "$tam_novo" -le "$p4_tam_atual" ]; then
    log "aviso: a particao nao cresceu ($tam_novo <= $p4_tam_atual); /user segue com ${p4_tam_atual} setores"
    exit 0
fi

# --- 7. sistema de arquivos -------------------------------------------
# Desmontado aqui, entao resize2fs roda offline. O e2fsck antes nao e'
# cerimonia: resize2fs recusa (ou faz besteira) num fs marcado como unclean,
# e este cartao acabou de vir de uma imagem gravada em bloco.
if command -v e2fsck >/dev/null 2>&1; then
    e2fsck -f -y "$user_part" >/dev/null 2>&1 || true
fi
if resize2fs "$user_part" >/dev/null 2>&1; then
    log "pronto: $user_part com $((tam_novo / 2048)) MiB de sistema de arquivos"
else
    log "ERRO: resize2fs falhou; a particao tem $((tam_novo / 2048)) MiB e o fs ficou menor"
    log "  o sistema sobe normalmente; rode 'resize2fs $user_part' a mao"
fi
exit 0
