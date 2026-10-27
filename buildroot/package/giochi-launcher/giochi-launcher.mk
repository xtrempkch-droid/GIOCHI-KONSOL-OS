################################################################################
#
# giochi-launcher — UI (ADR-09)
#
################################################################################

GIOCHI_LAUNCHER_VERSION = 1.2
GIOCHI_LAUNCHER_SITE = $(BR2_EXTERNAL_GIOCHI_PATH)/..
GIOCHI_LAUNCHER_SITE_METHOD = local
GIOCHI_LAUNCHER_LICENSE = Proprietary
GIOCHI_LAUNCHER_DEPENDENCIES = sdl2 host-pkgconf

# SDL2 vem do staging; os backticks rodam na execucao da receita (depois de
# sdl2 estar instalado), evitando $(shell) vazio na fase de parse.
GIOCHI_LAUNCHER_SDL_CFLAGS = `$(STAGING_DIR)/usr/bin/sdl2-config --cflags`
GIOCHI_LAUNCHER_SDL_LIBS = `$(STAGING_DIR)/usr/bin/sdl2-config --libs`

GIOCHI_LAUNCHER_EXTRA_CXXFLAGS = $(TARGET_CXXFLAGS) \
	-march=$(GIOCHI_ARCH) -D_GNU_SOURCE -DGIOCHI_HAVE_SDL2 \
	$(GIOCHI_LAUNCHER_SDL_CFLAGS) \
	-DGIOCHI_ENABLE_PROTON=$(if $(BR2_PACKAGE_GIOCHI_PROTON),1,0)

define GIOCHI_LAUNCHER_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) HOST_BUILD=0 \
		BUILD_DIR=build OPT="-O2" \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" \
		PKG_CONFIG="$(PKG_CONFIG_HOST_BINARY)" \
		PKG_CONFIG_SYSROOT_DIR="$(STAGING_DIR)" \
		PKG_CONFIG_LIBDIR="$(STAGING_DIR)/usr/lib/pkgconfig:$(STAGING_DIR)/usr/share/pkgconfig" \
		EXTRA_CXXFLAGS="$(GIOCHI_LAUNCHER_EXTRA_CXXFLAGS)" \
		EXTRA_LDFLAGS="$(TARGET_LDFLAGS) $(GIOCHI_LAUNCHER_SDL_LIBS)" \
		build/target/bin/giochi-launcher
endef

define GIOCHI_LAUNCHER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/build/target/bin/giochi-launcher \
		$(TARGET_DIR)/usr/bin/giochi-launcher
endef

$(eval $(generic-package))
