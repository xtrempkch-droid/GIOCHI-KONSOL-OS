################################################################################
#
# giochi-ppsspp — PPSSPPSDL + PPSSPPHeadless em /apps/ppsspp
#
# Build do fonte (tag fixada) porque nao existe binario oficial para
# aarch64-musl: o pacote do Alpine e' a variante Qt (arrasta Qt5+ffmpeg, ~35
# MB instalados) e nao serve para um console sem toolkit. Aqui:
#   - SDL2 do sistema (o mesmo do Launcher, KMSDRM);
#   - GLES2 SEM o bloco EGL proprio do PPSSPP: o contexto GL vem do SDL2
#     (SDL_GL_CreateContext) e o bloco EGL do PPSSPP (SDLGLGraphicsContext)
#     chama XOpenDisplay direto no ramo nao-fbdev — sem X11 no alvo, a
#     compilacao morre em "XCloseDisplay was not declared". A config
#     USING_FBDEV=ON + USING_EGL=OFF + USING_GLES2=ON e' a MESMA que a
#     ROCKNIX usa para os mesmos aparelhos (ppsspp-sa/package.mk, ramo
#     OPENGLES_SUPPORT); o FBDEV aqui so' liga SDL_WINDOW_FULLSCREEN_DESKTOP
#     e o define EGL_NO_X11 (o display continua sendo o KMSDRM do SDL2);
#   - SEM ffmpeg (movies nao tocam; jogos nao precisam), SEM Qt, SEM discord;
#   - libpng/zstd/snappy/zip vendidos do proprio repositorio (submodulos);
#   - HEADLESS=ON gera o PPSSPPHeadless (o "CLI" sem display).
#
# CUIDADO (armadilha §3.23, mesmas consequencias): SITE_METHOD=git com
# GIT_SUBMODULES=YES baixa ~20 submodulos, incluindo o fork do ffmpeg
# (~100 MB). E' uma vez so, fica no cache do git em $(GIT_DIR)? Nao: o
# Buildroot clona de novo a cada dirclean. Nao faça dirclean deste pacote sem
# necessidade.
#
# ONDE VAI PARAR: $(BINARIES_DIR)/apps-stage/ppsspp (o post-image.sh injeta
# no user.ext4, igual ao giochi-portmaster — nada pode ficar sob /user no
# SquashFS, §3.46).
################################################################################

GIOCHI_PPSSPP_VERSION = v1.17.1
GIOCHI_PPSSPP_SITE = https://github.com/hrydgard/ppsspp.git
GIOCHI_PPSSPP_SITE_METHOD = git
GIOCHI_PPSSPP_GIT_SUBMODULES = YES
GIOCHI_PPSSPP_LICENSE = GPL-2.0
GIOCHI_PPSSPP_LICENSE_FILES = LICENSE.TXT
GIOCHI_PPSSPP_DEPENDENCIES = sdl2 sdl2_ttf zlib host-python3
GIOCHI_PPSSPP_SUPPORTS_IN_SOURCE_BUILD = NO

GIOCHI_PPSSPP_CONF_OPTS = \
	-DUSE_FFMPEG=OFF \
	-DUSE_DISCORD=OFF \
	-DUSE_SYSTEM_LIBPNG=OFF \
	-DUSE_SYSTEM_LIBSDL2=ON \
	-DUSE_WAYLAND_WSI=OFF \
	-DUSING_X11_VULKAN=OFF \
	-DUSE_VULKAN_DISPLAY_KHR=OFF \
	-DUSING_GLES2=ON \
	-DUSING_EGL=OFF \
	-DUSING_FBDEV=ON \
	-DUSING_QT_UI=OFF \
	-DHEADLESS=ON \
	-DUNITTEST=OFF \
	-DLIBRETRO=OFF \
	-DUSE_LIBNX=OFF \
	-DANDROID=OFF \
	-DCMAKE_DISABLE_FIND_PACKAGE_SDL2_ttf=TRUE \
	-DCMAKE_BUILD_TYPE=Release

# POR QUE USING_EGL=OFF + USING_FBDEV=ON (medido no build real):
#
#   SDL/SDLGLGraphicsContext.cpp:291: error: 'Display' was not declared
#   SDL/SDLGLGraphicsContext.cpp:291: error: 'XCloseDisplay' was not declared
#
# O bloco EGL do PPSSPP (linha 15 do arquivo: #if defined(USING_EGL)) chama
# XOpenDisplay/XCloseDisplay SEM guarda no ramo `#else` (nao-fbdev): ele
# assume "EGL fora do fbdev = X11 disponivel". Num alvo KMS/DRM sem libX11,
# o find_package(X11) do CMakeLists (linha 884) nao acha nada e a
# compilacao morre em 56%%.
#
# Com USING_EGL=OFF o bloco inteiro nao compila e o contexto GLES2 vem do
# SDL2 (Init() usa SDL_GL_CreateContext — o caminho principal, nao um
# fallback). USING_FBDEV=ON liga SDL_WINDOW_FULLSCREEN_DESKTOP (o que um
# console quer) e o add_definitions(-DEGL_NO_X11) do proprio CMakeLists.
# E' a configuracao que a ROCKNIX usa para GLES nesses mesmos aparelhos
# (projects/ROCKNIX/packages/emulators/standalone/ppsspp-sa/package.mk,
# ramo OPENGLES_SUPPORT: -DUSING_FBDEV=ON -DUSING_EGL=OFF -DUSING_GLES2=ON).

# POR QUE CMAKE_DISABLE_FIND_PACKAGE_SDL2_ttf=TRUE (medido no build real):
#
#   CMake Error at CMakeLists.txt:1535 (target_link_libraries):
#     Target "native" links to: SDL2_ttf::SDL2_ttf
#     but the target was not found.
#
# O Buildroot compila o sdl2_ttf com AUTOTOOLS, e o sdl2_ttf-config.cmake que
# o `configure` dele gera NAO e' relocavel: `exec_prefix="/usr"` fixo, e o
# alvo importado so' e' criado se `EXISTS "/usr/lib/libSDL2_ttf.so"` — o
# /usr do HOST, nao o sysroot. No cross, o EXISTS falha, o alvo nunca nasce,
# mas `SDL2_ttf_FOUND` e' setado TRUE incondicionalmente. Resultado: o
# PPSSPP usa `SDL2_ttf::SDL2_ttf` (linha 1371) e o generate morre.
#
# Com o find_package DESLIGADO, o proprio CMakeLists do PPSSPP cai no
# fallback que ele ja' tem: `pkg_check_modules(SDL2_ttf_PKGCONFIG
# IMPORTED_TARGET SDL2_ttf)` — o SDL2_ttf.pc do staging resolve (2.22.0 >=
# 2.0.18) e o alvo PkgConfig::SDL2_ttf_PKGCONFIG e' usado na linha 1373.

# Os assets do jogo (fontes PSP, atlas da UI, idiomas, shaders, gamecontrollerdb)
# viajam junto com o binario; sem eles o menu sai sem fonte e os jogos que
# usam fonte do PSP ficam sem texto. vfpu/ sao testes de VFPU (4,8 MB), nao
# fazem falta em runtime.
define GIOCHI_PPSSPP_INSTALL_TARGET_CMDS
	mkdir -p $(BINARIES_DIR)/apps-stage
	rm -rf $(BINARIES_DIR)/apps-stage/PPSSPP
	mkdir -p $(BINARIES_DIR)/apps-stage/PPSSPP/assets
	$(INSTALL) -m 0755 $(@D)/buildroot-build/PPSSPPSDL \
		$(BINARIES_DIR)/apps-stage/PPSSPP/PPSSPPSDL
	$(INSTALL) -m 0755 $(@D)/buildroot-build/PPSSPPHeadless \
		$(BINARIES_DIR)/apps-stage/PPSSPP/PPSSPPHeadless
	$(TARGET_STRIP) $(BINARIES_DIR)/apps-stage/PPSSPP/PPSSPPSDL \
		$(BINARIES_DIR)/apps-stage/PPSSPP/PPSSPPHeadless 2>/dev/null || true
	cp -a $(@D)/assets/lang $(@D)/assets/shaders \
		$(BINARIES_DIR)/apps-stage/PPSSPP/assets/
	cp -a $(@D)/assets/flash0 \
		$(BINARIES_DIR)/apps-stage/PPSSPP/assets/
	cp -a $(@D)/assets/font_atlas.zim $(@D)/assets/ppge_atlas.zim \
		$(@D)/assets/asciifont_atlas.zim $(@D)/assets/compat.ini \
		$(@D)/assets/Roboto-Condensed.ttf $(@D)/assets/redump.csv \
		$(@D)/assets/gamecontrollerdb.txt \
		$(BINARIES_DIR)/apps-stage/PPSSPP/assets/ 2>/dev/null || true
	# O MESMO arranjo do PortMaster: pasta do app em /apps/PPSSPP e o
	# wrapper EXECUTAVEL em /apps/ppsspp — o que o APP:ppsspp do Launcher
	# resolve. A pasta e o wrapper NAO podem ter o mesmo nome: /apps/ppsspp
	# tem de ser arquivo executavel, nao diretorio (o execve falharia).
	$(INSTALL) -m 0755 $(GIOCHI_PPSSPP_PKGDIR)/ppsspp-wrapper \
		$(BINARIES_DIR)/apps-stage/ppsspp
endef

$(eval $(cmake-package))
