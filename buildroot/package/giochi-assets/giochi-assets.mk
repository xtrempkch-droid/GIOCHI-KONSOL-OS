################################################################################
#
# giochi-assets — layouts/tema (secoes 4.1 e 4.8)
#
################################################################################

GIOCHI_ASSETS_VERSION = 1.2
GIOCHI_ASSETS_SITE = $(BR2_EXTERNAL_GIOCHI_PATH)/..
GIOCHI_ASSETS_SITE_METHOD = local
GIOCHI_ASSETS_LICENSE = Proprietary

define GIOCHI_ASSETS_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/usr/share/giochi
	$(INSTALL) -m 0644 $(@D)/assets/*.json $(TARGET_DIR)/usr/share/giochi/
endef

$(eval $(generic-package))
