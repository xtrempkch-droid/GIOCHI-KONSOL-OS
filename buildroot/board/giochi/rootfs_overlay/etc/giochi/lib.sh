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
    # A ordem importa: o caso de DOIS digitos tem de vir antes do de um.
    # Com `*[0-9]` primeiro, `/dev/sda10` virava `/dev/sda1` (medido no host).
    case "$_part" in
        *p[0-9][0-9]) echo "${_part%p??}" ;;
        *p[0-9])      echo "${_part%p?}" ;;
        *[0-9][0-9])  echo "${_part%??}" ;;
        *[0-9])       echo "${_part%?}" ;;
        *)            echo "$_part" ;;
    esac
}

# Disco que CONTEM a particao raiz. Devolve 1 se nao der para saber.
#
# `giochi_disk_of("PARTUUID=47494f43-02")` devolve lixo ("PARTUUID=47494f43-0"):
# o corte de sufixo de particao so' faz sentido em caminho /dev. Como o
# cmdline deste sistema usa root=PARTUUID, a versao anterior nunca reconhecia
# o cartao de boot — e `giochi_other_disk` podia devolver o PROPRIO cartao de
# boot como "slot 2" (medido na X55: /dev/mmcblk1 e' o unico bloco).
giochi_boot_disk() {
    _root=$(giochi_root_part)
    case "${_root:-}" in
        /dev/*)
            giochi_disk_of "$_root"
            return 0 ;;
        PARTUUID=*|UUID=*|LABEL=*)
            if command -v blkid >/dev/null 2>&1; then
                _dev=$(blkid -t "$_root" -o device 2>/dev/null | head -n1)
                if [ -n "${_dev:-}" ]; then
                    giochi_disk_of "$_dev"
                    return 0
                fi
            fi ;;
    esac
    return 1
}

# Disco que NAO contem a raiz (segundo cartao = slot 2, secao 1.3).
#
# Criterios, todos medidos/sabidos:
#   - `removable` = 1  -> leitor de cartao; eMMC e SDIO ficam de fora
#   - mmcblk0 (eMMC)   -> nunca e' slot 2, e na X55 nem existe
#   - tem particao     -> leitor vazio nao serve
# Se nao der para saber qual e' o disco de boot, devolve 1 em vez de
# arriscar: tratar o cartao de boot como slot 2 significaria montar a
# particao FAT de boot em /user2.
giochi_other_disk() {
    _disco=$(giochi_boot_disk) || return 1
    [ -n "${_disco:-}" ] || return 1
    for _blk in /sys/block/mmcblk* /sys/block/sd*; do
        [ -e "$_blk" ] || continue
        _nome=$(basename "$_blk")
        [ "$_nome" = "mmcblk0" ] && continue
        [ "$(cat "$_blk/removable" 2>/dev/null)" = "1" ] || continue
        _dev="/dev/$_nome"
        [ "$_dev" = "$_disco" ] && continue
        [ -b "$_dev" ] || continue
        # Precisa de ao menos uma particao: leitor sem cartao tem size=0.
        _tem_part=0
        for _p in "$_blk"/"$_nome"p*; do
            [ -e "$_p" ] && { _tem_part=1; break; }
        done
        [ "$_tem_part" = "1" ] || continue
        echo "$_dev"
        return 0
    done
    return 1
}

# Primeira particao de um disco, com o nome certo para o tipo de dispositivo
# (/dev/mmcblk2p1 x /dev/sda1). Vazio se nao houver.
# p1 do MESMO disco da raiz: /dev/mmcblk1p2 -> /dev/mmcblk1p1, /dev/sda2 ->
# /dev/sda1. Nunca assume mmcblk0: o cartao SD e' mmcblk1 nestes aparelhos e
# mmcblk0 e' o eMMC, que a X55 nao tem (AGENTS.md 3.29).
giochi_boot_part() {
    _rp=${1:-}
    if [ -z "$_rp" ] && command -v giochi_root_part >/dev/null 2>&1; then
        _rp=$(giochi_root_part)
    fi
    # So' aceita caminho de dispositivo. Sem esta guarda, `root=UUID=...
    # 5431` casava em `*[0-9]` e a funcao devolvia o UUID truncado como se
    # fosse um no' de bloco.
    case "$_rp" in
        /dev/*) ;;
        *) echo ""; return 0 ;;
    esac
    case "$_rp" in
        *p[0-9][0-9]) echo "${_rp%p??}p1" ;;
        *p[0-9])      echo "${_rp%p?}p1" ;;
        *[0-9])       echo "${_rp%?}1" ;;
        *)            echo "" ;;
    esac
}

giochi_first_part() {
    _d=$1
    _n=$(basename "$_d")
    case "$_n" in
        mmcblk*)
            for _c in "$_d"p*; do [ -e "$_c" ] && { echo "$_c"; return 0; }; done ;;
        *)
            for _c in "$_d"[0-9] "$_d"[0-9][0-9]; do
                [ -e "$_c" ] && { echo "$_c"; return 0; }
            done ;;
    esac
    return 1
}

# No' de bloco da particao C (/user).
#
# NAO devolve PARTUUID: o f_mass_storage do kernel nao resolve esse formato e
# recusa a midia em silencio, deixando o PC ver um disco de 0 setores. Medido
# no serial da X55:
#
#   mass_storage.0/lun.0: unable to open backing file: PARTUUID=47494f43-04
#   -> no PC: sde 0B (File-Stor Gadget anuncia a funcao, sem midia)
#
# A versao anterior montava "PARTUUID=47494f43-02" + "4" e entregava isso ao
# gadget — o formato que o `mount` aceita (fstab usa LABEL=GIOCHI), nao
# o que o bloco aceita. Quem for montar continua usando o label do fstab.
giochi_user_part() {
    # 1. /user montado: /proc/mounts guarda o dispositivo ja' resolvido (o
    #    fstab usa LABEL, mas o ponto de montagem conhece o no' de bloco).
    _mnt=$(awk '$2 == "/user" { print $1; exit }' /proc/mounts 2>/dev/null)
    if [ -n "${_mnt:-}" ] && [ -b "$_mnt" ]; then
        echo "$_mnt"
        return 0
    fi
    # 2. root= como caminho de dispositivo: mesma numeracao de particao.
    _root=$(giochi_root_part)
    case "${_root:-}" in
        /dev/*)
            _disk=$(giochi_disk_of "$_root")
            for _cand in "${_disk}p4" "${_disk}4"; do
                if [ -b "$_cand" ]; then echo "$_cand"; return 0; fi
            done ;;
    esac
    # 3. by-label: so' existe com udev, mas e' caminho curto quando existe.
    if [ -L /dev/disk/by-label/GIOCHI ]; then
        _cand=$(readlink -f /dev/disk/by-label/GIOCHI 2>/dev/null)
        if [ -n "${_cand:-}" ] && [ -b "$_cand" ]; then echo "$_cand"; return 0; fi
    fi
    # 4. O layout A/B fixa p4 como particao de dados (genimage.cfg), entao o
    #    glob e' honesto: cobre root=PARTUUID com /user desmontado, que e'
    #    exatamente o caso do segundo USB_MODE MASS numa sessao.
    for _cand in /dev/mmcblk*p4 /dev/sd?4; do
        [ -b "$_cand" ] || continue
        echo "$_cand"
        return 0
    done
    return 1
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

# Caminho do log de boot na particao FAT (vazio se S00bootlog nao rodou).
giochi_bootlog_path() {
    [ -r /run/giochi/bootlog.path ] || return 1
    sed -n '1p' /run/giochi/bootlog.path 2>/dev/null
}

# Marca o boot como concluido (chamado quando o controle passa ao supervisor).
# Sem isso o proximo boot reportaria "BOOT ANTERIOR NAO TERMINOU" para sempre,
# porque rcS nao retorna: S90graphics faz exec do giochi-init.
giochi_boot_marker_clear() {
    _log=$(giochi_bootlog_path) || return 0
    [ -n "${_log:-}" ] || return 0
    _dir=$(dirname "$_log")
    rm -f "$_dir/giochi-boot.inprogress" 2>/dev/null || true
    return 0
}

giochi_active_slot() {
    case "$(giochi_root_part)" in
        *p2) echo 2 ;;
        *p3) echo 3 ;;
        *)   sed -n 's/.*giochi\.slot=\([23]\).*/\1/p' /proc/cmdline 2>/dev/null ;;
    esac
}

# --- settings.json: espelho em /run -------------------------------------
# O modo `mass` do gadget DESMONTA /user para entregar a particao ao PC
# (dois donos do mesmo ext4 corrompem). A partir dai' /user/settings.json
# nao existe mais, e quem decide o Modo Desenvolvedor e' justamente esse
# arquivo — que roda DEPOIS do S50usb.
#
# Medido na X55 (imagem de 01/11), no serial:
#
#   [1.794] EXT4-fs (mmcblk1p4): mounted filesystem ... r/w
#   [5.737] EXT4-fs (mmcblk1p4): unmounting filesystem ...
#   [5.753] configfs-gadget.giochi gadget.0: HOST MAC 02:47:69:6f:63:69
#
# e o efeito no PC: `enx0247696f6369` em NO-CARRIER, sem 10.55.0.2 e sem
# sshd — o S55usbnet nao achou settings nenhum e concluiu "desligado".
# Ou seja: o modo de USB decidia lendo o arquivo que ele mesmo destruia.
#
# O espelho corta a dependencia de ordem: o dono do dado continua sendo
# /user/settings.json (so' o Launcher escreve), e /run guarda o ultimo
# estado conhecido, refeito por quem le o arquivo original antes de
# qualquer coisa desmontar /user.
GIOCHI_SETTINGS_CACHE=${GIOCHI_SETTINGS_CACHE:-$GIOCHI_RUN_DIR/settings.json}

# Copia o settings do usuario para /run. Silencioso quando nao existe.
giochi_settings_snapshot() {
    _src=${1:-/user/settings.json}
    [ -r "$_src" ] || return 1
    giochi_ensure_run_dir
    cp "$_src" "$GIOCHI_SETTINGS_CACHE" 2>/dev/null || return 1
    chmod 600 "$GIOCHI_SETTINGS_CACHE" 2>/dev/null || true
    return 0
}

# Caminho do settings a LER: o do usuario quando existe, senao o espelho.
# Devolve 1 (sem imprimir nada) quando nao ha nenhum dos dois.
giochi_settings_path() {
    _pedido=${1:-/user/settings.json}
    if [ -r "$_pedido" ]; then
        echo "$_pedido"
        return 0
    fi
    [ -r "$GIOCHI_SETTINGS_CACHE" ] || return 1
    echo "$GIOCHI_SETTINGS_CACHE"
}

# --- chaves do dropbear: o MESMO problema do settings, um passo pior -------
# Em modo `mass` a particao /user vai inteira para o PC. /etc/dropbear e' um
# symlink para /user/.ssh/dropbear, entao TODAS as chaves (de host e
# authorized_keys) ficam penduradas num alvo que nao esta' montado:
#
#   S50usb: modo USB: mass (origem: /user/settings.json)
#   S62sshd: ERRO: /etc/dropbear nao e' gravavel (particao /user montada?)
#
# Ou seja: o adendo 4.6.1 diz "a rede USB esta' sempre presente", e ela esta'
# (usb0 com 10.55.0.2/24, medido) — mas sem chave o dropbear nao sobe, e o PC
# fica com interface UP e nenhum servico para acessar.
#
# O espelho em /run resolve a LEITURA. A geracao de chave de host, quando nao
# ha nenhuma copia, acontece em /run tambem: as chaves passam a ser volateis
# naquele boot (mudam a cada reinicio, o `known_hosts` do PC reclama), o que
# e' honestamente melhor que "SSH nao sobe". O log diz qual dos dois casos.
GIOCHI_SSH_CACHE=${GIOCHI_SSH_CACHE:-$GIOCHI_RUN_DIR/dropbear}

# Copia o diretorio de chaves do usuario para /run. Silencioso quando nao
# existe — quem chama decide o que fazer com o resultado.
giochi_ssh_snapshot() {
    _src=${1:-/user/.ssh/dropbear}
    [ -d "$_src" ] || return 1
    mkdir -p "$(dirname "$GIOCHI_SSH_CACHE")" 2>/dev/null || return 1
    mkdir -p "$GIOCHI_SSH_CACHE" 2>/dev/null || return 1
    # Cada arquivo por vez: `cp -r` de um diretorio que pode estar sendo
    # desmontado deixa lixo pela metade e o erro e' engolido.
    _ok=0
    for _f in "$_src"/*; do
        [ -f "$_f" ] || continue
        cp "$_f" "$GIOCHI_SSH_CACHE/" 2>/dev/null && _ok=1
    done
    [ "$_ok" = "1" ]
}

# Diretorio de chaves a USAR. Devolve o do usuario quando gravavel; senao o
# espelho de /run (criado se preciso). Devolve 1 quando nenhum serve.
giochi_ssh_dir() {
    _pedido=${1:-/etc/dropbear}
    # Resolve o symlink: /etc/dropbear -> /user/.ssh/dropbear. `readlink -f`
    # funciona mesmo com o alvo ausente, desde que o diretorio pai exista —
    # e' exatamente o caso da particao recem-formatada.
    _alvo=$_pedido
    if [ -L "$_pedido" ]; then
        _alvo=$(readlink -f "$_pedido" 2>/dev/null) || _alvo=
        [ -n "${_alvo:-}" ] || _alvo=$_pedido
    fi
    # Criar o alvo e' a PRIMEIRA tentativa: numa particao nova o diretorio
    # ainda nao existe, mas o pai (a particao) esta' montado e gravavel.
    # Tratar "nao existe" como "nao serve" jogava o sistema para o espelho
    # volátil sem precisar — e o teste de cartao recem-formatado pegou isso.
    if mkdir -p "$_alvo" 2>/dev/null && [ -w "$_alvo" ]; then
        echo "$_alvo"
        return 0
    fi
    if mkdir -p "$GIOCHI_SSH_CACHE" 2>/dev/null && \
       [ -w "$GIOCHI_SSH_CACHE" ]; then
        echo "$GIOCHI_SSH_CACHE"
        return 0
    fi
    return 1
}
