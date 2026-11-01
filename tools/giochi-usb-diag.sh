#!/bin/sh
# Giochi Konsol OS v1.2 — diagnostico de conexao com o console.
#
# POR QUE EXISTE
# --------------
# "Liguei e nao aparece nada" tem quatro causas com acoes OPOSTAS, e do lado do
# PC elas sao indistinguiveis sem medir:
#
#   1. cabo de carga (nao tem fio de dados)   -> trocar o cabo
#   2. o console nao chegou ao userspace      -> ler o log do cartao
#   3. o gadget falhou (ep0out, F_FS)         -> ler o log do cartao
#   4. tudo certo, so' falta o IP no PC       -> este script configura
#
# A informacao que decide 1 vs 2/3 esta' nos DESCRITORES do USB: se aparece
# qualquer dispositivo novo no barramento, o cabo tem dados e o gadget subiu
# (a causa era 4). Se nao aparece NADA, e' 1 ou 2/3 — e ai' o log do cartao
# decide (docs/BOOT-DEBUG.md §1.5, AGENTS.md §3.36).
#
# USO
#   tools/giochi-usb-diag.sh              # mede agora e explica o resultado
#   tools/giochi-usb-diag.sh --esperar 30 # espera o dispositivo aparecer
#   tools/giochi-usb-diag.sh --cartao     # le o log de boot do cartao montado
set -u

PROG=giochi-usb-diag
ESPERAR=0
CARTAO=0
IFACE=
HOST=${GIOCHI_HOST:-10.55.0.2}
PC_IP=10.55.0.1

log()  { printf '%s: %s\n' "$PROG" "$*"; }
secao() { printf '\n== %s\n' "$*"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --esperar) ESPERAR=${2:-30}; shift 2 ;;
        --cartao)  CARTAO=1; shift ;;
        --iface)   IFACE=${2:-}; shift 2 ;;
        -h|--help) sed -n '2,28p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) printf '%s: opcao desconhecida %s\n' "$PROG" "$1" >&2; exit 2 ;;
    esac
done

# --- 1. o cartao foi gravado e bootou? -----------------------------------
ler_cartao() {
    secao "log de boot no cartao"
    achou=0
    for d in $(findmnt -rno TARGET 2>/dev/null | grep -E '^/(run/)?media/'); do
        for f in "$d"/giochi-boot*.log "$d"/giochi-boot.dmesg; do
            [ -f "$f" ] || continue
            achou=1
            log "--- $f"
            # O marcador .inprogress diz se o boot ANTERIOR terminou (§3.32).
            [ -f "$d/giochi-boot.inprogress" ] && \
                printf '    ATENCAO: giochi-boot.inprogress presente -> o boot anterior NAO terminou\n'
            tail -n "${LINHAS:-25}" "$f" | sed 's/^/    /'
        done
    done
    if [ "$achou" = "0" ]; then
        log "nenhum giochi-boot.log encontrado."
        log "  monte a particao GIOCHI_BOOT do cartao (80 MB, FAT) e repita;"
        log "  se o cartao esteve no aparelho e nao ha log, o rcS NAO rodou:"
        log "  o boot parou antes do userspace (kernel/DTB/raiz)."
        return 1
    fi
    return 0
}

# --- 2. o que existe no barramento USB -----------------------------------
usb_novo() {
    # Descritores dos dispositivos que NAO sao hub/mouse/teclado conhecidos.
    # manufacturer/product identificam QUEM esta' rodando (AGENTS.md §3.36):
    #   "Giochi Konsol OS" -> o nosso umtprd subiu
    #   "ROCKNIX"/"Handheld Device" -> o firmware de fabrica
    for d in /sys/bus/usb/devices/*; do
        [ -r "$d/idVendor" ] || continue
        vid=$(cat "$d/idVendor" 2>/dev/null)
        pid=$(cat "$d/idProduct" 2>/dev/null)
        case "$vid" in
            1d6b|8087|046d|1bcf|30fa|0c45|11b0) continue ;;  # hubs, mouse...
        esac
        man=$(cat "$d/manufacturer" 2>/dev/null || echo "?")
        prod=$(cat "$d/product" 2>/dev/null || echo "?")
        printf '%s:%s  %s / %s\n' "$vid" "$pid" "$man" "$prod"
    done
}

# --- 3. rede do gadget --------------------------------------------------
achar_iface() {
    [ -n "$IFACE" ] && { printf '%s' "$IFACE"; return 0; }
    for i in $(ip -br link 2>/dev/null | awk '{print $1}' | grep -E '^(usb|enp0s|enx)'); do
        printf '%s' "${i%%@*}"
        return 0
    done
    return 1
}

tem_ip() { ip -br addr show "$1" 2>/dev/null | grep -q "$PC_IP"; }

# -----------------------------------------------------------------------
if [ "$CARTAO" = "1" ]; then
    ler_cartao
    exit $?
fi

secao "barramento USB"
DISPOSITIVOS=$(usb_novo)

if [ -z "$DISPOSITIVOS" ] && [ "$ESPERAR" -gt 0 ]; then
    log "nada no barramento; esperando ${ESPERAR}s (plugue o cabo agora)"
    i=0
    while [ "$i" -lt "$ESPERAR" ]; do
        sleep 1
        i=$((i + 1))
        DISPOSITIVOS=$(usb_novo)
        [ -n "$DISPOSITIVOS" ] && break
    done
fi

if [ -z "$DISPOSITIVOS" ]; then
    log "nenhum dispositivo novo."
    printf '\n'
    log "O console NAO esta' enumerando no USB do PC. Tres causas, por ordem"
    log "de frequencia:"
    log "  1. CABO DE CARGA (sem fio de dados). E' a mais comum. Troque por um"
    log "     cabo que transfira arquivo — o do celular serve."
    log "  2. o console NAO CHEGOU ao userspace (boot parou antes do rcS)."
    log "     Prova: tools/giochi-usb-diag.sh --cartao  (le o log do cartao)"
    log "  3. o GADGET falhou ao iniciar. Mesma prova: o log do cartao dira"
    log "     'gadget USB configurado em modo mtp' ou o erro."
    log ""
    log "Nenhuma dessas se resolve pelo lado do PC: sem enumeracao, nao ha' IP"
    log "nem MTP para testar. Esta' medido: 0 dispositivos novos."
    exit 1
fi

log "encontrado:"
printf '%s\n' "$DISPOSITIVOS" | sed 's/^/    /'

case "$DISPOSITIVOS" in
    *"Giochi Konsol OS"*)
        log "-> e' o NOSSO sistema (o umtprd subiu)." ;;
    *ROCKNIX*|*Handheld*|*handheld*)
        log "-> ATENCAO: parece o firmware DE FABRICA, nao o nosso."
        log "   O boot caiu para o eMMC: o cartao nao foi usado (AGENTS.md §3.35/§3.36)." ;;
    *)
        log "-> dispositivo presente, fabricante nao reconhecido (pode ser o gadget"
        log "   sem o umtprd, que e' o caso 'MTP nao subiu, mas a rede sim')." ;;
esac

secao "MTP"
MTP=$(ls -d /run/user/"$(id -u)"/gvfs/mtp* 2>/dev/null | head -1)
if [ -n "$MTP" ]; then
    log "montado em: $MTP"
    ls "$MTP" 2>/dev/null | head -8 | sed 's/^/    /'
else
    log "sem volume MTP no gvfs."
    log "  (o gvfs monta quando o gadget expoe a funcao MTP; sem ele, o MTP"
    log "   do console nao aparece — mas a REDE pode funcionar)"
fi

secao "rede do gadget (SSH)"
IF=$(achar_iface || true)
if [ -z "$IF" ]; then
    log "nenhuma interface usb0/enp0s/enx."
    log "  Se o MTP apareceu mas a rede nao, o gadget subiu sem ECM/RNDIS."
    exit 2
fi
log "interface: $IF"

if ! tem_ip "$IF"; then
    log "atribuindo $PC_IP/24 em $IF (precisa de root)"
    if command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
        sudo -n ip addr add "$PC_IP/24" dev "$IF" 2>/dev/null || true
        sudo -n ip link set "$IF" up 2>/dev/null || true
    else
        log "  sem sudo sem senha; rode: sudo ip addr add $PC_IP/24 dev $IF"
        exit 3
    fi
fi

log "esperando $HOST:22 ..."
i=0
while [ "$i" -lt 20 ]; do
    if timeout 2 sh -c "echo > /dev/tcp/$HOST/22" 2>/dev/null; then
        log "SSH respondeu em $HOST:22"
        log "teste completo: tools/giochi-ctl.py ctl PING"
        exit 0
    fi
    sleep 1
    i=$((i + 1))
done

log "$HOST:22 nao respondeu."
log "  Modo Desenvolvedor desligado? E' o padrao de fabrica (§4.6.1)."
log "  Ligue em Select > Sistema > Modo Desenvolvedor, ou pelo cartao:"
log "      tools/giochi-dev-mode.sh"
exit 4
