################################################################################
#
# giochi-portmaster — PortMaster em /apps (aba Ports do Launcher)
#
# O bundle oficial do muOS e' um zip com o nucleo (PortMaster.sh, funcs,
# dialog) + ferramentas por arquitetura + pastas de hack por firmware. Este
# pacote:
#   1. baixa o zip oficial (versao fixada);
#   2. remove o que nao e' nosso (pastas batocera/knulli/muos/trimui/... e os
#      binarios armhf/x86_64);
#   3. ajusta o controlfolder para /apps/PortMaster e remove a chamada
#      muos/image_smash.txt (muOS-only, roda no caminho do autoinstall);
#   4. escreve device_info.txt (CFW_NAME=giochi) e mod_giochi.txt;
#   5. cria os symlinks de arquitetura que os ports esperam
#      (7zzs, sdl2imgshow, innoextract, astcenc, sdl_resolution,
#      libinterpose.so) e o wrapper /apps/portmaster.
#
# ONDE VAI PARAR: $(BINARIES_DIR)/apps-stage/PortMaster. O post-image.sh
# injeta esse diretorio no user.ext4 (particao gravavel) via debugfs — a raiz
# e' SquashFS e /apps -> /user/apps, entao nada disto pode ficar no TARGET_DIR
# (armadilha §3.46: o user.ext4 montado ESCONDE o que estiver sob /user).
#
# Por que nao SITE_METHOD=local: o zip vem do GitHub, nao do repositorio.
################################################################################

GIOCHI_PORTMASTER_VERSION = 2026.11.01-1314
GIOCHI_PORTMASTER_SOURCE = muos.portmaster.zip
GIOCHI_PORTMASTER_SITE = https://github.com/PortsMaster/PortMaster-GUI/releases/download/$(GIOCHI_PORTMASTER_VERSION)
GIOCHI_PORTMASTER_LICENSE = MIT (nucleo); licencas dos componentes em licenses/
GIOCHI_PORTMASTER_LICENSE_FILES = licenses

# O download-wrapper do Buildroot nao extrai .zip; o unzip do host faz.
#
# O `rm -rf $(@D)` e' obrigatorio: o diretorio de build SOBREVIVE a um build
# que falha, e o unzip INTERATIVO pergunta "replace ...? [y]es, [n]o, [A]ll"
# quando o arquivo ja' existe — com stdin fechado ele trata como "[N]one" e
# MORRE (medido: "NULL (EOF or read error, treating as "[N]one" ...)" no
# segundo build depois da falha do PPSSPP). O `-o` e' a defesa em
# profundidade: sobrescreve sem perguntar, mesmo que algo tenha ficado.
define GIOCHI_PORTMASTER_EXTRACT_CMDS
	rm -rf $(@D)
	mkdir -p $(@D)
	unzip -q -o $(GIOCHI_PORTMASTER_DL_DIR)/$(GIOCHI_PORTMASTER_SOURCE) -d $(@D)
endef

define GIOCHI_PORTMASTER_INSTALL_TARGET_CMDS
	mkdir -p $(BINARIES_DIR)/apps-stage
	rm -rf $(BINARIES_DIR)/apps-stage/PortMaster
	mkdir -p $(BINARIES_DIR)/apps-stage/PortMaster
	cp -a $(@D)/mnt/mmc/MUOS/PortMaster/. \
		$(BINARIES_DIR)/apps-stage/PortMaster/
	cd $(BINARIES_DIR)/apps-stage/PortMaster && \
		rm -rf batocera knulli muos trimui retrodeck miyoo post-install \
			".Backup" 2>/dev/null || true
	cd $(BINARIES_DIR)/apps-stage/PortMaster && \
		rm -f 7zzs.armhf 7zzs.x86_64 gptokeyb.armhf gptokeyb.x86_64 \
			gptokeyb2.armhf gptokeyb2.x86_64 innoextract.armhf \
			innoextract.x86_64 sdl2imgshow.armhf sdl2imgshow.x86_64 \
			sdl_resolution.armhf sdl_resolution.x86_64 xdelta3.armhf \
			xdelta3.x86_64 libinterpose.armhf.so libinterpose.x86_64.so \
			2>/dev/null || true
	# controlfolder e o caminho que o PortMaster.sh usa para tudo; o bundle
	# muOS aponta para /mnt/mmc/MUOS/PortMaster.
	sed -i 's#controlfolder="/mnt/mmc/MUOS/PortMaster"#controlfolder="/apps/PortMaster"#' \
		$(BINARIES_DIR)/apps-stage/PortMaster/PortMaster.sh
	# image_smash e' especifico do muOS (chamado no fim do autoinstall).
	sed -i '\#\${controlfolder}/muos/image_smash.txt#d' \
		$(BINARIES_DIR)/apps-stage/PortMaster/PortMaster.sh
	# device_info/mod do nosso firmware (CFW_NAME=giochi).
	$(INSTALL) -m 0755 $(GIOCHI_PORTMASTER_PKGDIR)/device_info.txt \
		$(BINARIES_DIR)/apps-stage/PortMaster/device_info.txt
	$(INSTALL) -m 0755 $(GIOCHI_PORTMASTER_PKGDIR)/mod_giochi.txt \
		$(BINARIES_DIR)/apps-stage/PortMaster/mod_giochi.txt
	# Symlinks de arquitetura: os ports chamam o nome SEM sufixo.
	cd $(BINARIES_DIR)/apps-stage/PortMaster && \
		ln -sf 7zzs.aarch64 7zzs && \
		ln -sf sdl2imgshow.aarch64 sdl2imgshow && \
		ln -sf innoextract.aarch64 innoextract && \
		ln -sf astcenc.aarch64 astcenc && \
		ln -sf sdl_resolution.aarch64 sdl_resolution && \
		ln -sf libinterpose.aarch64.so libinterpose.so
	# Wrapper: o Launcher lanca APP:portmaster -> /apps/portmaster.
	$(INSTALL) -m 0755 $(GIOCHI_PORTMASTER_PKGDIR)/portmaster-wrapper \
		$(BINARIES_DIR)/apps-stage/portmaster
endef

$(eval $(generic-package))
