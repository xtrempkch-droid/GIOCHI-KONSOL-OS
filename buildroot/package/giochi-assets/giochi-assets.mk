################################################################################
#
# giochi-assets — layouts/tema (secoes 4.1 e 4.8)
#
################################################################################

GIOCHI_ASSETS_VERSION = 1.2
# Fontes: build-image.sh exporta GIOCHI_PKGSRC com um snapshot so do que e
# fonte (~9 MB). Sem ele o SITE caia na raiz do repositorio, e o rsync do
# Buildroot copiava dist/ (o build em andamento) para dentro de si mesmo.
GIOCHI_ASSETS_SITE = $(if $(GIOCHI_PKGSRC),$(GIOCHI_PKGSRC),$(BR2_EXTERNAL_GIOCHI_PATH)/..)
GIOCHI_ASSETS_SITE_METHOD = local
GIOCHI_ASSETS_LICENSE = Proprietary

define GIOCHI_ASSETS_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/usr/share/giochi
	$(INSTALL) -m 0644 $(@D)/assets/*.json $(TARGET_DIR)/usr/share/giochi/
	# Atlas da fonte embutida (gerado por tools/make-font.py). Sem ele o
	# Launcher desenha as caixas sem rotulo: diagnostico, nao tela preta.
	$(INSTALL) -m 0644 $(@D)/assets/font.a8 $(TARGET_DIR)/usr/share/giochi/font.a8
endef

$(eval $(generic-package))
