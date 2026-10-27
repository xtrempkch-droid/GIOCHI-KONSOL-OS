################################################################################
#
# giochi-tools — verify/extract/thumbgen (S9, S12)
#
################################################################################

GIOCHI_TOOLS_VERSION = 1.2
GIOCHI_TOOLS_SITE = $(BR2_EXTERNAL_GIOCHI_PATH)/..
GIOCHI_TOOLS_SITE_METHOD = local
GIOCHI_TOOLS_LICENSE = Proprietary
GIOCHI_TOOLS_DEPENDENCIES = openssl libarchive libpng host-pkgconf

GIOCHI_TOOLS_EXTRA_CFLAGS = $(TARGET_CFLAGS) \
	-march=$(GIOCHI_ARCH) -D_GNU_SOURCE \
	-DGIOCHI_HAVE_OPENSSL -DGIOCHI_HAVE_LIBARCHIVE -DGIOCHI_HAVE_LIBPNG
GIOCHI_TOOLS_EXTRA_LDFLAGS = $(TARGET_LDFLAGS) -lpng -larchive -lcrypto

define GIOCHI_TOOLS_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) HOST_BUILD=0 \
		BUILD_DIR=build OPT="-O2" \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" \
		PKG_CONFIG="$(PKG_CONFIG_HOST_BINARY)" \
		PKG_CONFIG_SYSROOT_DIR="$(STAGING_DIR)" \
		PKG_CONFIG_LIBDIR="$(STAGING_DIR)/usr/lib/pkgconfig:$(STAGING_DIR)/usr/share/pkgconfig" \
		EXTRA_CFLAGS="$(GIOCHI_TOOLS_EXTRA_CFLAGS)" \
		EXTRA_LDFLAGS="$(GIOCHI_TOOLS_EXTRA_LDFLAGS)" \
		build/target/bin/giochi-verify build/target/bin/giochi-extract \
		build/target/bin/giochi-thumbgen
endef

define GIOCHI_TOOLS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/build/target/bin/giochi-verify \
		$(TARGET_DIR)/usr/bin/giochi-verify
	$(INSTALL) -D -m 0755 $(@D)/build/target/bin/giochi-extract \
		$(TARGET_DIR)/usr/bin/giochi-extract
	$(INSTALL) -D -m 0755 $(@D)/build/target/bin/giochi-thumbgen \
		$(TARGET_DIR)/usr/bin/giochi-thumbgen
	$(INSTALL) -D -m 0755 $(@D)/tools/giochi-pack.sh \
		$(TARGET_DIR)/usr/bin/giochi-pack.sh
endef

$(eval $(generic-package))
