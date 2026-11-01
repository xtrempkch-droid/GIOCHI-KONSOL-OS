# Giochi Konsol OS v1.2 — boot.cmd (S2). Fonte de boot.scr (mkimage -T script).
#
# Variaveis de ambiente (particao A, FAT):
#   giochi_slot       -> 2 (B1) | 3 (B2), slot ativo
#   giochi_prev_slot  -> slot anterior para rollback
# Regra: bootcount > 3 => rollback para giochi_prev_slot e zera bootcount.

setenv bootargs_base "console=ttyS2,1500000n8 earlycon=uart8250,mmio32,0xfe660000 rootfstype=squashfs ro rootwait"

# QUEM CARREGOU ESTE SCRIPT? O nosso U-Boot publica 'giochi_mmc' no
# BOOTCOMMAND, ANTES do 'source'. Um U-Boot de fabrica (ROCKNIX/JELOS, que
# apenas varre o cartao atras de boot.scr) nao define essa variavel.
#
# Isso importa porque 'saveenv' grava onde o U-Boot em execucao guarda o
# ambiente -- e, num aparelho de fabrica, esse lugar e o eMMC do usuario. Um
# boot.scr nosso escrevendo no eMMC de terceiro e efeito colateral inaceitavel:
# o A/B por bootcount e uma decisao da NOSSA imagem, nao do aparelho alheio.
# Sem 'giochi_mmc', rodamos sem estado (nada e gravado).
if test -z "${giochi_mmc}"; then
  setenv giochi_env_gravavel 0
else
  setenv giochi_env_gravavel 1
fi

if test -z "${giochi_slot}"; then
  setenv giochi_slot 2
  setenv giochi_prev_slot 3
  if test "${giochi_env_gravavel}" = "1"; then
    saveenv
  else
    echo "GIOCHI: bootloader de fabrica; sem saveenv (nao grava no eMMC)"
  fi
fi

if test "${bootcount}" -gt 3; then
  echo "GIOCHI: rollback apos ${bootcount} falhas de boot"
  setenv tmp "${giochi_slot}"
  setenv giochi_slot "${giochi_prev_slot}"
  setenv giochi_prev_slot "${tmp}"
  setenv bootcount 0
  if test "${giochi_env_gravavel}" = "1"; then
    saveenv
  fi
fi

# Layout (secao 1.3): p1 = boot (FAT), p2 = B1, p3 = B2, p4 = /user.
# Nos aparelhos de referencia o cartao SD e o mmc 1 do U-Boot
# (docs/hardware-e-drivers.md §2). giochi_mmc permite sobrescrever.
if test -z "${giochi_mmc}"; then
  setenv giochi_mmc 1
fi

# RAIZ POR PARTUUID, NAO POR NOME DE DISPOSITIVO.
#
# /dev/mmcblk1p2 vale para o cartao (mmc1) e so para ele: a MESMA imagem
# gravada no eMMC vira mmcblk0p2, e no segundo slot de cartao, mmcblk2p2.
# Nessas duas situacoes o kernel nao encontra a raiz ("Waiting for root
# device") mesmo com tudo certo. O PARTUUID resolve por identidade do disco:
# a assinatura MBR e FIXA (genimage.cfg: disk-signature = 0x47494f43) e o
# numero da particao escolhe o slot -- 02 = A, 03 = B.
#
# O kernel resolve PARTUUID sozinho (init/do_mounts.c); nao precisa de
# initramfs. Formato em MBR: <assinatura 8 hex>-<particao 2 hex>.
setenv giochi_root "PARTUUID=47494f43-02"
if test "${giochi_slot}" = "3"; then
  setenv giochi_root "PARTUUID=47494f43-03"
fi

setenv bootargs "${bootargs_base} root=${giochi_root} giochi.slot=${giochi_slot} giochi.bootcount=${bootcount}"

# Rotacao do console de video do kernel.
#
# O painel da X55 e' 720x1280 em retrato: sem fbcon=rotate, qualquer texto na
# tela sai deitado — inclusive a mensagem de falha que o Launcher escreve no
# /dev/tty1 quando nao consegue abrir o video.
#
# O valor NAO e' escolhido a mao. O marcador abaixo e' substituido pelo
# post-image.sh a partir de devices/<id>.conf (GIOCHI_ROTATION dividido por 90),
# que e' a MESMA fonte que o Launcher usa para girar a UI (ui_present). Uma
# fonte, uma derivacao: com duas rotacoes independentes, elas concordarem era
# sorte — foi o relato "o terminal tambem girou".
#
# Continua sobreponivel para o campo (o U-Boot tem prioridade sobre o padrao):
#   => setenv giochi_fbcon 1 ; saveenv     (forca um valor)
#   => setenv giochi_fbcon ; saveenv        (volta ao valor derivado)
if test -z "${giochi_fbcon}"; then
  setenv giochi_fbcon @FBCON_ROTATE@
fi
if test -n "${giochi_fbcon}" && test "${giochi_fbcon}" != "0"; then
  setenv bootargs "${bootargs} fbcon=rotate:${giochi_fbcon}"
fi

# Marcadores no console serial (ttyS2, 1500000 8N1). Um cabo USB-UART e a
# unica forma de ver o que acontece ANTES do kernel; com estes echo da para
# saber se o boot.scr rodou e com que dispositivo/raiz ele decidiu bootar.
echo "GIOCHI: boot.scr em execucao (mmc=${giochi_mmc} slot=${giochi_slot} root=${giochi_root})"

# DTB: tenta o nome que o U-Boot conhece (fdtfile), e se ESSE ARQUIVO NAO
# EXISTIR no cartao cai para board.dtb.
#
# A versao anterior testava apenas "fdtfile vazio" para decidir o fallback.
# Como fdtfile vem do ambiente do U-Boot (do bootloader de fabrica, quando ele
# e quem boota o cartao), ele quase nunca esta vazio — e o nosso cartao so tem o
# DTB do aparelho para o qual a imagem foi gerada. Resultado: num aparelho
# diferente (X35H com imagem de X55) o 'load dtbs/${fdtfile}' falhava, o
# fallback NAO acontecia porque fdtfile nao estava vazio, fdt_addr_r ficava com
# lixo e o booti falhava — o U-Boot seguia para o proximo alvo e bootava o eMMC.
# O sintoma para o usuario e "o sistema nao aparece" (aparece o de fabrica).
# 1) dtbs/<fdtfile>  (o U-Boot de fabrica usa "rockchip/<placa>.dtb")
# 2) dtbs/board.dtb   (o mesmo arquivo, com outro nome)
# 3) board.dtb        (o post-image tambem grava na raiz da particao A)
giochi_fdt_ok=0
if test -n "${fdtfile}"; then
  if load mmc ${giochi_mmc}:1 ${fdt_addr_r} dtbs/${fdtfile}; then
    echo "GIOCHI: dtb dtbs/${fdtfile}"
    giochi_fdt_ok=1
  else
    echo "GIOCHI: dtbs/${fdtfile} ausente no cartao; tentando dtbs/board.dtb"
  fi
fi
if test "${giochi_fdt_ok}" = "0"; then
  if load mmc ${giochi_mmc}:1 ${fdt_addr_r} dtbs/board.dtb; then
    echo "GIOCHI: dtb dtbs/board.dtb"
    giochi_fdt_ok=1
  fi
fi
if test "${giochi_fdt_ok}" = "0"; then
  if load mmc ${giochi_mmc}:1 ${fdt_addr_r} board.dtb; then
    echo "GIOCHI: dtb board.dtb (fallback)"
    giochi_fdt_ok=1
  else
    echo "GIOCHI: ERRO: nenhum DTB legivel no cartao"
  fi
fi
# Sair agora, sem tentar o booti, e melhor do que deixar o U-Boot seguir para
# o proximo alvo em silencio: o 'exit' faz o BOOTCOMMAND continuar a varredura
# de mmc, mas o erro fica registrado no serial (e visivel no fbcon).
if test "${giochi_fdt_ok}" = "0"; then
  echo "GIOCHI: abortando sem DTB (Image lida, device tree invalido)"
fi

# ---------------------------------------------------------------------
# Bootsplash (PRE-PRONTO — desativado)
#
# Ativa em conjunto com o bloco CONFIG_* em uboot.fragment. O splash.bmp e
# copiado para a particao A pelo post-image.sh; sem o arquivo, o load falha
# silenciosamente e o boot continua (comportamento seguro).
#
# if load mmc ${giochi_mmc}:1 ${splashimage}; then
#   echo "GIOCHI: splash carregado"
# fi
# ---------------------------------------------------------------------

if load mmc ${giochi_mmc}:1 ${kernel_addr_r} Image; then
  echo "GIOCHI: Image carregada de mmc ${giochi_mmc}:1"
else
  echo "GIOCHI: FALHA ao ler Image de mmc ${giochi_mmc}:1"
fi
echo "GIOCHI: booti kernel=${kernel_addr_r} fdt=${fdt_addr_r}"
booti ${kernel_addr_r} - ${fdt_addr_r}
