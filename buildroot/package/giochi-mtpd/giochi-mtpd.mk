################################################################################
#
# giochi-mtpd — responder MTP (secao 4.6)
#
# O f_mtp do kernel so faz o transporte; quem responde o protocolo MTP e o
# umtprd no userspace. Sem isto o PC ve o device e nao lista arquivo nenhum.
#
################################################################################

GIOCHI_MTPD_VERSION = umtprd-1.8.1
GIOCHI_MTPD_SITE = $(call github,viveris,uMTP-Responder,$(GIOCHI_MTPD_VERSION))
GIOCHI_MTPD_LICENSE = GPL-3.0
GIOCHI_MTPD_LICENSE_FILES = LICENSE
GIOCHI_MTPD_DEPENDENCIES = host-pkgconf

GIOCHI_MTPD_STORAGE = $(call qstrip,$(BR2_PACKAGE_GIOCHI_MTPD_STORAGE))

# O Makefile do upstream usa CFLAGS/LDFLAGS do ambiente; nao ha configure.
GIOCHI_MTPD_MAKE_OPTS = \
	CC="$(TARGET_CC)" \
	CFLAGS="$(TARGET_CFLAGS) -I./inc -Wall" \
	LDFLAGS="$(TARGET_LDFLAGS) -lpthread -lrt"

ifeq ($(BR2_STATIC_LIBS),y)
GIOCHI_MTPD_MAKE_OPTS += LDFLAGS="$(TARGET_LDFLAGS) -static -lpthread -lrt"
endif

define GIOCHI_MTPD_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) $(GIOCHI_MTPD_MAKE_OPTS) DEBUG=0
endef

define GIOCHI_MTPD_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/umtprd $(TARGET_DIR)/usr/sbin/umtprd

	# Configuracao do console (storage, VID/PID, caminhos do FunctionFS).
	$(INSTALL) -D -m 0644 $(GIOCHI_MTPD_PKGDIR)/umtprd.conf \
		$(TARGET_DIR)/etc/umtprd.conf
	$(SED) "s|@GIOCHI_MTP_STORAGE@|$(GIOCHI_MTPD_STORAGE)|g" \
		$(TARGET_DIR)/etc/umtprd.conf

	# Helper de ciclo de vida (start/stop com pidfile), usado pelo
	# giochi-usb-mode ao trocar de modo.
	$(INSTALL) -D -m 0755 $(GIOCHI_MTPD_PKGDIR)/giochi-mtpd \
		$(TARGET_DIR)/usr/sbin/giochi-mtpd
endef

define GIOCHI_MTPD_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(GIOCHI_MTPD_PKGDIR)/S60mtpd \
		$(TARGET_DIR)/etc/init.d/S60mtpd
endef

$(eval $(generic-package))
