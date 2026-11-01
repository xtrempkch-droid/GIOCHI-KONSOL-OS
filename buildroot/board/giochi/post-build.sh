#!/bin/sh
# Giochi Konsol OS v1.2 — post-build.sh (S0).
# Copia tema/defaults + modulos .ko de dongle para /lib/modules (sem carregar)
# e instala os scripts de init. Nada aqui depende de um dongle presente.
set -eu

TARGET_DIR=${1:?uso: post-build.sh TARGET_DIR}
BOARD_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

REPO_DIR=$(CDPATH= cd -- "$BOARD_DIR/../../.." && pwd)
ASSETS_DIR="$REPO_DIR/assets"
DEVICE_DIR="$REPO_DIR/device"
SCRIPTS_DIR="$BOARD_DIR/../../../scripts/init.d"
HELPERS_DIR="$BOARD_DIR/../../../scripts"

# --- identificacao do aparelho ---------------------------------------
# GIOCHI_DEVICE vem do ambiente (tools/build-image.sh exporta). Sem ele o
# build direto por 'make' produz uma imagem generica, com aviso.
GIOCHI_DEVICE=${GIOCHI_DEVICE:-}
if [ -z "$GIOCHI_DEVICE" ]; then
    GIOCHI_DEVICE=rk3326-generic
    echo "post-build: AVISO: GIOCHI_DEVICE nao definido; usando $GIOCHI_DEVICE" >&2
fi
DEVICE_CONF="$REPO_DIR/device/$GIOCHI_DEVICE/device.conf"
if [ ! -f "$DEVICE_CONF" ]; then
    echo "post-build: ERRO: aparelho '$GIOCHI_DEVICE' sem $DEVICE_CONF" >&2
    exit 1
fi

# shellcheck disable=SC1090
. "$DEVICE_CONF"

dev_key() { sed -n "s/^GIOCHI_$1=//p" "$DEVICE_CONF" | head -n1 | tr -d '"'; }

# --- layouts/tema (S6/S7) ---------------------------------------------
install -d "$TARGET_DIR/usr/share/giochi"
# theme.json NAO entra mais: o tema vem de assets/themes/ (catalogo). O padrao
# do aparelho e' device/<id>/theme.json, instalado como device-theme.json.
for f in layout.json home.json library.json music.json \
         defaults.json buttons.default.json buttons.map; do
    [ -f "$ASSETS_DIR/$f" ] && install -m 0644 "$ASSETS_DIR/$f" \
        "$TARGET_DIR/usr/share/giochi/$f"
done

# theme.json avulso e' RESTO de imagem anterior: o tema agora vem do catalogo
# (assets/themes/). O TARGET_DIR sobrevive entre builds, entao um arquivo que
# deixou de ser instalado continua na arvore e vai parar no SquashFS — foi o que
# aconteceu aqui (medido: theme.json de 01/11 numa imagem de 01/11 03:58).
rm -f "$TARGET_DIR/usr/share/giochi/theme.json"

# Tema padrao do APARELHO: so' o "theme_id", instalado como device-theme.json.
# O catalogo completo vai por giochi-assets.mk; aqui e' a escolha de fabrica.
if [ -f "$DEVICE_DIR/$GIOCHI_DEVICE/theme.json" ]; then
    install -m 0644 "$DEVICE_DIR/$GIOCHI_DEVICE/theme.json" \
        "$TARGET_DIR/usr/share/giochi/device-theme.json"
    echo "post-build: tema padrao de $GIOCHI_DEVICE"
fi

# Layout especifico do aparelho (secao 4.1.3): a X55 tem painel 720x1280,
# nao 640x480. O override substitui apenas os arquivos que ele define.
if [ -d "$DEVICE_DIR/$GIOCHI_LAYOUT/layout" ]; then
    for f in "$DEVICE_DIR/$GIOCHI_LAYOUT/layout"/*.json; do
        [ -f "$f" ] || continue
        install -m 0644 "$f" "$TARGET_DIR/usr/share/giochi/$(basename "$f")"
        echo "post-build: layout $GIOCHI_LAYOUT/$(basename "$f")"
    done
fi

# --- scripts de init --------------------------------------------------
# Sem estes scripts a imagem nao inicia: falta de um deles e erro fatal, nao
# um skip silencioso (foi assim que um .gitignore errado passou despercebido).
#
# O TARGET_DIR sobrevive ao build incremental e NAO apaga nada quando um
# arquivo sai do repositorio: um script renomeado deixa o antigo no lugar, a
# imagem sai com os DOIS e o `rcS` roda o velho primeiro. Medido em
# 2026-11-01, ao mover S20graphics para S90graphics: o squashfs tinha
# S20graphics, S90graphics e S99launcher juntos — ou seja, a correcao do §3.86
# nao tinha efeito nenhum e nada avisava.
#
# Por isso o que instalamos fica registrado num manifesto FORA da imagem: na
# execucao seguinte, todo nome do manifesto que sumiu da lista e' apagado.
INIT_DIR="$TARGET_DIR/etc/init.d"
INI_MANIFEST="$(dirname "$TARGET_DIR")/.giochi-init-managed"
install -d "$INIT_DIR"

ini_list="S01relogio S00bootlog S05modules S05update S10storage S30swap
          S40dongle S50usb S55usbnet S62sshd S90graphics"

# Nomes que nunca chegaram a entrar no manifesto (limpeza unica):
#   S20graphics -> S90graphics  fazia `exec /usr/bin/giochi-init` no meio do
#     laco do rcS e matava o resto do boot (AGENTS.md §3.86).
#   S99launcher                 codigo morto; o supervisor lanca o Launcher.
aposentados="S20graphics S99launcher"
[ -f "$INI_MANIFEST" ] && aposentados="$aposentados $(cat "$INI_MANIFEST")"
for velho in $aposentados; do
    case " $(echo $ini_list) " in
        *" $velho "*) continue ;;
    esac
    if [ -f "$INIT_DIR/$velho" ]; then
        rm -f "$INIT_DIR/$velho"
        echo "post-build: removido /etc/init.d/$velho (fora da lista de init)"
    fi
done

missing_init=""
for s in $ini_list; do
    if [ -f "$SCRIPTS_DIR/$s" ]; then
        install -m 0755 "$SCRIPTS_DIR/$s" "$INIT_DIR/$s"
    else
        missing_init="$missing_init $s"
    fi
done
if [ -n "$missing_init" ]; then
    echo "post-build: ERRO scripts de init ausentes:$missing_init" >&2
    echo "post-build: verifique se scripts/init.d esta versionado" >&2
    exit 1
fi
( echo $ini_list | tr ' ' '\n' ) > "$INI_MANIFEST"

# --- scripts de init dos PACOTES que conflitam com os nossos --------------
# Cada pacote do Buildroot instala o proprio /etc/init.d/Sxx. Dois deles
# brigam com uma decisao deste projeto, e a briga e silenciosa:
#
#   S01syslogd / S02klogd (busybox.mk): sobem syslogd/klogd SEM -O, gravando
#     em /var/log/messages (tmpfs). O busybox syslogd desvincula /dev/log e
#     cria o proprio socket, entao o syslogd que o S00bootlog subiu perde o
#     socket e o log no cartao PARA DE CRESCER sem nenhum erro. O S00bootlog
#     e o dono de syslogd/klogd nesta imagem.
#
#   S50dropbear (dropbear.mk): sobe sshd incondicionalmente, em todas as
#     interfaces, sem checar o Modo Desenvolvedor. Isso anula o adendo 4.6.1
#     (SSH desligado por padrao e so em 10.55.0.2) — o S62sshd e quem decide.
#     Guarda: tools/check-image-contents.sh.
#
#   S10mdev (busybox.mk): o /dev desta imagem e devtmpfs (inittab) e os nos
#     que faltam vem do device_table.txt; nao existe modprobe no rootfs. O
#     script sobe um 'mdev -df' e depois roda 'xargs modprobe' — sem modprobe
#     ele sai != 0, e o rcS antigo ainda o chamava SEM argumento, imprimindo
#     "Usage: /etc/init.d/S10mdev {start|stop|restart}" no boot da X55. Quem
#     carrega modulo sob demanda e o giochi-dongle-hotplug.
for conflito in S01syslogd S02klogd S50dropbear S10mdev; do
    if [ -f "$TARGET_DIR/etc/init.d/$conflito" ]; then
        rm -f "$TARGET_DIR/etc/init.d/$conflito"
        echo "post-build: removido /etc/init.d/$conflito (conflita com o nosso)"
    fi
done

# --- helpers ----------------------------------------------------------
install -d "$TARGET_DIR/usr/sbin"
install -m 0755 "$BOARD_DIR/giochi-dongle-hotplug" \
    "$TARGET_DIR/usr/sbin/giochi-dongle-hotplug"
# Helper unico do gadget USB (usado por S50usb e por control.sock USB_MODE).
install -m 0755 "$HELPERS_DIR/giochi-usb-mode" \
    "$TARGET_DIR/usr/sbin/giochi-usb-mode"
install -m 0644 "$BOARD_DIR/mdev.conf" "$TARGET_DIR/etc/mdev.conf"

# Instalador do eMMC: roda NO CONSOLE (nao e ferramenta de PC). E o caminho
# para colocar o sistema no eMMC de um aparelho cujo bootloader de fabrica
# ganha do cartao (ver docs/BOOT-DEBUG.md 6). Instalado em /usr/sbin porque
# so root deve conseguir executa-lo.
install -m 0755 "$BOARD_DIR/../../../tools/install-image-to-emmc.sh" \
    "$TARGET_DIR/usr/sbin/install-image-to-emmc.sh"

# Mapa canonico de botoes (secao 2.2): /etc/giochi/buttons.map.
install -d "$TARGET_DIR/etc/giochi"
install -m 0644 "$ASSETS_DIR/buttons.map" "$TARGET_DIR/etc/giochi/buttons.map"

# --- diretorios rw da particao C (montada em runtime) -----------------
install -d "$TARGET_DIR/user/games" "$TARGET_DIR/user/music" \
           "$TARGET_DIR/user/ports" "$TARGET_DIR/user/.giochi" \
           "$TARGET_DIR/user/.cache/thumbs" "$TARGET_DIR/user/.cache/meta"

# --- modulos de dongle: ja vem de linux-modules; apenas garante preset ---
install -d "$TARGET_DIR/etc/modules-load.d"
cat > "$TARGET_DIR/etc/modules-load.d/giochi.conf" <<'CONF'
# Dongles NAO carregam no boot (ADR-14): mdev faz modprobe sob demanda.
CONF

# --- firmware de dongles (secao 1.4, <= 2 MB) -------------------------
install -d "$TARGET_DIR/lib/firmware"
if [ -d "$BOARD_DIR/firmware" ]; then
    cp -a "$BOARD_DIR/firmware/." "$TARGET_DIR/lib/firmware/" 2>/dev/null || true
fi

# --- Bluetooth do RTL8821CS (integrado na X55) ------------------------
# docs/hardware-e-drivers.md F5: o rtl8821cs_config.bin NAO existe upstream.
# Sem esta entrada de 0x0c (16 bytes) o hci0 fica com MAC zero.
install -d "$TARGET_DIR/lib/firmware/rtl_bt"
config_bin="$TARGET_DIR/lib/firmware/rtl_bt/rtl8821cs_config.bin"
# shellcheck disable=SC2059
printf '\125\253\043\207\023\000\014\000\020\002\200\222\004\120\305\352\031\341\033\375\257\137\001\244\013' \
    > "$config_bin"
echo "post-build: rtl8821cs_config.bin ($(wc -c <"$config_bin") bytes)"

# --- bootsplash (assets gerados por tools/make-bootlogo.py) ------------
# O splash do U-Boot vai para a particao A (ver post-image.sh). Aqui entra o
# que o Launcher desenha nos primeiros frames.
BOOTLOGO_DIR="$ASSETS_DIR/bootlogo"
if [ -f "$BOOTLOGO_DIR/bootlogo.rgba" ]; then
    install -d "$TARGET_DIR/usr/share/giochi"
    install -m 0644 "$BOOTLOGO_DIR/bootlogo.rgba" \
        "$TARGET_DIR/usr/share/giochi/bootlogo.rgba"
    [ -f "$BOOTLOGO_DIR/bootlogo.json" ] && \
        install -m 0644 "$BOOTLOGO_DIR/bootlogo.json" \
            "$TARGET_DIR/usr/share/giochi/bootlogo.json"
    echo "post-build: bootlogo.rgba instalado"
else
    echo "post-build: aviso: bootlogo.rgba ausente (rode 'make bootlogo')" >&2
fi

# --- rede USB / SSH (secao 4.6) ---------------------------------------
install -d "$TARGET_DIR/etc"
# Funcao de rede do gadget: ECM (Linux/macOS) ou RNDIS (Windows sem driver).
# Pode ser sobrescrita por aparelho no .conf (GIOCHI_USB_NET).
usb_net=$(dev_key USB_NET)
[ -n "${usb_net:-}" ] || usb_net=ecm
cat > "$TARGET_DIR/etc/giochi/usb-net.conf" <<CONF
# Gerado por post-build.sh — rede USB (adendo 4.6.1).
# Console 10.55.0.2/24; PC 10.55.0.1/24 (estatico, sem DHCP no console).
GIOCHI_USB_NET=$usb_net
GIOCHI_USB_IP=10.55.0.2
GIOCHI_USB_PREFIX=24
CONF
echo "post-build: rede USB = $usb_net (console 10.55.0.2/24, PC 10.55.0.1/24)"

# SSH (Modo Desenvolvedor, adendo 4.6.1): desativado por padrao.
install -d "$TARGET_DIR/user/ssh"
if [ ! -f "$TARGET_DIR/user/ssh/README" ]; then
    cat > "$TARGET_DIR/user/ssh/README" <<'DOC'
Modo Desenvolvedor (SSH via USB) — desativado por padrao.
Ative em Configuracoes -> Sistema -> Modo Desenvolvedor.

Depois de ativo:
  PC: 10.55.0.1/24 (estatico)   Console: 10.55.0.2/24
  ssh root@10.55.0.2

Autenticacao por chave: coloque aqui authorized_keys (formato OpenSSH,
uma chave por linha):

  ssh-keygen -t ed25519
  (cole ~/.ssh/id_ed25519.pub em /user/ssh/authorized_keys)

Autenticacao por senha: a imagem precisa ter /etc/giochi/root_password
(gerada com GIOCHI_ROOT_PASSWORD=<senha> no build) ou defina a senha de
root pelo console serial.
DOC
fi

# dropbear le a lista de chaves de /etc/dropbear/authorized_keys, mas a raiz
# e squashfs (somente leitura). Apontamos /etc/dropbear para a particao rw:
# assim o S62sshd instala /user/ssh/authorized_keys em tempo de execucao.
if [ -d "$TARGET_DIR/etc/dropbear" ] && [ ! -L "$TARGET_DIR/etc/dropbear" ]; then
    rm -rf "$TARGET_DIR/etc/dropbear"
fi
ln -sfn /user/.ssh/dropbear "$TARGET_DIR/etc/dropbear"

# Senha de root (adendo 4.6.1: autenticacao por senha OU chave).
# So e gravada quando o build pede explicitamente; sem ela o acesso e por
# chave. O Modo Desenvolvedor continua desligado por padrao de qualquer forma.
if [ -n "${GIOCHI_ROOT_PASSWORD:-}" ] || \
   [ "${GIOCHI_TEST_PASSWORD:-0}" = "1" ]; then
    test_pass=${GIOCHI_ROOT_PASSWORD:-giochi}
    echo "$test_pass" > "$TARGET_DIR/etc/giochi/root_password"
    chmod 0600 "$TARGET_DIR/etc/giochi/root_password"
    echo "post-build: ******************************************************" >&2
    echo "post-build: senha de root definida (root/$test_pass)" >&2
    echo "post-build: o SSH so sobe com o Modo Desenvolvedor ativo" >&2
    echo "post-build: ******************************************************" >&2
fi

# --- identidade do aparelho em runtime --------------------------------
# Lido pelo Launcher (buttons.map), por S90graphics (painel/rotacao) e por
# S50usb (suporte a gadget). Formato chave=valor, sem espacos.
install -d "$TARGET_DIR/etc/giochi"
cat > "$TARGET_DIR/etc/giochi/device.conf" <<CONF
# Gerado por post-build.sh — nao editar na imagem.
GIOCHI_DEVICE=${GIOCHI_DEVICE}
GIOCHI_NAME="$(dev_key NAME)"
GIOCHI_SOC=$(dev_key SOC)
GIOCHI_PANEL=$(dev_key PANEL)
GIOCHI_ROTATION=$(dev_key ROTATION)
GIOCHI_BUTTONS_MAP=$(dev_key BUTTONS_MAP)
GIOCHI_JOYPAD=$(dev_key JOYPAD)
GIOCHI_WIFI="$(dev_key WIFI)"
GIOCHI_BT="$(dev_key BT)"
GIOCHI_USB_GADGET=$(dev_key USB_GADGET)
GIOCHI_TESTED=$(dev_key TESTED)
GIOCHI_USB_NET=$usb_net
GIOCHI_TEST_IMAGE=${GIOCHI_TEST_IMAGE:-0}
CONF

# Marca imagem de teste gerada com DTS de fallback (nao suportada).
if [ "${GIOCHI_TEST_IMAGE:-0}" = "1" ]; then
    echo 1 > "$TARGET_DIR/etc/giochi/test_image"
    echo "post-build: *** IMAGEM DE TESTE (DTS de fallback) — nao suportada ***" >&2
fi

# --- componentes de kernel fora do mainline (hardware-e-drivers.md §9) --
joypad=$(dev_key JOYPAD)
if [ "$joypad" = "rocknix" ]; then
    if [ ! -d "$TARGET_DIR/lib/modules" ] || \
       ! find "$TARGET_DIR/lib/modules" -name 'rocknix-joypad*' 2>/dev/null | \
           grep -q .; then
        echo "post-build: AVISO: $GIOCHI_DEVICE usa rocknix-joypad, mas o" >&2
        echo "            modulo nao esta em /lib/modules (ver board/giochi/dts/README)" >&2
    fi
fi

usb_gadget=$(dev_key USB_GADGET)
if [ "$usb_gadget" = "no" ]; then
    echo "post-build: aviso: $GIOCHI_DEVICE nao ancora USB (F12); gadget desativado" >&2
    echo 1 > "$TARGET_DIR/etc/giochi/usb_gadget_disabled"
fi

# --- tamanho do rootfs: o que o Buildroot NAO remove -------------------
# O target-finalize do 2024.02 ja apaga usr/include, usr/share/{doc,man,info},
# pkgconfig, *.a/*.la/*.prl, e faz o strip dos binarios e dos .ko (via
# INSTALL_MOD_STRIP) — conferido no Makefile do Buildroot. Nao repita isso
# aqui. O que ele DEIXA e nao serve para nada neste console sao os catalogos
# gettext: 9,5 MB crus / 2,1 MB no SquashFS no build real da X55.
# A UI do Giochi nao usa gettext (as frases vivem nos assets e no locale.gd dos
# jogos); sem os .mo, e2fsprogs/util-linux/alsa caem no texto em ingles.
# GIOCHI_KEEP_LOCALE=1 mantem (util ao investigar traducao de ferramenta).
if [ "${GIOCHI_KEEP_LOCALE:-0}" != "1" ]; then
    for d in usr/share/locale usr/share/i18n usr/share/help usr/share/gtk-doc; do
        if [ -d "$TARGET_DIR/$d" ]; then
            kb=$(du -sk "$TARGET_DIR/$d" 2>/dev/null | cut -f1)
            rm -rf "$TARGET_DIR/$d"
            echo "post-build: removido /$d (${kb:-?} KB; nao usado pelo console)"
        fi
    done
fi

# --- chave publica de update (ADR-12) ---------------------------------
if [ -f "$BOARD_DIR/pubkey.ed25519" ]; then
    install -m 0644 "$BOARD_DIR/pubkey.ed25519" \
        "$TARGET_DIR/etc/giochi/pubkey.ed25519"
else
    echo "post-build: aviso: pubkey.ed25519 ausente (updates serao recusados)"
fi

# --- lib.sh comum -----------------------------------------------------
install -m 0644 "$BOARD_DIR/rootfs_overlay/etc/giochi/lib.sh" \
    "$TARGET_DIR/etc/giochi/lib.sh"

echo "post-build: Giochi Konsol OS v1.2 em $TARGET_DIR"
