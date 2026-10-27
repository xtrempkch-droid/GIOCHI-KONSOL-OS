################################################################################
#
# giochi-musicd — daemon de musica (secao 4.8)
#
################################################################################

GIOCHI_MUSICD_VERSION = 1.2
GIOCHI_MUSICD_SITE = $(BR2_EXTERNAL_GIOCHI_PATH)/..
GIOCHI_MUSICD_SITE_METHOD = local
GIOCHI_MUSICD_LICENSE = Proprietary
GIOCHI_MUSICD_DEPENDENCIES = alsa-lib host-pkgconf

GIOCHI_MUSICD_EXTRA_CFLAGS = $(TARGET_CFLAGS) \
	-march=$(GIOCHI_ARCH) -D_GNU_SOURCE -DGIOCHI_HAVE_ALSA
GIOCHI_MUSICD_EXTRA_LDFLAGS = $(TARGET_LDFLAGS) -lasound

ifeq ($(BR2_PACKAGE_LIBMPG123),y)
GIOCHI_MUSICD_DEPENDENCIES += libmpg123
GIOCHI_MUSICD_EXTRA_CFLAGS += -DGIOCHI_HAVE_MPG123
GIOCHI_MUSICD_EXTRA_LDFLAGS += -lmpg123
endif

ifeq ($(BR2_PACKAGE_FLAC),y)
GIOCHI_MUSICD_DEPENDENCIES += flac
GIOCHI_MUSICD_EXTRA_CFLAGS += -DGIOCHI_HAVE_FLAC
GIOCHI_MUSICD_EXTRA_LDFLAGS += -lFLAC
endif

ifeq ($(BR2_PACKAGE_LIBVORBIS),y)
GIOCHI_MUSICD_DEPENDENCIES += libvorbis
GIOCHI_MUSICD_EXTRA_CFLAGS += -DGIOCHI_HAVE_VORBIS
GIOCHI_MUSICD_EXTRA_LDFLAGS += -lvorbisfile
endif

define GIOCHI_MUSICD_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) HOST_BUILD=0 \
		BUILD_DIR=build OPT="-O2" \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" \
		PKG_CONFIG="$(PKG_CONFIG_HOST_BINARY)" \
		PKG_CONFIG_SYSROOT_DIR="$(STAGING_DIR)" \
		PKG_CONFIG_LIBDIR="$(STAGING_DIR)/usr/lib/pkgconfig:$(STAGING_DIR)/usr/share/pkgconfig" \
		EXTRA_CFLAGS="$(GIOCHI_MUSICD_EXTRA_CFLAGS)" \
		EXTRA_LDFLAGS="$(GIOCHI_MUSICD_EXTRA_LDFLAGS)" \
		build/target/bin/giochi-musicd
endef

define GIOCHI_MUSICD_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/build/target/bin/giochi-musicd \
		$(TARGET_DIR)/usr/bin/giochi-musicd
endef

$(eval $(generic-package))
