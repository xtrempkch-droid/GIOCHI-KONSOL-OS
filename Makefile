# Giochi Konsol OS v1.2 — build de host (validação) e cross (Buildroot).
#
# O alvo padrão compila todos os binários em build/host usando as bibliotecas
# disponíveis no host. Bibliotecas opcionais são detectadas por pkg-config e
# ativam os macros GIOCHI_HAVE_*; sem elas o código cai nos stubs previstos na
# especificação (render headless, áudio indisponível, etc.).
#
# Cross/device:
#   make CC=aarch64-linux-musl-gcc CXX=aarch64-linux-musl-g++ HOST=0
# (no Buildroot os mesmos fontes são compilados pelos pacotes em buildroot/.)

BUILD_DIR   ?= build
HOST_BUILD  ?= 1
PREFIX      ?= /usr
BINDIR      ?= $(PREFIX)/bin
ASSETDIR    ?= $(PREFIX)/share/giochi

PKG_CONFIG  ?= pkg-config

ifeq ($(HOST_BUILD),1)
  BUILD_ROOT := $(BUILD_DIR)/host
else
  BUILD_ROOT := $(BUILD_DIR)/target
endif

CC  ?= cc
CXX ?= c++

OPT     ?= -O2 -g
CSTD    ?= -std=c99
CXXSTD  ?= -std=c++17

COMMON_WARN := -Wall -Wextra -Wshadow -Wformat=2 -Wundef
CWARN       := $(COMMON_WARN) -Wstrict-prototypes -Wmissing-prototypes
CXXWARN     := $(COMMON_WARN) -Wnon-virtual-dtor

# Buildroot/host externo pode injetar flags (sysroot, -march, etc.).
EXTRA_CFLAGS   ?=
EXTRA_CXXFLAGS ?=
EXTRA_LDFLAGS  ?=

DEFS := -D_GNU_SOURCE
INCS := -Isrc/common -Isrc/init -Isrc/launcher -Isrc/musicd

CFLAGS_ALL   = $(CSTD) $(OPT) $(CWARN) $(DEFS) $(INCS) $(EXTRA_CFLAGS)
CXXFLAGS_ALL = $(CXXSTD) $(OPT) $(CXXWARN) $(DEFS) $(INCS) $(EXTRA_CXXFLAGS)

PKG_LIBS :=
SDL2_CFLAGS :=
SDL2_LIBS :=
ALSA_LIBS :=
MPG123_LIBS :=
FLAC_LIBS :=
CRYPTO_LIBS :=
PNG_LIBS :=
ARCHIVE_LIBS :=

pkg_ok = $(shell $(PKG_CONFIG) --exists $(1) 2>/dev/null && echo 1 || echo 0)

HAVE_SDL2     := $(call pkg_ok,sdl2)
HAVE_ALSA     := $(call pkg_ok,alsa)
HAVE_MPG123   := $(call pkg_ok,libmpg123)
HAVE_FLAC     := $(call pkg_ok,flac)
HAVE_OPENSSL  := $(call pkg_ok,libcrypto)
HAVE_LIBPNG   := $(call pkg_ok,libpng)
HAVE_ARCHIVE  := $(call pkg_ok,libarchive)

ifeq ($(HAVE_SDL2),1)
  SDL2_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
  SDL2_LIBS   := $(shell $(PKG_CONFIG) --libs sdl2)
  DEFS += -DGIOCHI_HAVE_SDL2
endif
ifeq ($(HAVE_ALSA),1)
  ALSA_LIBS := $(shell $(PKG_CONFIG) --libs alsa)
  DEFS += -DGIOCHI_HAVE_ALSA
endif
ifeq ($(HAVE_MPG123),1)
  MPG123_LIBS := $(shell $(PKG_CONFIG) --libs libmpg123)
  DEFS += -DGIOCHI_HAVE_MPG123
endif
ifeq ($(HAVE_FLAC),1)
  FLAC_LIBS := $(shell $(PKG_CONFIG) --libs flac)
  DEFS += -DGIOCHI_HAVE_FLAC
endif
ifeq ($(HAVE_OPENSSL),1)
  CRYPTO_LIBS := $(shell $(PKG_CONFIG) --libs libcrypto)
  DEFS += -DGIOCHI_HAVE_OPENSSL
endif
ifeq ($(HAVE_LIBPNG),1)
  PNG_LIBS := $(shell $(PKG_CONFIG) --libs libpng)
  DEFS += -DGIOCHI_HAVE_LIBPNG
endif
ifeq ($(HAVE_ARCHIVE),1)
  ARCHIVE_LIBS := $(shell $(PKG_CONFIG) --libs libarchive)
  DEFS += -DGIOCHI_HAVE_LIBARCHIVE
endif

CFLAGS_ALL   += $(SDL2_CFLAGS)
CXXFLAGS_ALL += $(SDL2_CFLAGS)

OBJ := $(BUILD_ROOT)/obj
BIN := $(BUILD_ROOT)/bin

COMMON_SRC := \
	src/common/giochi_json.c \
	src/common/giochi_log.c \
	src/common/giochi_util.c \
	src/common/giochi_sock.c \
	src/common/giochi_transition.c \
	src/common/giochi_hotkey.c \
	src/common/giochi_sha256.c \
	src/common/giochi_ed25519.c

COMMON_OBJ := $(patsubst src/%.c,$(OBJ)/%.o,$(COMMON_SRC))

INIT_SRC := \
	src/init/main.c \
	src/init/supervisor.c \
	src/init/control.c \
	src/init/power.c \
	src/init/device_profile.c \
	src/init/game.c \
	src/init/hotkey.c

INIT_OBJ := $(patsubst src/%.c,$(OBJ)/%.o,$(INIT_SRC))

MUSICD_SRC := src/musicd/main.c src/musicd/musicd.c
ifeq ($(HAVE_ALSA),1)
  MUSICD_SRC += src/musicd/audio_alsa.c
else
  MUSICD_SRC += src/musicd/audio_stub.c
endif
MUSICD_OBJ := $(patsubst src/%.c,$(OBJ)/%.o,$(MUSICD_SRC))

LAUNCHER_SRC := \
	src/launcher/main.cpp \
	src/launcher/layout.cpp \
	src/launcher/theme.cpp \
	src/launcher/settings.cpp \
	src/launcher/overlay.cpp \
	src/launcher/input.cpp \
	src/launcher/evdev.cpp \
	src/launcher/ipc_client.cpp \
	src/launcher/metadata.cpp \
	src/launcher/library.cpp \
	src/launcher/render.cpp \
	src/launcher/render_null.cpp

LAUNCHER_OBJ := $(patsubst src/%.cpp,$(OBJ)/%.o,$(LAUNCHER_SRC))

THUMBGEN_SRC := src/thumbgen/main.c
THUMBGEN_OBJ := $(patsubst src/%.c,$(OBJ)/%.o,$(THUMBGEN_SRC))

TOOLS_SRC := tools/giochi-verify.c tools/giochi-extract.c
TOOLS_OBJ := $(patsubst tools/%.c,$(OBJ)/tools/%.o,$(TOOLS_SRC))

TARGETS := \
	$(BIN)/giochi-init \
	$(BIN)/giochi-musicd \
	$(BIN)/giochi-launcher \
	$(BIN)/giochi-thumbgen \
	$(BIN)/giochi-verify \
	$(BIN)/giochi-extract

TESTS_C := \
	tests/test_json.c \
	tests/test_sha256.c \
	tests/test_hotkey.c \
	tests/test_musicd.c \
	tests/test_transition.c

TESTS_CXX := \
	tests/test_layout.cpp \
	tests/test_metadata.cpp \
	tests/test_settings.cpp \
	tests/test_buttonsmap.cpp

TEST_BINS := $(patsubst tests/%.c,$(BIN)/%,$(TESTS_C)) \
             $(patsubst tests/%.cpp,$(BIN)/%,$(TESTS_CXX))

.PHONY: all clean test install info check-config defconfigs lint images bootlogo

all: $(TARGETS)

info:
	@echo "build root : $(BUILD_ROOT)"
	@echo "CC/CXX     : $(CC) / $(CXX)"
	@echo "SDL2       : $(HAVE_SDL2)"
	@echo "ALSA       : $(HAVE_ALSA)"
	@echo "mpg123     : $(HAVE_MPG123)"
	@echo "FLAC       : $(HAVE_FLAC)"
	@echo "OpenSSL    : $(HAVE_OPENSSL)"
	@echo "libpng     : $(HAVE_LIBPNG)"
	@echo "libarchive : $(HAVE_ARCHIVE)"

$(OBJ)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_ALL) -MMD -MP -c $< -o $@

$(OBJ)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_ALL) -MMD -MP -c $< -o $@

$(OBJ)/tools/%.o: tools/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_ALL) -MMD -MP -c $< -o $@

$(OBJ)/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_ALL) -MMD -MP -c $< -o $@

$(OBJ)/tests/%.o: tests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_ALL) -DGIOCHI_ASSET_DIR='"$(CURDIR)/assets"' -MMD -MP -c $< -o $@

$(BIN)/giochi-init: $(INIT_OBJ) $(COMMON_OBJ)
	@mkdir -p $(BIN)
	$(CC) $^ -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS) $(CRYPTO_LIBS)

$(BIN)/giochi-musicd: $(MUSICD_OBJ) $(COMMON_OBJ)
	@mkdir -p $(BIN)
	$(CC) $^ -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS) $(CRYPTO_LIBS) $(ALSA_LIBS) $(MPG123_LIBS) $(FLAC_LIBS)

$(BIN)/giochi-launcher: $(LAUNCHER_OBJ) $(COMMON_OBJ)
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS) $(CRYPTO_LIBS) $(SDL2_LIBS)

$(BIN)/giochi-thumbgen: $(THUMBGEN_OBJ) $(COMMON_OBJ)
	@mkdir -p $(BIN)
	$(CC) $^ -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS) $(CRYPTO_LIBS) $(PNG_LIBS)

$(BIN)/giochi-verify: $(OBJ)/tools/giochi-verify.o $(COMMON_OBJ)
	@mkdir -p $(BIN)
	$(CC) $^ -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS) $(CRYPTO_LIBS)

$(BIN)/giochi-extract: $(OBJ)/tools/giochi-extract.o $(COMMON_OBJ)
	@mkdir -p $(BIN)
	$(CC) $^ -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS) $(CRYPTO_LIBS) $(ARCHIVE_LIBS)

$(BIN)/test_json: $(OBJ)/tests/test_json.o $(COMMON_OBJ)
	@mkdir -p $(BIN)
	$(CC) $^ -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS) $(CRYPTO_LIBS)

$(BIN)/test_sha256: $(OBJ)/tests/test_sha256.o $(COMMON_OBJ)
	@mkdir -p $(BIN)
	$(CC) $^ -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS) $(CRYPTO_LIBS)

$(BIN)/test_hotkey: $(OBJ)/tests/test_hotkey.o $(COMMON_OBJ)
	@mkdir -p $(BIN)
	$(CC) $^ -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS) $(CRYPTO_LIBS)

$(BIN)/test_musicd: $(OBJ)/tests/test_musicd.o $(OBJ)/musicd/musicd.o $(COMMON_OBJ)
	@mkdir -p $(BIN)
	$(CC) $^ -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS) $(CRYPTO_LIBS)

$(BIN)/test_transition: $(OBJ)/tests/test_transition.o $(COMMON_OBJ)
	@mkdir -p $(BIN)
	$(CC) $^ -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS) $(CRYPTO_LIBS)

$(BIN)/test_layout: $(OBJ)/tests/test_layout.o $(OBJ)/launcher/layout.o $(COMMON_OBJ)
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS) $(CRYPTO_LIBS)

$(BIN)/test_metadata: $(OBJ)/tests/test_metadata.o $(OBJ)/launcher/metadata.o $(COMMON_OBJ)
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS) $(CRYPTO_LIBS)

$(BIN)/test_buttonsmap: $(OBJ)/tests/test_buttonsmap.o $(OBJ)/launcher/input.o $(COMMON_OBJ)
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS) $(CRYPTO_LIBS)

$(BIN)/test_settings: $(OBJ)/tests/test_settings.o $(OBJ)/launcher/settings.o $(COMMON_OBJ)
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS) $(CRYPTO_LIBS)

# --- imagens por aparelho (delega para tools/build-image.sh) -------------
#   make images                  -> --check --all (nao compila)
#   make images BUILD=1          -> compila todos
#   make image DEVICE=x55        -> compila um aparelho
defconfigs:
	@sh tools/gen-defconfigs.sh

# Gera assets/bootlogo/{bootlogo.rgba,<aparelho>/splash.bmp} a partir de
# docs/logo.jpg. Requer Pillow; sem ele, avisa e nao falha o build.
screenshots:
	@python3 tools/make-screenshots.py

bootlogo:
	@python3 tools/make-bootlogo.py || { \
		rc=$$?; \
		if [ $$rc -eq 3 ]; then \
			echo "bootlogo: pulado (instale python3-pil)"; \
		else \
			exit $$rc; \
		fi; \
	}

images:
	@if [ "$(BUILD)" = "1" ]; then \
		sh tools/build-image.sh --all $(if $(FALLBACK),--fallback-dts,); \
	else \
		sh tools/build-image.sh --check --all; \
	fi

image:
	@sh tools/build-image.sh $(DEVICE) $(if $(FALLBACK),--fallback-dts,)

# --- lint: o que a CI roda antes de qualquer build -----------------------
lint:
	@echo "== shell =="
	@for f in scripts/init.d/S[0-9]* scripts/*.sh tools/*.sh \
	          buildroot/board/giochi/*.sh \
	          buildroot/board/giochi/giochi-dongle-hotplug \
	          buildroot/board/giochi/rootfs_overlay/etc/init.d/rc[SK] \
	          buildroot/board/giochi/rootfs_overlay/etc/giochi/lib.sh \
	          tests/integration/lifecycle_test.sh \
	          tests/integration/usbgadget_test.sh \
	          tests/integration/devmode_test.sh \
	          buildroot/package/giochi-mtpd/giochi-mtpd \
	          buildroot/package/giochi-mtpd/S60mtpd; do \
		[ -f "$$f" ] || continue; \
		sh -n "$$f" || { echo "ERRO de sintaxe: $$f"; exit 1; }; \
	done; echo "ok"
	@echo "== json =="
	@python3 -c 'import glob,json,sys; [json.load(open(p)) for p in glob.glob("assets/*.json")+glob.glob("assets/devices/*/*.json")+glob.glob("schemas/*.json")]; print("ok")'
	@echo "== defconfigs =="
	@sh tools/gen-defconfigs.sh --check
	@# Um bloco de DTS por defconfig: se o gerador casar o marcador tambem em
	@# comentario, o bloco sai duplicado e o Buildroot avisa "override:
	@# reassigning to symbol". Falhar aqui pega isso antes do build.
	@for f in buildroot/configs/*_defconfig; do \
		n=$$(grep -cE '^BR2_LINUX_KERNEL_INTREE_DTS_NAME=|^BR2_LINUX_KERNEL_CUSTOM_DTS_PATH=|^# DTS externo' "$$f"); \
		[ "$$n" -eq 1 ] || { echo "ERRO $$f: $$n blocos de DTS (esperado 1)"; exit 1; }; \
	done; echo "ok"
	@echo "== markdown =="
	@python3 tools/check-links.py
	@echo "== arquivos versionados =="
	@sh tools/check-tracked.sh .
	@echo "== simbolos do buildroot =="
	@tools/check-buildroot-symbols.py
	@echo "== u-boot/rkbin =="
	@tools/check-uboot.py
	@echo "== ferramentas python =="
	@python3 -m py_compile tools/make-screenshots.py \
	           tools/check-buildroot-symbols.py tools/check-uboot.py \
	           tools/check-links.py tools/make-bootlogo.py && echo "ok"

# 'all' antes dos testes: a suite de integracao roda o giochi-init REAL, e num
# clone limpo 'make test' sozinho nao o teria compilado.
test: all $(TEST_BINS)
	@sh tests/run_tests.sh "$(BIN)"

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(TARGETS) $(DESTDIR)$(BINDIR)/
	install -d $(DESTDIR)$(ASSETDIR)
	install -m 0644 assets/*.json assets/buttons.map $(DESTDIR)$(ASSETDIR)/
	install -d $(DESTDIR)/etc/giochi
	install -m 0644 assets/buttons.map $(DESTDIR)/etc/giochi/buttons.map
	install -d $(DESTDIR)/usr/sbin
	install -m 0755 scripts/giochi-usb-mode $(DESTDIR)/usr/sbin/giochi-usb-mode
	@if [ -f assets/bootlogo/bootlogo.rgba ]; then \
		install -m 0644 assets/bootlogo/bootlogo.rgba \
			$(DESTDIR)$(ASSETDIR)/bootlogo.rgba; \
		install -m 0644 assets/bootlogo/bootlogo.json \
			$(DESTDIR)$(ASSETDIR)/bootlogo.json; \
	fi

clean:
	rm -rf $(BUILD_DIR)

-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)
