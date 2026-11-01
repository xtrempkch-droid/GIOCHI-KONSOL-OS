#!/bin/sh
# Giochi Konsol OS v1.2 — executor dos testes de host.
set -u

BIN=${1:-build/host/bin}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
fail=0
total=0

export GIOCHI_LOG_LEVEL=error

for t in test_json test_sha256 test_hotkey test_musicd test_transition \
         test_layout test_metadata test_settings test_buttonsmap \
         test_deviceconf; do
    total=$((total + 1))
    if [ ! -x "$BIN/$t" ]; then
        echo "FALTA  $t"
        fail=$((fail + 1))
        continue
    fi
    if "$BIN/$t"; then
        echo "OK     $t"
    else
        echo "FALHA  $t"
        fail=$((fail + 1))
    fi
done

# Integracao: precisa de python3 para escrever na FIFO binaria.
if [ -x "$ROOT/tests/integration/lifecycle_test.sh" ] && \
   command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if "$ROOT/tests/integration/lifecycle_test.sh" "$BIN" >/dev/null 2>&1; then
        echo "OK     lifecycle (integracao)"
    else
        echo "FALHA  lifecycle (integracao)"
        fail=$((fail + 1))
    fi
fi

# Assets de bootsplash (contrato consumido pelo render).
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_bootlogo) >/dev/null 2>&1; then
        echo "OK     bootlogo (python)"
    else
        echo "FALHA  bootlogo (python)"
        fail=$((fail + 1))
    fi
fi

# Imagens de documentacao: contrato com os assets reais (tema/layout/splash).
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_screenshots) >/dev/null 2>&1; then
        echo "OK     screenshots (python)"
    else
        echo "FALHA  screenshots (python)"
        fail=$((fail + 1))
    fi
fi

# Jogos padrao: licenca, projeto, bench e documentacao (assets/default-games.json).
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_default_games) >/dev/null 2>&1; then
        echo "OK     jogos-padrao (python)"
    else
        echo "FALHA  jogos-padrao (python)"
        fail=$((fail + 1))
    fi
fi

# Catalogo de engines: os tres lugares que descrevem a capacidade do aparelho
# (devices/<id>.conf, a tabela por SoC do device_profile.c e os perfis manuais
# do forge) tem de concordar. Quando discordavam, o RK3326 aparecia como
# panfrost/GLES 3.2 e a Loja recomendava engine que nao roda nele.
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_engines) >/dev/null 2>&1; then
        echo "OK     engines (python)"
    else
        echo "FALHA  engines (python)"
        fail=$((fail + 1))
    fi
fi

# Catalogos de idioma: os dois tem de ter as MESMAS chaves, senao trocar de
# idioma deixa rotulo em pt num menu em ingles.
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if python3 "$ROOT/tests/test_locales.py" >/dev/null 2>&1; then
        echo "OK     idiomas (python)"
    else
        echo "FALHA  idiomas (python)"
        fail=$((fail + 1))
    fi
fi

# Atlas da fonte embutida: o arquivo e gerado, entao o tamanho em bytes, os
# tamanhos que a UI pede e a cobertura dos glifos tem de casar com o codigo.
# Quando saem de sincronia os rotulos somem e o sintoma se confunde com
# "o Launcher nao subiu".
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if python3 "$ROOT/tests/test_font.py" >/dev/null 2>&1; then
        echo "OK     fonte-embutida (python)"
    else
        echo "FALHA  fonte-embutida (python)"
        fail=$((fail + 1))
    fi
fi

# Layout do cartao (genimage.cfg): o u-boot tem de caber na regiao raw e as
# particoes nao podem se sobrepor. Sem isso o build morre no genimage depois de
# 1-3 h compilando (foi o que aconteceu com a regiao de 8 MiB).
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_image_layout) >/dev/null 2>&1; then
        echo "OK     imagem-layout (python)"
    else
        echo "FALHA  imagem-layout (python)"
        fail=$((fail + 1))
    fi
fi

# Log de boot e dispositivo de boot do cartao. Cada checagem corresponde a um
# defeito que deixa a tela preta sem pista: o cartao e mmc1 (mmc0 e o eMMC, que
# a X55 nao tem), o env do U-Boot tem de ficar no cartao, o /boot precisa
# existir na imagem (raiz e somente leitura) e o busybox precisa de
# FEATURE_MOUNT_LABEL para o LABEL= do fstab.
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_boot_log) >/dev/null 2>&1; then
        echo "OK     log-de-boot (python)"
    else
        echo "FALHA  log-de-boot (python)"
        fail=$((fail + 1))
    fi
fi

# Invariantes do primeiro boot sadio: rcS sem chamada dupla, S10mdev fora da
# imagem, arvore de /user criada e leitura de device.conf pelo tamanho real da
# chave. Todos vieram de um boot real da X55.
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_primeiro_boot) >/dev/null 2>&1; then
        echo "OK     primeiro-boot (python)"
    else
        echo "FALHA  primeiro-boot (python)"
        fail=$((fail + 1))
    fi
fi

# Verificador do cartao (tools/verify-card.py). Distingue "a gravacao nao
# terminou" de "o cartao tem setor ruim" -- medido num boot real que parou em
# "No working init found" com a imagem integra e o cartao devolvendo EIO.
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_verify_card) >/dev/null 2>&1; then
        echo "OK     verificador-de-cartao (python)"
    else
        echo "FALHA  verificador-de-cartao (python)"
        fail=$((fail + 1))
    fi
fi

# Diagnostico de USB (tools/giochi-usb-diag.sh): a ferramenta que responde
# "o gadget ancorou?" olhava so' o fim do giochi-boot.log, e num log real de
# 1141 linhas a sonda do S50usb fica centenas de linhas antes do fim.
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_usb_diag) >/dev/null 2>&1; then
        echo "OK     diagnostico-de-usb (python)"
    else
        echo "FALHA  diagnostico-de-usb (python)"
        fail=$((fail + 1))
    fi
fi

# Gravar o cartao (tools/flash-card.sh). Une dd + verificacao + Modo
# Desenvolvedor; cada recusa do script e' um jeito de perder o cartao ou o
# tempo de quem esta' com o console na mao.
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_flash_card) >/dev/null 2>&1; then
        echo "OK     gravacao-do-cartao (python)"
    else
        echo "FALHA  gravacao-do-cartao (python)"
        fail=$((fail + 1))
    fi
fi

# Painel do DTS escolhido por aparelho: uma imagem cujo DTS nao tem no de
# painel utilizavel nunca acende a tela, e o build termina com sucesso.
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_dts_panel) >/dev/null 2>&1; then
        echo "OK     painel-do-dts (python)"
    else
        echo "FALHA  painel-do-dts (python)"
        fail=$((fail + 1))
    fi
fi

# Simbolo pedido e descartado em silencio pelo Kconfig: foi assim que o
# SDL2_KMSDRM sumiu e o SDL2 saiu sem driver de video (AGENTS.md §3.55).
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_config_aplicada) >/dev/null 2>&1; then
        echo "OK     config-aplicada (python)"
    else
        echo "FALHA  config-aplicada (python)"
        fail=$((fail + 1))
    fi
fi

# Patches locais de kernel: um patch que nao entra no build nao avisa nada --
# a imagem sai com o defeito de volta. Foi assim que o bind do DSI (-22) ficou
# escondido atras de um build "verde" (AGENTS.md §3.48).
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_kernel_patch) >/dev/null 2>&1; then
        echo "OK     patch-de-kernel (python)"
    else
        echo "FALHA  patch-de-kernel (python)"
        fail=$((fail + 1))
    fi
fi

# Proposta de layout (docs/proposta/): renders nativos das 5 abas nos dois
# paineis (640x480 e 1280x720) + as assercoes geometricas do proprio gerador.
# Sem Pillow a suite se auto-pula (SkipTest), como as outras de imagem.
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_layout_proposal) >/dev/null 2>&1; then
        echo "OK     proposta-layout (python)"
    else
        echo "FALHA  proposta-layout (python)"
        fail=$((fail + 1))
    fi
fi

# Escala do override por aparelho: cada numero de device/x55/layout/*.json tem
# de ser round(base x 1,5), politica igual ao base, ou um dos especiais do
# painel. Foi a ausencia deste guarda que deixou 7 fatores implicitos (1,28 a
# 2,00) e 4,48 MB de thumbnail contra o teto de 4 MB. Sem Pillow: so stdlib.
if command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_device_scale) >/dev/null 2>&1; then
        echo "OK     escala-x55 (python)"
    else
        echo "FALHA  escala-x55 (python)"
        fail=$((fail + 1))
    fi
fi

# Gadget USB composto (rede + MTP) contra configfs falso.
if [ -x "$ROOT/tests/integration/usbgadget_test.sh" ]; then
    total=$((total + 1))
    if "$ROOT/tests/integration/usbgadget_test.sh" \
            "$ROOT/scripts/giochi-usb-mode" >/dev/null 2>&1; then
        echo "OK     usb-gadget (integracao)"
    else
        echo "FALHA  usb-gadget (integracao)"
        fail=$((fail + 1))
    fi
fi

# Modo Desenvolvedor / SSH via USB (adendo 4.6.1).
if [ -x "$ROOT/tests/integration/devmode_test.sh" ]; then
    total=$((total + 1))
    if "$ROOT/tests/integration/devmode_test.sh" \
            "$ROOT/scripts/init.d" "$BIN" >/dev/null 2>&1; then
        echo "OK     devmode (integracao)"
    else
        echo "FALHA  devmode (integracao)"
        fail=$((fail + 1))
    fi
fi

# O forge e' app de PC desde a reorganizacao: mora em apps/forge, nao em src/.
if [ -d "$ROOT/apps/forge" ] && command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if PYTHONPATH="$ROOT/apps/forge" python3 -m unittest discover \
            -s "$ROOT/tests/forge" >/dev/null 2>&1; then
        echo "OK     forge (python)"
    else
        echo "FALHA  forge (python)"
        fail=$((fail + 1))
    fi
fi

# Catalogo de temas: schema, contraste medido, paridade do indice e o efeito
# REAL no layout (o binario tem de mudar a grade, nao so' a cor).
if [ -x "$ROOT/build/host/bin/dump-geometry" ] && command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_themes) >/dev/null 2>&1; then
        echo "OK     temas (python)"
    else
        echo "FALHA  temas (python)"
        fail=$((fail + 1))
    fi
fi

# App de loja do PC: validacao do .giochi, news.json e copia conferida.
if [ -d "$ROOT/apps/store" ] && command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_store) >/dev/null 2>&1; then
        echo "OK     loja-do-pc (python)"
    else
        echo "FALHA  loja-do-pc (python)"
        fail=$((fail + 1))
    fi
fi

# Fonte GitHub Releases: le a lista do GithubLauncher, escolhe o build aarch64 e
# confere o ELF. Sem isso a Loja so' enxerga o que ja' esta' empacotado.
if [ -d "$ROOT/apps/store" ] && command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_fonte) >/dev/null 2>&1; then
        echo "OK     fonte-github (python)"
    else
        echo "FALHA  fonte-github (python)"
        fail=$((fail + 1))
    fi
fi

# Contrato do control.sock: o canal que um app de jogos usa. Roda o
# giochi-init REAL com GIOCHI_ROOT num temporario (sem root, sem aparelho).
if [ -x "$ROOT/build/host/bin/giochi-init" ] && command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_control_protocol) >/dev/null 2>&1; then
        echo "OK     protocolo-do-app (python)"
    else
        echo "FALHA  protocolo-do-app (python)"
        fail=$((fail + 1))
    fi
fi

# Cliente do control.sock por SSH: o binario que a gente usa para falar com o
# console. Roda contra o giochi-init real, com um `ssh` falso no PATH.
if [ -x "$ROOT/tools/giochi-ctl.py" ] && command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_giochi_ctl) >/dev/null 2>&1; then
        echo "OK     cliente-ssh (python)"
    else
        echo "FALHA  cliente-ssh (python)"
        fail=$((fail + 1))
    fi
fi

# Modo Desenvolvedor pelo lado do PC: o script que abre o SSH no cartao, e a
# prova de que o S62sshd real sobe com o que ele escreveu.
if [ -x "$ROOT/tools/giochi-dev-mode.sh" ] && command -v python3 >/dev/null 2>&1; then
    total=$((total + 1))
    if (cd "$ROOT" && python3 -m unittest tests.test_dev_mode_card) >/dev/null 2>&1; then
        echo "OK     modo-dev-no-cartao (python)"
    else
        echo "FALHA  modo-dev-no-cartao (python)"
        fail=$((fail + 1))
    fi
fi

echo "----"
echo "$((total - fail))/$total suites OK"
[ "$fail" -eq 0 ]
