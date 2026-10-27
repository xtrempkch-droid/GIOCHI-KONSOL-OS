# Giochi Konsol OS v1.2 — boot.cmd (S2). Fonte de boot.scr (mkimage -T script).
#
# Variaveis de ambiente (particao A, FAT):
#   giochi_slot       -> 2 (B1) | 3 (B2), slot ativo
#   giochi_prev_slot  -> slot anterior para rollback
# Regra: bootcount > 3 => rollback para giochi_prev_slot e zera bootcount.

setenv bootargs_base "console=ttyS2,1500000n8 earlycon=uart8250,mmio32,0xfe660000 rootfstype=squashfs ro rootwait"

if test -z "${giochi_slot}"; then
  setenv giochi_slot 2
  setenv giochi_prev_slot 3
  saveenv
fi

if test "${bootcount}" -gt 3; then
  echo "GIOCHI: rollback apos ${bootcount} falhas de boot"
  setenv tmp "${giochi_slot}"
  setenv giochi_slot "${giochi_prev_slot}"
  setenv giochi_prev_slot "${tmp}"
  setenv bootcount 0
  saveenv
fi

# Layout (secao 1.3): p1 = boot (FAT), p2 = B1, p3 = B2, p4 = /user.
# Nos aparelhos de referencia o cartao SD e o mmc 1 do U-Boot
# (docs/hardware-e-drivers.md §2). giochi_mmc permite sobrescrever.
if test -z "${giochi_mmc}"; then
  setenv giochi_mmc 1
fi

setenv giochi_root "/dev/mmcblk${giochi_mmc}p2"
if test "${giochi_slot}" = "3"; then
  setenv giochi_root "/dev/mmcblk${giochi_mmc}p3"
fi

setenv bootargs "${bootargs_base} root=${giochi_root} giochi.slot=${giochi_slot} giochi.bootcount=${bootcount}"

# DTB por placa: X55 (mainline) x X35S/X35H (mesmo DTB). Sem fdtfile, usa
# board.dtb gravado no cartao (post-image).
if test -n "${fdtfile}"; then
  if load mmc ${giochi_mmc}:1 ${fdt_addr_r} dtbs/${fdtfile}; then
    echo "GIOCHI: dtb dtbs/${fdtfile}"
  fi
fi
if test -z "${fdtfile}"; then
  load mmc ${giochi_mmc}:1 ${fdt_addr_r} board.dtb
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

load mmc ${giochi_mmc}:1 ${kernel_addr_r} Image
booti ${kernel_addr_r} - ${fdt_addr_r}
