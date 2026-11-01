################################################################################
#
# giochi-tools — verify/extract/thumbgen (S9, S12)
#
# SO o que roda NO CONSOLE entra aqui:
#   giochi-verify   -> valida assinatura Ed25519+SHA256 do update (§4.3)
#   giochi-extract  -> desempacota .giochi ao instalar (§4.2)
#   giochi-thumbgen -> pre-gera thumbnails 152x85 no install (§4.2)
#
# Ferramentas de PC NAO entram na imagem: giochi-pack.sh, giochi-sign.sh,
# giochi-keys.sh e giochi-forge (src/forge) sao do lado do autor/publicador.
# Alem de nao terem uso no aparelho, o giochi-pack.sh depende de GNU tar
# (--format=ustar, --owner/--group, -r) e de jq, que o rootfs nao tem — na
# imagem ele falharia em toda invocacao. tools/check-image-contents.sh barra
# regressao desse tipo.
#
################################################################################

GIOCHI_TOOLS_VERSION = 1.2
# Fontes: build-image.sh exporta GIOCHI_PKGSRC com um snapshot so do que e
# fonte (~9 MB). Sem ele o SITE caia na raiz do repositorio, e o rsync do
# Buildroot copiava dist/ (o build em andamento) para dentro de si mesmo.
GIOCHI_TOOLS_SITE = $(if $(GIOCHI_PKGSRC),$(GIOCHI_PKGSRC),$(BR2_EXTERNAL_GIOCHI_PATH)/..)
GIOCHI_TOOLS_SITE_METHOD = local
GIOCHI_TOOLS_LICENSE = Proprietary
GIOCHI_TOOLS_DEPENDENCIES = openssl libarchive libpng host-pkgconf \
	libdrm mesa3d

GIOCHI_TOOLS_EXTRA_CFLAGS = $(TARGET_CFLAGS) \
	-march=$(GIOCHI_ARCH) -D_GNU_SOURCE \
	-DGIOCHI_HAVE_OPENSSL -DGIOCHI_HAVE_LIBARCHIVE -DGIOCHI_HAVE_LIBPNG
GIOCHI_TOOLS_EXTRA_LDFLAGS = $(TARGET_LDFLAGS) -lpng -larchive -lcrypto

# giochi-gpu-probe depende de libdrm/libgbm/EGL, que SO existem no sysroot do
# Buildroot — por isso ele e' compilado aqui e nao no Makefile da raiz (que
# roda no host, sem essas bibliotecas).
GIOCHI_TOOLS_GPU_SRC = $(@D)/src/tools/gpu_probe.c
GIOCHI_TOOLS_GPU_LIBS = -ldrm -lgbm -lEGL -lGLESv2
GIOCHI_TOOLS_GPU_INCS = -I$(STAGING_DIR)/usr/include \
	-I$(STAGING_DIR)/usr/include/libdrm

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
	$(TARGET_CC) $(GIOCHI_TOOLS_EXTRA_CFLAGS) $(GIOCHI_TOOLS_GPU_INCS) \
		-o $(@D)/build/target/bin/giochi-gpu-probe \
		$(GIOCHI_TOOLS_GPU_SRC) $(TARGET_LDFLAGS) $(GIOCHI_TOOLS_GPU_LIBS)
endef

define GIOCHI_TOOLS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/build/target/bin/giochi-verify \
		$(TARGET_DIR)/usr/bin/giochi-verify
	$(INSTALL) -D -m 0755 $(@D)/build/target/bin/giochi-extract \
		$(TARGET_DIR)/usr/bin/giochi-extract
	$(INSTALL) -D -m 0755 $(@D)/build/target/bin/giochi-thumbgen \
		$(TARGET_DIR)/usr/bin/giochi-thumbgen
	$(INSTALL) -D -m 0755 $(@D)/build/target/bin/giochi-gpu-probe \
		$(TARGET_DIR)/usr/bin/giochi-gpu-probe
endef

$(eval $(generic-package))
