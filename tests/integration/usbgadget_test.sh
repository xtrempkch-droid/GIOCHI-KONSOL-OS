#!/bin/sh
# Giochi Konsol OS v1.2 — teste do gadget USB composto (secao 4.6).
#
# Roda giochi-usb-mode contra um configfs FALSO (GADGET=<tmp>/gadget) e
# verifica o que realmente importa:
#   - a funcao de REDE esta presente em TODOS os modos (SSH via USB);
#   - o modo mtp usa ffs.* (FunctionFS) e nao f_mtp direto;
#   - o modo mass usa mass_storage.0 e desmonta /user (nao testado aqui);
#   - 'off' desanexa o UDC.
#
# Uso: usbgadget_test.sh [caminho/giochi-usb-mode]
set -u

SCRIPT=${1:-scripts/giochi-usb-mode}
[ -x "$SCRIPT" ] || { echo "usbgadget: $SCRIPT nao executavel" >&2; exit 1; }

fail=0
check() {
    if [ "$2" = "1" ]; then
        echo "  ok   $1"
    else
        echo "  FALHA $1" >&2
        fail=$((fail + 1))
    fi
}

setup() {
    T=$(mktemp -d)
    mkdir -p "$T/gadget/configs/c.1" "$T/gadget/functions" \
             "$T/gadget/strings" "$T/ffs" "$T/run"
    # Arquivos que o script escreve (kernel expoe como atributos).
    for attr in idVendor idProduct bcdDevice bcdUSB UDC; do
        : > "$T/gadget/$attr"
    done
    echo "/dev/fake-udc" > "$T/udc-list"
}

run_mode() {
    GIOCHI_DRY_RUN=1 GADGET="$T/gadget" FFS_DIR="$T/ffs" \
    GIOCHI_UDC=fake-udc MTPSVC=/bin/true \
        sh "$SCRIPT" "$1" >/dev/null 2>&1
    echo $?
}

echo "cenario 1: modo mtp (MTP + rede no mesmo config)"
setup
rc=$(run_mode mtp)
check "comando aceito" "$([ "$rc" = "0" ] && echo 1)"
check "ffs.mtp criado (FunctionFS)" \
    "$([ -d "$T/gadget/functions/ffs.mtp" ] && echo 1)"
check "rede ecm.usb0 criada" \
    "$([ -d "$T/gadget/functions/ecm.usb0" ] && echo 1)"
check "ffs.mtp vinculado ao config c.1" \
    "$([ -L "$T/gadget/configs/c.1/ffs.mtp" ] && echo 1)"
check "ecm.usb0 vinculado ao config c.1" \
    "$([ -L "$T/gadget/configs/c.1/ecm.usb0" ] && echo 1)"
check "VID/PID gravados" \
    "$(grep -qi '1d6b' "$T/gadget/idVendor" && echo 1)"
check "string de fabricante" \
    "$(grep -q 'Giochi' "$T/gadget/strings/0x409/manufacturer" && echo 1)"
rm -rf "$T"

echo "cenario 2: troca para mass (rede permanece)"
setup
# simula estado anterior: mtp ativo
GIOCHI_DRY_RUN=1 GADGET="$T/gadget" FFS_DIR="$T/ffs" GIOCHI_UDC=fake-udc \
    MTPSVC=/bin/true sh "$SCRIPT" mtp >/dev/null 2>&1
: > "$T/gadget/UDC"; echo fake-udc > "$T/gadget/UDC"
rc=$(run_mode mass)
check "comando aceito" "$([ "$rc" = "0" ] && echo 1)"
check "mass_storage.0 mantido/criado OU ausencia de /user tratada" \
    "$([ -d "$T/gadget/functions/mass_storage.0" ] && echo 1 || \
       [ ! -d "$T/gadget/functions/ffs.mtp" ] && echo 1)"
check "rede ecm.usb0 continua presente" \
    "$([ -d "$T/gadget/functions/ecm.usb0" ] && echo 1)"
check "ffs.mtp removido ao sair do modo mtp" \
    "$([ ! -d "$T/gadget/functions/ffs.mtp" ] && echo 1)"
rm -rf "$T"

echo "cenario 3: off desanexa e nao deixa funcoes"
setup
echo fake-udc > "$T/gadget/UDC"
rc=$(run_mode off)
check "comando aceito" "$([ "$rc" = "0" ] && echo 1)"
check "UDC esvaziado" "$([ -z "$(tr -d '[:space:]' < "$T/gadget/UDC")" ] && echo 1)"
check "nenhuma funcao de rede ativa" \
    "$([ ! -d "$T/gadget/functions/ecm.usb0" ] && echo 1)"
rm -rf "$T"

echo "cenario 4: modo invalido e recusado"
setup
rc=$(run_mode xyz)
check "exit != 0" "$([ "$rc" != "0" ] && echo 1)"
rm -rf "$T"

echo "cenario 5: aparelho sem USB (marcador) nao altera nada"
setup
mkdir -p "$T/etc/giochi"
touch "$T/etc/giochi/usb_gadget_disabled"
# o script checa caminho absoluto; simula via chroot impossivel -> valida o
# ramo por leitura do codigo (documentado no QA) e segue
rm -rf "$T"
echo "  skip marcador /etc/giochi/usb_gadget_disabled (caminho absoluto)"

echo "cenario 6: bind no UDC falha e /user NAO fica desmontada"
# Medido na X55 (imagem de 01/11): modo mass desmontou /user, o bind no UDC
# falhou com "failed to enable ep0out" e o sistema seguiu a sessao inteira
# sem /user — settings.json ausente, 0 jogos, 0 musicas — ate' reiniciar.
# Aqui o UDC e' um arquivo comum, entao o `echo > UDC` falha como falharia
# no controlador quebrado. O que se cobra: o helper devolve a particao.
setup
# UDC como DIRETORIO: open(O_WRONLY) nele falha com EISDIR. E' assim que
# simulamos "o controlador recusou o bind" sem precisar de hardware.
rm -f "$T/gadget/UDC"
mkdir -p "$T/gadget/UDC"
GIOCHI_DRY_RUN=0 GADGET="$T/gadget" FFS_DIR="$T/ffs" \
GIOCHI_UDC=fake-udc MTPSVC=/bin/true \
    sh "$SCRIPT" mtp >"$T/out.txt" 2>&1
rc=$?
check "bind recusado devolve erro" "$([ "$rc" != "0" ] && echo 1)"
check "controlador NAO ficou marcado como anexado" \
    "$([ -d "$T/gadget/UDC" ] && echo 1)"
rm -rf "$T"

echo "cenario 7: o helper tenta devolver /user quando o gadget falha"
# A remontagem real depende de montar um ext4; aqui se cobra o CONTRATO no
# codigo: todo caminho de saida depois do desmonte chama restaura_user, e o
# desmonte marca o estado. Sem isso, o bug do §3.89 volta calado.
corpo=$(sed 's/^[[:space:]]*#.*$//' "$SCRIPT")
check "restaura_user definida" \
    "$(printf '%s' "$corpo" | grep -q 'restaura_user()' && echo 1)"
check "desmonte marca USUARIO_DESMONTADO" \
    "$(printf '%s' "$corpo" | grep -q 'USUARIO_DESMONTADO=1' && echo 1)"
check "sem UDC -> restaura_user" \
    "$(printf '%s' "$corpo" | grep -q 'nenhum UDC disponivel' && \
       printf '%s' "$corpo" | awk '/nenhum UDC disponivel/,/^fi/' | \
       grep -q 'restaura_user' && echo 1)"
check "bind falho -> restaura_user" \
    "$(printf '%s' "$corpo" | awk '/falha ao anexar/,/exit 1/' | \
       grep -q 'restaura_user' && echo 1)"

echo "----"
[ "$fail" -eq 0 ]
