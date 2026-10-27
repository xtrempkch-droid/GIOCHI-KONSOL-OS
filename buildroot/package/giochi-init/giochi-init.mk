################################################################################
#
# giochi-init — supervisor (ADR-10)
#
################################################################################

GIOCHI_INIT_VERSION = 1.2
GIOCHI_INIT_SITE = $(BR2_EXTERNAL_GIOCHI_PATH)/..
GIOCHI_INIT_SITE_METHOD = local
GIOCHI_INIT_LICENSE = Proprietary
GIOCHI_INIT_DEPENDENCIES = host-pkgconf

GIOCHI_INIT_SOC = $(call qstrip,$(BR2_PACKAGE_GIOCHI_INIT_SOC))
GIOCHI_INIT_DEVICE = $(call qstrip,$(BR2_PACKAGE_GIOCHI_INIT_DEVICE))
GIOCHI_INIT_EXTRA_LDFLAGS = $(TARGET_LDFLAGS)
GIOCHI_INIT_PROTON = $(if $(BR2_PACKAGE_GIOCHI_PROTON),1,0)

GIOCHI_INIT_EXTRA_CFLAGS = $(TARGET_CFLAGS) \
	-march=$(GIOCHI_ARCH) -D_GNU_SOURCE \
	-DGIOCHI_SOC='"$(GIOCHI_INIT_SOC)"' \
	-DGIOCHI_DEVICE='"$(GIOCHI_INIT_DEVICE)"' \
	-DGIOCHI_OS_VERSION='"1.2"' \
	-DGIOCHI_KERNEL_VERSION='"6.12"' \
	-DGIOCHI_ENABLE_PROTON=$(GIOCHI_INIT_PROTON)

ifeq ($(BR2_PACKAGE_OPENSSL),y)
GIOCHI_INIT_DEPENDENCIES += openssl
GIOCHI_INIT_EXTRA_CFLAGS += -DGIOCHI_HAVE_OPENSSL
GIOCHI_INIT_EXTRA_LDFLAGS += -lcrypto
endif

# Um unico descritor de build (Makefile da raiz) evita divergencia de fontes.
define GIOCHI_INIT_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) HOST_BUILD=0 \
		BUILD_DIR=build OPT="-O2" \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" \
		PKG_CONFIG="$(PKG_CONFIG_HOST_BINARY)" \
		PKG_CONFIG_SYSROOT_DIR="$(STAGING_DIR)" \
		PKG_CONFIG_LIBDIR="$(STAGING_DIR)/usr/lib/pkgconfig:$(STAGING_DIR)/usr/share/pkgconfig" \
		EXTRA_CFLAGS="$(GIOCHI_INIT_EXTRA_CFLAGS)" \
		EXTRA_LDFLAGS="$(GIOCHI_INIT_EXTRA_LDFLAGS)" \
		build/target/bin/giochi-init
endef

define GIOCHI_INIT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/build/target/bin/giochi-init \
		$(TARGET_DIR)/usr/bin/giochi-init
endef

$(eval $(generic-package))
