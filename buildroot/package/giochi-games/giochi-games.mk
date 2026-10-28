################################################################################
#
# giochi-games — jogos padrao do sistema (teste e benchmark do aparelho)
#
# A lista e o campo "license" de cada jogo vivem em assets/default-games.json,
# que e a FONTE UNICA: o script de instalacao le o manifesto em vez de repetir
# nomes de jogo aqui. tools/check-default-games.py barra licenca que nao pode
# vir no aparelho (copyleft/proprietary) e projeto sem bench.
#
# O QUE AINDA NAO EXISTE: o export template do Godot para aarch64/musl
# (ADR-03). Sem ele nao ha binario pronto, entao o pacote instala os PROJETOS
# (que rodam no host e no aparelho com o motor disponivel) e o manifesto.
# Quando o template entrar, e aqui que ele passa a ser usado.
#
################################################################################

GIOCHI_GAMES_VERSION = 1.0
GIOCHI_GAMES_SITE = $(BR2_EXTERNAL_GIOCHI_PATH)/..
GIOCHI_GAMES_SITE_METHOD = local
GIOCHI_GAMES_LICENSE = Public Domain / permissao dos autores / codigo proprio
GIOCHI_GAMES_LICENSE_FILES = assets/default-games.json

# O manifesto e o script de instalacao precisam do mesmo caminho de origem.
GIOCHI_GAMES_MANIFEST = assets/default-games.json
GIOCHI_GAMES_INSTALLER = $(BR2_EXTERNAL_GIOCHI_PATH)/package/giochi-games/install-games.sh

define GIOCHI_GAMES_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/usr/share/giochi
	$(INSTALL) -m 0644 $(@D)/$(GIOCHI_GAMES_MANIFEST) \
		$(TARGET_DIR)/usr/share/giochi/default-games.json
	sh $(GIOCHI_GAMES_INSTALLER) \
		$(GIOCHI_GAMES_MANIFEST) \
		$(@D) \
		$(TARGET_DIR)/usr/share/giochi/games
endef

define GIOCHI_GAMES_INSTALL_INIT_SYSROOT
	# Nada a fazer: nao ha servico, o jogo e iniciado pelo Launcher/bench.
endef

$(eval $(generic-package))
