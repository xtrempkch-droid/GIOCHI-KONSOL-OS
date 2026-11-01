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
#   tools/giochi-usb-diag.sh --cartao --linhas 60   # mais contexto do log
#
# O `--cartao` NAO mostra so' o fim do log: extrai primeiro as linhas de USB
# (a sonda do S50usb, o bind, os erros do dwc3) e as CLASSIFICA. Num log real
# de 1 141 linhas a sonda fica centenas de linhas antes do fim, e o `tail`
# sozinho escondia exatamente a resposta que a ferramenta existe para dar.
set -u

PROG=giochi-usb-diag
ESPERAR=0
CARTAO=0
IFACE=
LINHAS=15
HOST=${GIOCHI_HOST:-10.55.0.2}
PC_IP=10.55.0.1

log()  { printf '%s: %s\n' "$PROG" "$*"; }
secao() { printf '\n== %s\n' "$*"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --esperar) ESPERAR=${2:-30}; shift 2 ;;
        --cartao)  CARTAO=1; shift ;;
        --iface)   IFACE=${2:-}; shift 2 ;;
        --linhas)  LINHAS=${2:-15}; shift 2 ;;
        -h|--help) sed -n '2,27p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
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
            # As linhas do USB primeiro, e o rabo do log depois. Antes isto era
            # so' `tail -25`: num log de 1100 linhas a sonda do S50usb fica
            # centenas de linhas antes do fim e NUNCA aparecia — a ferramenta
            # feita para responder "o gadget ancorou?" nao mostrava a resposta.
            usb_do_log "$f"
            log "--- ultimas ${LINHAS:-15} linhas"
            tail -n "${LINHAS:-15}" "$f" | sed 's/^/    /'
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

# Extrai e CLASSIFICA as linhas de USB de um log de boot.
#
# Cada padrao abaixo corresponde a um estado diferente do controlador, e a
# acao muda em cada um. A sonda do S50usb (AGENTS.md §3.88) existe porque o
# log anterior nao distinguia "configurado" de "anexado".
usb_do_log() {
    arq=$1
    log "--- USB neste boot"

    # 1. A sonda do S50usb.
    sonda=$(grep -aE "S50usb:.*sonda:" "$arq" 2>/dev/null | sed 's/^.*sonda: *//')
    if [ -n "$sonda" ]; then
        printf '%s\n' "$sonda" | sed 's/^/    /'
        # O caso COMBINADO e' o mais informativo: DT ja' em peripheral e,
        # mesmo assim, nenhum UDC. Aí o problema nao e' o papel do controlador
        # — e' o probe dele (PHY, clock, power domain). Dizer isso separa
        # "falta o patch" de "o patch entrou e outra coisa quebrou".
        case "$sonda" in
            *"udc VAZIO"*)
                case "$sonda" in
                    *"peripheral"*)
                        log "   -> DT ja' esta' em peripheral e MESMO ASSIM nao ha'"
                        log "      UDC: o probe do dwc3/PHY nao completou. Procure"
                        log "      'dwc3' e 'usb2phy' no dmesg deste log." ;;
                    *)
                        log "   -> o CONTROLADOR nao entrou em modo dispositivo."
                        log "      E' DT (dr_mode/extcon) ou PHY. Nao e' script, e"
                        log "      nao adianta mexer no giochi-usb-mode." ;;
                esac ;;
        esac
        case "$sonda" in
            *"dr_mode USB-C = otg"*)
                log "   -> o DT ainda esta' em OTG: o patch de DT nao entrou"
                log "      na imagem (confira o dr_mode no DTB do cartao)." ;;
            *"dr_mode USB-C = peripheral"*)
                log "   -> o DT esta' em peripheral (patch aplicado)." ;;
        esac
    else
        log "    (sem sonda: imagem anterior a 2026-11-01 06:00)"
    fi

    # 2. O bind do gadget.
    if grep -qa "S50usb:.*gadget anexado ao UDC" "$arq" 2>/dev/null; then
        log "    bind: OK (o configfs recebeu o controlador)"
    elif grep -qa "S50usb:.*gadget NAO anexado" "$arq" 2>/dev/null; then
        log "    bind: FALHOU (o configfs ficou sem controlador)"
    fi

    # 3. O que o kernel reclamou. `failed to enable <ep>: <n>` vem do patch
    #    0004; sem ele a mesma mensagem nao traz o numero.
    kernel_usb=$(grep -aE "dwc3 .*(failed|error)|failed to enable ep|\
failed to initialize (peripheral|host)|dwc3 .*no UDC" "$arq" 2>/dev/null)
    if [ -n "$kernel_usb" ]; then
        printf '%s\n' "$kernel_usb" | sed 's/^/    /'
        case "$kernel_usb" in
            *"ep0out: -110"*|*"ep0out: -62"*)
                log "   -> -110/-62 e' TIMEOUT: o comando DEPCFG nao completou."
                log "      O core nao esta' em modo dispositivo (PRTCAPDIR) —"
                log "      e' o que o dr_mode = \"peripheral\" corrige (§3.88)." ;;
            *"ep0out: -22"*)
                log "   -> -22 e' NO_RESOURCE/EINVAL: falta recurso de endpoint."
                log "      Procure o WARN 'No resource for ep0out' acima." ;;
            *"ep0out: -11"*)
                log "   -> -11 e' BUS_EXPIRY: a janela de tempo passou." ;;
            *"ep0out"*)
                log "   -> ep0out recusado SEM o codigo de erro: imagem anterior"
                log "      ao patch 0004. O codigo decide o diagnostico." ;;
        esac
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
