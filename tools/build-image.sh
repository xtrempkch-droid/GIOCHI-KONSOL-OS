#!/bin/sh
# Giochi Konsol OS v1.2 — build de imagem SD por aparelho.
#
# Uso:
#   tools/build-image.sh --list
#   tools/build-image.sh x55
#   tools/build-image.sh x55 x35s --jobs 8
#   tools/build-image.sh --all --out dist
#   tools/build-image.sh --check            # valida sem compilar
#
# Variaveis uteis:
#   BUILDROOT_DIR=/caminho/buildroot   (obrigatorio para compilar; --check nao)
#   GIOCHI_DTS_PATH=/caminho/placa.dts (aparelhos sem DTS upstream)
#   GIOCHI_DTB_PATH=/caminho/placa.dtb (DTB ja compilado, alternativa ao .dts)
#   GIOCHI_JOBS=N                      (default: nproc)
#   GIOCHI_UBOOT_DIR=/caminho/uboot    tree do U-Boot do vendor (quando o
#                                      mainline nao tem defconfig)
#   GIOCHI_UBOOT_BIN=/caminho/bin      bootloader ja compilado (idbloader/u-boot.itb)
#
# Aparelhos sem DTS upstream (X35S/X35H) exigem o DTS do fabricante. Para
# smoke-test sem ele, use --fallback-dts: a imagem eh marcada como de teste e
# provavelmente NAO inicia video/controles corretamente.
#   GIOCHI_OUT=dist                    (default: dist)
#   GIOCHI_DEBUG_BOOT=1                imagem de diagnostico: liga o fbcon no
#                                      kernel (mensagens de boot na TELA). O
#                                      artefato vira <aparelho>/giochi-
#                                      <aparelho>-sdcard-debug.img; a arvore
#                                      de build e a mesma (incremental).
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DEV_DIR="$ROOT/buildroot/board/giochi/devices"
OUT=${GIOCHI_OUT:-$ROOT/dist}
JOBS=${GIOCHI_JOBS:-$( (nproc 2>/dev/null || echo 4) )}
CHECK=0
LIST=0
ALL=0
FALLBACK=0
VENDOR_UBOOT=0
DEBUG_BOOT=${GIOCHI_DEBUG_BOOT:-0}
DEVICES=""

usage() {
    cat <<EOF
uso: tools/build-image.sh [opcoes] [aparelho...]

  --all            todos os aparelhos de devices/*.conf
  --list           lista os aparelhos e sai
  --check          valida configs/insumos sem compilar
  --out DIR        diretorio de saida (default: $OUT)
  --jobs N         paralelismo do make
  --fallback-dts   permite usar o DTS de fallback (imagem de TESTE)
  --vendor-uboot   exige U-Boot do vendor (GIOCHI_UBOOT_DIR/BIN)
  --debug          imagem de diagnostico: fbcon no kernel (mensagens de boot
                   na tela); o artefato sai como ...-sdcard-debug.img
  -h, --help       esta ajuda

Aparelhos disponiveis: $(device_ids | tr '\n' ' ')
EOF
}

device_ids() {
    for conf in "$DEV_DIR"/*.conf; do
        [ -f "$conf" ] || continue
        sed -n 's/^GIOCHI_ID=//p' "$conf" | head -n1
    done | sort
}

conf_of() {
    id=$1
    for conf in "$DEV_DIR"/*.conf; do
        [ -f "$conf" ] || continue
        [ "$(sed -n 's/^GIOCHI_ID=//p' "$conf" | head -n1)" = "$id" ] && {
            echo "$conf"
            return 0
        }
    done
    return 1
}

while [ $# -gt 0 ]; do
    case "$1" in
        --all) ALL=1 ;;
        --list) LIST=1 ;;
        --check) CHECK=1 ;;
        --fallback-dts) FALLBACK=1 ;;
        --vendor-uboot) VENDOR_UBOOT=1 ;;
        --debug) DEBUG_BOOT=1 ;;
        --out) shift; OUT=${1:?--out exige um diretorio} ;;
        --jobs) shift; JOBS=${1:?--jobs exige um numero} ;;
        -h|--help) usage; exit 0 ;;
        -*) echo "build-image: opcao desconhecida '$1'" >&2; usage >&2; exit 2 ;;
        *) DEVICES="$DEVICES $1" ;;
    esac
    shift
done

[ "$LIST" = "1" ] && { device_ids; exit 0; }
[ "$ALL" = "1" ] && DEVICES=$(device_ids)
[ -n "$DEVICES" ] || { usage >&2; exit 2; }

fail=0
summary=""

check_device() {
    id=$1
    conf=$(conf_of "$id") || {
        echo "  ERRO aparelho desconhecido: $id" >&2
        return 1
    }

    # shellcheck disable=SC1090
    . "$conf"

    echo "== $id ($GIOCHI_NAME, $GIOCHI_SOC) =="

    defconfig="$ROOT/buildroot/configs/$GIOCHI_DEFCONFIG"
    [ -f "$defconfig" ] || {
        echo "  ERRO defconfig ausente: buildroot/configs/$GIOCHI_DEFCONFIG" >&2
        echo "       rode 'make defconfigs'" >&2
        return 1
    }

    # O defconfig gerado precisa citar o aparelho e o SoC corretos.
    grep -qx "BR2_PACKAGE_GIOCHI_INIT_DEVICE=\"$id\"" "$defconfig" || {
        echo "  ERRO defconfig sem BR2_PACKAGE_GIOCHI_INIT_DEVICE=\"$id\"" >&2
        return 1
    }
    grep -qx "BR2_PACKAGE_GIOCHI_INIT_SOC=\"$GIOCHI_SOC\"" "$defconfig" || {
        echo "  ERRO defconfig sem o SoC $GIOCHI_SOC" >&2
        return 1
    }

    # Layout do aparelho (quando existe override).
    if [ "$GIOCHI_LAYOUT" != "default" ]; then
        [ -d "$ROOT/assets/devices/$GIOCHI_LAYOUT" ] || {
            echo "  ERRO layout ausente: assets/devices/$GIOCHI_LAYOUT" >&2
            return 1
        }
    fi

    # DTS: upstream (Buildroot busca no kernel) ou fornecido pelo usuario.
    if [ "$GIOCHI_DTS_UPSTREAM" = "yes" ]; then
        echo "  dts: $GIOCHI_DTS (upstream)"
    else
        dts=${GIOCHI_DTS_PATH:-$ROOT/buildroot/board/giochi/dts/$GIOCHI_DTS.dts}
        dtb=${GIOCHI_DTB_PATH:-}
        if [ -n "$dtb" ] && [ -f "$dtb" ]; then
            echo "  dts: DTB externo $dtb"
        elif [ -f "$dts" ]; then
            echo "  dts: $dts (externo)"
        elif [ "$FALLBACK" = "1" ] && [ -n "${GIOCHI_DTS_FALLBACK:-}" ]; then
            echo "  AVISO dts: usando FALLBACK '$GIOCHI_DTS_FALLBACK' (IMAGEM DE TESTE)"
            echo "        painel/controles deste aparelho podem nao funcionar" >&2
        else
            echo "  FALTA dts: $dts" >&2
            echo "        baixe/copie o DTS do aparelho ou use GIOCHI_DTB_PATH" >&2
            if [ -n "${GIOCHI_DTS_FALLBACK:-}" ]; then
                echo "        ou rode com --fallback-dts para uma imagem de TESTE" >&2
            fi
            echo "        (ver docs/DEVICES.md e board/giochi/dts/README)" >&2
            return 1
        fi
    fi

    # Componente de kernel fora do mainline.
    if [ "$GIOCHI_JOYPAD" = "rocknix" ]; then
        if [ -d "$ROOT/buildroot/board/giochi/kernel-patches" ]; then
            echo "  joypad: rocknix (patches presentes)"
        else
            echo "  AVISO joypad: rocknix-joypad nao empacotado (gamepad ausente)"
        fi
    fi

    echo "  painel: $GIOCHI_PANEL rotacao=$GIOCHI_ROTATION botoes=$GIOCHI_BUTTONS_MAP"
    echo "  usb gadget: $GIOCHI_USB_GADGET | testado: $GIOCHI_TESTED"

    ub_ok=1
    if [ "${GIOCHI_UBOOT_UPSTREAM:-no}" = "yes" ]; then
        echo "  u-boot: $GIOCHI_UBOOT_DEFCONFIG (mainline)"
    elif [ -n "${GIOCHI_UBOOT_BIN:-}" ] && [ -f "${GIOCHI_UBOOT_BIN}" ]; then
        echo "  u-boot: binario do vendor $GIOCHI_UBOOT_BIN"
    elif [ -n "${GIOCHI_UBOOT_DIR:-}" ] && [ -d "${GIOCHI_UBOOT_DIR}" ]; then
        echo "  u-boot: tree do vendor $GIOCHI_UBOOT_DIR"
    else
        ub_ok=0
        if [ "$VENDOR_UBOOT" = "1" ]; then
            echo "  FALTA u-boot: defina GIOCHI_UBOOT_DIR ou GIOCHI_UBOOT_BIN" >&2
        else
            echo "  AVISO u-boot: mainline sem defconfig; a imagem sai SEM"
            echo "        bootloader. Grave o do vendor antes (docs/DEVICES.md)."
        fi
    fi

    if [ "$VENDOR_UBOOT" = "1" ] && [ "$ub_ok" = "0" ]; then
        return 1
    fi
    echo "  ok"
    return 0
}

build_device() {
    id=$1
    conf=$(conf_of "$id")
    # shellcheck disable=SC1090
    . "$conf"

    buildroot=${BUILDROOT_DIR:-}
    if [ -z "$buildroot" ] || [ ! -d "$buildroot" ]; then
        echo "ERRO BUILDROOT_DIR nao definido ou invalido: '${buildroot:-}'" >&2
        echo "     baixe o Buildroot (LTS) e exporte BUILDROOT_DIR=/caminho/buildroot" >&2
        return 1
    fi
    [ -x "$buildroot/Makefile" ] || [ -f "$buildroot/Makefile" ] || {
        echo "ERRO $buildroot nao parece um Buildroot (sem Makefile)" >&2
        return 1
    }

    # A arvore de build e SEMPRE a mesma: criar dist/<id>-debug obrigaria a
    # recompilar toolchain + kernel + tudo so para trocar um fragmento de
    # kernel. O que muda e o NOME do artefato, para nao gravar o cartao com a
    # imagem errada (a de diagnostico tem fbcon e kernel maior).
    if [ "$DEBUG_BOOT" = "1" ]; then
        img_suffix="-debug"
    else
        img_suffix=""
    fi
    out="$OUT/$id"
    img_name="giochi-$id-sdcard$img_suffix.img"
    mkdir -p "$out"
    echo "== compilando $id -> $out =="

    # Um build completo (toolchain + kernel + U-Boot + openssl + mesa3d) passa
    # de 15 GB. Sem espaco o make morre NO MEIO, sem mensagem: o log termina numa
    # linha de compilacao qualquer. Melhor falhar agora, dizendo quanto falta.
    need_gb=${GIOCHI_MIN_FREE_GB:-15}
    free_gb=$(df -Pk "$out" 2>/dev/null | awk 'NR==2 {printf "%d", $4/1024/1024}')
    if [ -n "$free_gb" ] && [ "$free_gb" -lt "$need_gb" ]; then
        echo "ERRO $id: espaco insuficiente em $out" >&2
        echo "     livre: ${free_gb} GB | necessario: ~${need_gb} GB" >&2
        echo "     libere espaco ou rode com GIOCHI_MIN_FREE_GB=<n> para ignorar" >&2
        df -h "$out" >&2 || true
        return 1
    fi
    echo "  espaco livre: ${free_gb:-?} GB (minimo ${need_gb} GB)"

    # --- snapshot das fontes dos pacotes giochi-* --------------------------
    # Os pacotes usam SITE_METHOD=local apontando para a RAIZ do repositorio, e
    # o Buildroot sincroniza isso com rsync. Como dist/ fica dentro da raiz, o
    # rsync copiava o proprio build em andamento (14 GB) para dentro de si
    # mesmo, recursivamente -- o build travava e o disco acabava sem nenhuma
    # mensagem util. Aqui montamos um diretorio so com o que e fonte (~9 MB) e
    # os .mk o usam via GIOCHI_PKGSRC.
    pkg_src="$out/pkg-src"
    rm -rf "$pkg_src"
    mkdir -p "$pkg_src"
    for item in Makefile src tools assets schemas scripts Jogos; do
        [ -e "$ROOT/$item" ] || continue
        cp -a "$ROOT/$item" "$pkg_src/" 2>/dev/null || {
            echo "ERRO $id: falha ao copiar $item para o snapshot" >&2
            return 1
        }
    done
    GIOCHI_PKGSRC="$pkg_src"
    export GIOCHI_PKGSRC
    echo "  fontes: snapshot em $pkg_src ($(du -sh "$pkg_src" 2>/dev/null | cut -f1))"

    # Kconfig descarta simbolos inexistentes em silencio e aborta em
    # BR2_LEGACY sem apontar o culpado. Falhar aqui evita descobrir isso
    # depois de horas de build (ver tools/check-buildroot-symbols.py).
    if [ -x "$ROOT/tools/check-buildroot-symbols.py" ]; then
        "$ROOT/tools/check-buildroot-symbols.py" --buildroot "$buildroot" || {
            echo "ERRO $id: simbolos invalidos no defconfig (acima)" >&2
            return 1
        }
    fi

    # Defconfig efetivo = gerado + overrides dinamicos (DTS externo).
    effective="$out/defconfig"
    cp "$ROOT/buildroot/configs/$GIOCHI_DEFCONFIG" "$effective"

    if [ "$GIOCHI_DTS_UPSTREAM" != "yes" ]; then
        dtb=${GIOCHI_DTB_PATH:-}
        dts=${GIOCHI_DTS_PATH:-$ROOT/buildroot/board/giochi/dts/$GIOCHI_DTS.dts}

        if [ -n "$dtb" ] && [ -f "$dtb" ]; then
            echo "  dtb pre-compilado: $dtb"
            GIOCHI_DTB_FILE_OVERRIDE=$(basename "$dtb")
            export GIOCHI_DTB_FILE_OVERRIDE
            GIOCHI_PRECOMPILED_DTB=$dtb
            export GIOCHI_PRECOMPILED_DTB
        elif [ -f "$dts" ]; then
            echo "BR2_LINUX_KERNEL_CUSTOM_DTS_PATH=\"$dts\"" >> "$effective"
            echo "  dts externo: $dts"
        elif [ "$FALLBACK" = "1" ] && [ -n "${GIOCHI_DTS_FALLBACK:-}" ]; then
            echo "BR2_LINUX_KERNEL_INTREE_DTS_NAME=\"rockchip/$GIOCHI_DTS_FALLBACK\"" \
                >> "$effective"
            GIOCHI_DTB_FILE_OVERRIDE="$GIOCHI_DTS_FALLBACK.dtb"
            export GIOCHI_DTB_FILE_OVERRIDE
            GIOCHI_TEST_IMAGE=1
            export GIOCHI_TEST_IMAGE
            echo "  FALLBACK: $GIOCHI_DTS_FALLBACK (imagem de TESTE)" >&2
            # O fallback NAO e o painel do aparelho. Se o DTS de fallback nao
            # tiver no de painel utilizavel, a tela nunca acende -- e o build
            # passava sem dizer nada disto (medido: 1 ciclo de flash perdido).
            # --buscar resolve o DTS nas arvores de kernel ja extraidas em
            # dist/ (o checker sabe onde procurar; nao repetimos o caminho).
            if [ -x "$ROOT/tools/check-dts-panel.py" ]; then
                _fb_tmp=${TMPDIR:-/tmp}/.giochi_fb.$$
                if ! "$ROOT/tools/check-dts-panel.py" \
                        --buscar "$GIOCHI_DTS_FALLBACK" --strict \
                        >"$_fb_tmp" 2>&1; then
                    echo "  AVISO: o DTS de fallback nao tem painel utilizavel:" >&2
                    sed 's/^/         /' "$_fb_tmp" >&2
                    echo "         a TELA nao vai acender; o kernel e o LOG NO" >&2
                    echo "         CARTAO (docs/BOOT-DEBUG.md) continuam validos" >&2
                fi
                rm -f "$_fb_tmp"
            fi
        else
            echo "ERRO DTS ausente: $dts" >&2
            return 1
        fi
    fi

    # Modo diagnostico: acrescenta o fragmento de fbcon a lista de fragmentos
    # JA declarada no defconfig. A lista e derivada, nao repetida, para nao
    # virar um segundo lugar onde os mesmos nomes de arquivo vivem (AGENTS 3.20).
    if [ "$DEBUG_BOOT" = "1" ]; then
        dbg_rel='$(BR2_EXTERNAL_GIOCHI_PATH)/board/giochi/linux-debug.fragment'
        dbg_file="$ROOT/buildroot/board/giochi/linux-debug.fragment"
        if [ ! -f "$dbg_file" ]; then
            echo "ERRO $id: $dbg_file ausente" >&2
            return 1
        fi
        sed -i "s|^\(BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES=\"[^\"]*\)\"$|\1 $dbg_rel\"|" \
            "$effective"
        if ! grep -q "linux-debug.fragment" "$effective"; then
            echo "ERRO $id: nao consegui anexar linux-debug.fragment" >&2
            echo "     o defconfig nao tem BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES?" >&2
            return 1
        fi
        echo "  MODO DIAGNOSTICO: fbcon ligado (mensagens do kernel na tela)"
    fi

    export GIOCHI_DEVICE="$id"

    # U-Boot: mainline (defconfig) ou binario do vendor.
    if [ "${GIOCHI_UBOOT_UPSTREAM:-no}" != "yes" ] && \
       [ -n "${GIOCHI_UBOOT_BIN:-}" ] && [ -f "${GIOCHI_UBOOT_BIN}" ]; then
        GIOCHI_UBOOT_BIN_EXPORT=$GIOCHI_UBOOT_BIN
        export GIOCHI_UBOOT_BIN_EXPORT
        echo "  bootloader: $GIOCHI_UBOOT_BIN (vendor)"
    fi

    TARGET_MAKE_ENV="GIOCHI_DEVICE=$id"
    [ -n "${GIOCHI_UBOOT_BIN_EXPORT:-}" ] && \
        TARGET_MAKE_ENV="$TARGET_MAKE_ENV GIOCHI_UBOOT_BIN=$GIOCHI_UBOOT_BIN_EXPORT"
    export TARGET_MAKE_ENV

    make -C "$buildroot" O="$out/build" BR2_EXTERNAL="$ROOT/buildroot" \
        -j"$JOBS" defconfig BR2_DEFCONFIG="$effective" || {
        echo "ERRO $id: 'make defconfig' falhou" >&2
        return 1
    }

    # Pacote local do Buildroot NAO re-sincroniza sozinho.
    #
    # Medido neste projeto: depois de corrigir src/init/device_profile.c e
    # src/launcher/main.cpp, o build terminou "verde" e a imagem continuou com
    # os binarios ANTIGOS — o log do aparelho ainda dizia
    # "(device=rk3326 soc=rk3326)" e "buttons.map: secao [efault]". Causa: o
    # Buildroot cria build/giochi-*/  com .stamp_rsynced e so roda o rsync do
    # SITE_METHOD = local quando o pacote e' reconstruido, o que nao acontece
    # porque a versao do pacote (1.2) nao muda. Ou seja: codigo C/C++ novo
    # nunca chegava na imagem, sem nenhum aviso.
    #
    # Apagar os stamps forca rsync + build + install do pacote. Sao ~4 MB de
    # codigo; o custo e' de segundos contra um rebuild de kernel.
    for pkg in "$out"/build/build/giochi-*; do
        [ -d "$pkg" ] || continue
        rm -f "$pkg"/.stamp_*
    done

    TARGET_MAKE_ENV="GIOCHI_DEVICE=$id"
    [ -n "${GIOCHI_UBOOT_BIN_EXPORT:-}" ] && \
        TARGET_MAKE_ENV="$TARGET_MAKE_ENV GIOCHI_UBOOT_BIN=$GIOCHI_UBOOT_BIN_EXPORT"
    export TARGET_MAKE_ENV

    # ATENCAO ao 'if ! build_device': quando a funcao e' chamada num teste, o
    # 'set -e' do shell fica DESLIGADO dentro dela. Sem o '||' abaixo, um
    # 'make' que falha (ex.: patch de kernel duplicado) nao interrompe nada, o
    # script segue, encontra o sdcard.img VELHO em images/ e imprime
    # "imagens geradas" com rc=0. Aconteceu aqui: a build falhou no kernel e a
    # imagem foi anunciada como pronta.
    make -C "$buildroot" O="$out/build" BR2_EXTERNAL="$ROOT/buildroot" \
        -j"$JOBS" || {
        echo "ERRO $id: build falhou (veja o log acima)" >&2
        return 1
    }

    images="$out/build/images"
    if [ -f "$images/sdcard.img" ]; then
        cp "$images/sdcard.img" "$out/$img_name"
        summary="$summary
  $id: $out/$img_name ($(du -h "$out/$img_name" | cut -f1))"
        # O artefato de distribuicao e o comprimido (~40 MB contra ~900 MB):
        # o .img cru e quase todo zero e nao deve ser publicado.
        for ext in xz zst gz; do
            [ -f "$images/sdcard.img.$ext" ] || continue
            cp "$images/sdcard.img.$ext" "$out/$img_name.$ext"
            ( cd "$out" && sha256sum "$img_name.$ext" \
                  > "$img_name.$ext.sha256" )
            summary="$summary
      -> $img_name.$ext ($(du -h \
            "$out/$img_name.$ext" | cut -f1))"
            break
        done
    else
        echo "ERRO $id: sdcard.img nao gerado em $images" >&2
        return 1
    fi
}

for id in $DEVICES; do
    if ! check_device "$id"; then
        fail=$((fail + 1))
        continue
    fi
    [ "$CHECK" = "1" ] && continue
    if ! build_device "$id"; then
        fail=$((fail + 1))
    fi
done

echo "----"
if [ "$CHECK" = "1" ]; then
    [ "$fail" -eq 0 ] && echo "preflight: todos os aparelhos prontos" || \
        echo "preflight: $fail aparelho(s) com pendencia"
else
    [ -n "$summary" ] && { echo "imagens geradas:$summary"; }
    [ "$fail" -eq 0 ] || echo "build-image: $fail aparelho(s) falharam"
fi
[ "$fail" -eq 0 ]
