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

# Identidade do aparelho e da versao. Os valores chegam SEM aspas (o pacote
# Buildroot passa GIOCHI_SOC=rk3566 na linha de comando do make); as aspas sao
# aplicadas aqui, numa unica camada de shell. Fazer isso no .mk nao funcionava:
# o EXTRA_CFLAGS atravessa duas camadas e o shell comia as aspas, deixando o
# compilador com o simbolo rk3566 em vez da string "rk3566".
GIOCHI_SOC            ?= desconhecido
GIOCHI_DEVICE         ?= desconhecido
GIOCHI_OS_VERSION     ?= 0.0
GIOCHI_KERNEL_VERSION ?= 0.0
GIOCHI_PROTON         ?= 0

DEFS += -DGIOCHI_SOC='"$(GIOCHI_SOC)"' \
        -DGIOCHI_DEVICE='"$(GIOCHI_DEVICE)"' \
        -DGIOCHI_OS_VERSION='"$(GIOCHI_OS_VERSION)"' \
        -DGIOCHI_KERNEL_VERSION='"$(GIOCHI_KERNEL_VERSION)"' \
        -DGIOCHI_ENABLE_PROTON=$(GIOCHI_PROTON)

INCS := -Isrc/common -Isrc/init -Iapps/launcher -Isrc/musicd

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

# Flags para o gerador do compile_commands.json (alvo `compdb`). Vao por
# `export` do proprio make: o valor chega ao processo filho por execve, sem
# passar por um shell. Numa recipe comum as aspas simples de DEFS fecham as
# aspas da recipe e o `-DGIOCHI_SOC='"x55"'` chega como `-DGIOCHI_SOC=x55` —
# o define de TEXTO vira simbolo, exatamente a armadilha que o
# tools/check-image-contents.sh vigia nos .mk (AGENTS.md §3.111).
export GIOCHI_CC := $(CC)
export GIOCHI_CXX := $(CXX)
export GIOCHI_CFLAGS := $(CFLAGS_ALL)
export GIOCHI_CXXFLAGS := $(CXXFLAGS_ALL)
export GIOCHI_ASSET_DIR := $(CURDIR)/assets

OBJ := $(BUILD_ROOT)/obj
BIN := $(BUILD_ROOT)/bin

# --- migracao de layout de fontes -------------------------------------------
# O Launcher saiu de src/launcher/ para apps/launcher/ (virou aplicativo). O
# build incremental guarda .d/.o apontando para o caminho ANTIGO, e o make
# aborta na LEITURA do Makefile — antes de qualquer compilacao — com:
#
#   make: *** Sem regra para processar o alvo 'src/launcher/main.cpp',
#   necessario por 'build/host/obj/launcher/main.o'.  Pare.
#
# Limpar so' o diretorio do launcher resolveria aqui e agora, mas os .d dos
# TESTES tambem citam src/launcher/*.hpp. O criterio correto e' o conteudo:
# .d que cita o caminho aposentado e' lixo. Ele sai JUNTO com o .o
# correspondente — apagar apenas o .d esconderia dependencia de header ate' a
# proxima recompilacao, que e' a classe de defeito silencioso do §3.33.
# Carimbo para o custo ser zero a partir da segunda invocacao.
LAYOUT_STAMP := $(BUILD_DIR)/.layout-launcher-apps
$(shell if [ ! -f '$(LAYOUT_STAMP)' ]; then \
          find '$(BUILD_DIR)' -name '*.d' \
               -exec grep -ls 'src/launcher/' {} + 2>/dev/null \
          | while IFS= read -r d; do rm -f "$$d" "$${d%.d}.o"; done; \
          mkdir -p '$(dir $(LAYOUT_STAMP))' 2>/dev/null; \
          : > '$(LAYOUT_STAMP)' 2>/dev/null; \
      fi)

COMMON_SRC := \
	src/common/giochi_json.c \
	src/common/giochi_log.c \
	src/common/giochi_util.c \
	src/common/giochi_sock.c \
	src/common/giochi_transition.c \
	src/common/giochi_hotkey.c \
	src/common/giochi_sha256.c \
	src/common/giochi_ed25519.c \
	src/common/giochi_deviceconf.c

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

# O Launcher e' um APLICATIVO, nao nucleo do sistema: mora em apps/ (como o
# forge e o store), para poder ser mexido sem tocar no sistema nem nos jogos.
# O prefixo do patsubst e' apps/ justamente para o objeto continuar saindo em
# $(OBJ)/launcher/*.o — as regras dos testes e do dump-geometry dependem disso.
LAUNCHER_SRC := \
	apps/launcher/main.cpp \
	apps/launcher/layout.cpp \
	apps/launcher/theme.cpp \
	apps/launcher/settings.cpp \
	apps/launcher/overlay.cpp \
	apps/launcher/input.cpp \
	apps/launcher/evdev.cpp \
	apps/launcher/ipc_client.cpp \
	apps/launcher/metadata.cpp \
	apps/launcher/library.cpp \
	apps/launcher/ui_geometry.cpp \
	apps/launcher/font.cpp \
	apps/launcher/i18n.cpp \
	apps/launcher/status.cpp \
	apps/launcher/music.cpp \
	apps/launcher/render.cpp \
	apps/launcher/render_null.cpp

LAUNCHER_OBJ := $(patsubst apps/%.cpp,$(OBJ)/%.o,$(LAUNCHER_SRC))

THUMBGEN_SRC := src/thumbgen/main.c
THUMBGEN_OBJ := $(patsubst src/%.c,$(OBJ)/%.o,$(THUMBGEN_SRC))

TOOLS_SRC := tools/giochi-verify.c tools/giochi-extract.c
TOOLS_OBJ := $(patsubst tools/%.c,$(OBJ)/tools/%.o,$(TOOLS_SRC))

# Cliente do control.sock que roda NO CONSOLE. Fica em src/tools/ (junto do
# gpu_probe) e nao em tools/ como o verify/extract: o pacote giochi-tools ja'
# o compila daquele caminho, e o build da imagem e' a fonte da verdade.
# Regra explicita porque $(OBJ)/tools/%.o: tools/%.c nao casa src/tools/, e
# deixar cair no padrao generico $(OBJ)/%.o: src/%.c funcionaria por
# coincidencia — o make so' usaria esse ramo depois de falhar no outro.
CTL_SRC := src/tools/giochi-ctl.c
CTL_OBJ := $(OBJ)/tools/giochi-ctl.o

TARGETS := \
	$(BIN)/giochi-init \
	$(BIN)/giochi-musicd \
	$(BIN)/giochi-launcher \
	$(BIN)/giochi-thumbgen \
	$(BIN)/giochi-verify \
	$(BIN)/giochi-extract \
	$(BIN)/giochi-ctl

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
	tests/test_buttonsmap.cpp \
	tests/test_deviceconf.cpp \
	tests/test_ui_geometry.cpp \
	tests/test_i18n.cpp \
	tests/test_status.cpp \
	tests/test_music_ui.cpp

TEST_BINS := $(patsubst tests/%.c,$(BIN)/%,$(TESTS_C)) \
             $(patsubst tests/%.cpp,$(BIN)/%,$(TESTS_CXX))

.PHONY: all clean test install info check-config defconfigs lint images tema-galeria \
        bootlogo screenshots proposta font ui-preview compdb \
        $(BIN)/dump-geometry

all: $(TARGETS) compdb

# compile_commands.json: o editor nao le o Makefile, e sem este arquivo ele
# ADIVINHA os includes. O sintoma nao e' um erro de build — e' um erro que so'
# existe no editor:
#
#   apps/launcher/input.hpp:12: 'giochi_hotkey.h' file not found
#
# O header esta' em src/common/ e o `-Isrc/common` esta' aqui. A saida comum
# (copiar a lista de includes para o .vscode) seria uma SEGUNDA copia das
# flags: no dia em que um diretorio mudar, o build quebra e a gente conserta, e
# o editor fica apontando para o lugar antigo. As flags vem por variavel de
# ambiente do PROPRIO Makefile (§3.20), e as listas de fontes tambem.
#
# Depende de `all` para o arquivo nunca ficar velho: foi exatamente um MOVE
# (src/launcher -> apps/launcher) que produziu o erro acima. O arquivo e'
# gerado, entao esta' no .gitignore.
compdb:
	@if ! command -v python3 >/dev/null 2>&1; then \
	    echo "compdb: python3 ausente; compile_commands.json nao gerado"; \
	    exit 0; \
	fi; \
	python3 tools/gen-compdb.py \
	    --c $(COMMON_SRC) $(INIT_SRC) $(MUSICD_SRC) $(THUMBGEN_SRC) \
	        $(TOOLS_SRC) $(CTL_SRC) src/tools/gpu_probe.c $(TESTS_C) \
	    --cxx $(LAUNCHER_SRC) tools/dump-geometry.cpp \
	    --test-cxx $(TESTS_CXX)

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

# Apps em C++ (apps/launcher). O objeto sai em $(OBJ)/launcher/*.o, igual aos
# outros: o prefixo e' so' de onde o fonte vem.
$(OBJ)/%.o: apps/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_ALL) -MMD -MP -c $< -o $@

$(OBJ)/tools/%.o: tools/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_ALL) -MMD -MP -c $< -o $@

$(OBJ)/tools/%.o: tools/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_ALL) -MMD -MP -c $< -o $@

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

$(OBJ)/tools/giochi-ctl.o: $(CTL_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_ALL) -MMD -MP -c $< -o $@

$(BIN)/giochi-ctl: $(CTL_OBJ) $(COMMON_OBJ)
	@mkdir -p $(BIN)
	$(CC) $^ -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS) $(CRYPTO_LIBS)

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

$(BIN)/dump-geometry: $(OBJ)/tools/dump-geometry.o $(OBJ)/launcher/ui_geometry.o $(OBJ)/launcher/layout.o $(OBJ)/launcher/i18n.o $(OBJ)/launcher/settings.o $(OBJ)/launcher/theme.o $(OBJ)/launcher/overlay.o $(OBJ)/launcher/music.o $(COMMON_OBJ)
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS) $(CRYPTO_LIBS)

$(BIN)/test_i18n: $(OBJ)/tests/test_i18n.o $(OBJ)/launcher/i18n.o $(OBJ)/launcher/settings.o $(COMMON_OBJ)
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS) $(CRYPTO_LIBS)

$(BIN)/test_music_ui: $(OBJ)/tests/test_music_ui.o $(OBJ)/launcher/music.o $(OBJ)/common/giochi_json.o
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS)

$(BIN)/test_status: $(OBJ)/tests/test_status.o $(OBJ)/launcher/status.o $(COMMON_OBJ)
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS) $(CRYPTO_LIBS)

$(BIN)/test_ui_geometry: $(OBJ)/tests/test_ui_geometry.o $(OBJ)/launcher/ui_geometry.o $(OBJ)/launcher/layout.o $(COMMON_OBJ)
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS) $(EXTRA_LDFLAGS) $(CRYPTO_LIBS)

$(BIN)/test_deviceconf: $(OBJ)/tests/test_deviceconf.o $(COMMON_OBJ)
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

# Renders da PROPOSTA de layout (docs/proposta/): as 5 abas em 640x480 e
# 1280x720, desenhadas nativamente. Nao e imagem do produto — e o alvo descrito
# em docs/LAYOUT-PROPOSTA.md. O script falha (exit 1) se alguma assercao
# geometrica quebrar: texto que nao cabe, item de hint bar que colide.
proposta:
	@python3 tools/make-layout-proposal.py

bootlogo:
	@python3 tools/make-bootlogo.py || { \
		rc=$$?; \
		if [ $$rc -eq 3 ]; then \
			echo "bootlogo: pulado (instale python3-pil)"; \
		else \
			exit $$rc; \
		fi; \
	}

font:
	@python3 tools/make-font.py || { \
		rc=$$?; \
		if [ $$rc -eq 3 ]; then \
			echo "font: pulado (instale python3-pil)"; \
		else \
			exit $$rc; \
		fi; \
	}

# Preview do que o Launcher desenha: a geometria vem do ui_geometry.cpp (via
# dump-geometry, sem SDL) e o texto do MESMO atlas que vai para a imagem.
# Nao ha segundo conjunto de numeros de layout neste caminho.
ui-preview: $(BIN)/dump-geometry
	@python3 tools/preview-ui.py --size 1280x720 --tab 1 --items 15 --focus 0 \
		--out docs/proposta/ui-1280x720.png
	@python3 tools/preview-ui.py --size 640x480 --tab 1 --items 6 --focus 1 \
		--out docs/proposta/ui-640x480.png
	@python3 tools/preview-ui.py --size 1280x720 --tab 2 --items 0 \
		--out docs/proposta/ui-home-1280x720.png
	@python3 tools/preview-ui.py --size 1280x720 --tab 1 --items 0 \
		--out docs/proposta/ui-vazia-1280x720.png
	@python3 tools/preview-ui.py --size 1280x720 --tab 1 --settings 1 --level 1 \
		--out docs/proposta/ui-config-1280x720.png
	@python3 tools/preview-ui.py --size 1280x720 --lang en --tab 1 --items 15 \
		--out docs/proposta/ui-1280x720-en.png
	@python3 tools/preview-ui.py --size 1280x720 --tab 0 \
		--out docs/proposta/ui-musica-1280x720.png
	@python3 tools/preview-ui.py --size 640x480 --tab 0 \
		--out docs/proposta/ui-musica-640x480.png

# Galeria dos temas (docs/temas/): uma imagem por tema e por painel, mais o
# contato 2x3 para comparar. Nada aqui e' desenhado pelo script: ele chama o
# preview, que le a geometria do dump-geometry (mesma regra do ui-preview).
tema-galeria: $(BIN)/dump-geometry
	@python3 tools/make-theme-gallery.py

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
	          "Jogos/Arcade Volleyball/godot/tests/run_tests.sh" \
	          "Jogos/Spacewar/godot/tests/run_tests.sh" \
	          "Jogos/Colossal Cave Adventure/godot/tests/run_tests.sh" \
	          tools/bench-games.sh \
	          buildroot/package/giochi-games/install-games.sh \
	          buildroot/package/giochi-mtpd/giochi-mtpd \
	          buildroot/package/giochi-mtpd/S60mtpd; do \
		[ -f "$$f" ] || continue; \
		sh -n "$$f" || { echo "ERRO de sintaxe: $$f"; exit 1; }; \
	done; echo "ok"
	@echo "== jogos padrao =="
	@python3 tools/check-default-games.py
	@echo "== engines =="
	@# Catalogo de engines + recomendacao por aparelho. Tambem confere que
	@# devices/<id>.conf, a tabela por SoC do device_profile.c e os perfis
	@# manuais do forge dizem a MESMA capacidade grafica: quando discordavam,
	@# o RK3326 saia como panfrost/GLES 3.2 e a Loja recomendava engine errada.
	@python3 tools/check-engines.py --quiet
	@echo "== json =="
	@python3 -c 'import glob,json,sys; [json.load(open(p)) for p in glob.glob("assets/*.json")+glob.glob("assets/themes/*.json")+glob.glob("device/*/layout/*.json")+glob.glob("device/*/*.json")+glob.glob("schemas/*.json")]; print("ok")'
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
	@echo "== conteudo da imagem =="
	@sh tools/check-image-contents.sh
	@echo "== tabela de dispositivos =="
	@sh tools/check-device-table.sh
	@echo "== layout do cartao =="
	@python3 -m unittest tests.test_image_layout 2>&1 | tail -3
	@echo "== log de boot e dispositivo de boot =="
	@python3 -m unittest tests.test_boot_log 2>&1 | tail -3
	@echo "== verificador de cartao =="
	@python3 -m unittest tests.test_verify_card 2>&1 | tail -3
	@echo "== painel do DTS por aparelho =="
	@tools/check-dts-panel.py
	@echo "== config pedida x aplicada =="
	@python3 -m unittest tests.test_config_aplicada 2>&1 | tail -3
	@echo "== patches locais de kernel =="
	@python3 -m unittest tests.test_kernel_patch 2>&1 | tail -3
	@echo "== proposta de layout =="
	@python3 -m unittest tests.test_layout_proposal 2>&1 | tail -3
	@echo "== escala do override por aparelho =="
	@python3 -m unittest tests.test_device_scale 2>&1 | tail -3
	@echo "== catalogo de temas =="
	@$(MAKE) --no-print-directory $(BIN)/dump-geometry >/dev/null 2>&1 || true; \
	if [ -x "$(BIN)/dump-geometry" ]; then \
	    python3 -m unittest tests.test_themes 2>&1 | tail -3; \
	else \
	    echo "  PULADA: $(BIN)/dump-geometry ausente (sem compilador no host?)"; \
	fi
	@echo "== protocolo do app (control.sock) =="
	@python3 -m unittest tests.test_control_protocol 2>&1 | grep -E "Ran|OK|FAILED" | tail -2
	@echo "== cliente do control.sock (por SSH) =="
	@python3 -m unittest tests.test_giochi_ctl 2>&1 | grep -E "Ran|OK|FAILED" | tail -2
	@echo "== modo desenvolvedor no cartao =="
	@python3 -m unittest tests.test_dev_mode_card 2>&1 | grep -E "Ran|OK|FAILED" | tail -2
	@echo "== diagnostico de usb =="
	@python3 -m unittest tests.test_usb_diag 2>&1 | grep -E "Ran|OK|FAILED" | tail -2
	@echo "== gravacao do cartao =="
	@python3 -m unittest tests.test_flash_card 2>&1 | grep -E "Ran|OK|FAILED" | tail -2
	@echo "== app de loja do PC =="
	@python3 -m unittest tests.test_store 2>&1 | grep -E "Ran|OK|FAILED" | tail -2
	@echo "== fonte embutida (atlas gerado) =="
	@python3 tests/test_font.py
	@echo "== desenho SDL2 compila (mesmo sem SDL2 no host) =="
	@sh tools/check-sdl2-compile.sh
	@echo "== cliente do control.sock do console (giochi-ctl) =="
	@$(CC) -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wshadow -Wformat=2 -Werror \
	    -Isrc/common -o /tmp/giochi-ctl-lint \
	    src/tools/giochi-ctl.c src/common/giochi_sock.c src/common/giochi_util.c \
	    && rm -f /tmp/giochi-ctl-lint && echo "ok"
	@echo "== cliente do control.sock: comportamento e protocolo =="
	@$(MAKE) --no-print-directory $(BIN)/giochi-ctl >/dev/null 2>&1 || true; \
	if [ -x "$(BIN)/giochi-ctl" ]; then \
	    python3 -m unittest tests.test_giochi_ctl_cliente_c 2>&1 | grep -E "Ran|OK|FAILED" | tail -2; \
	else \
	    echo "  PULADA: $(BIN)/giochi-ctl ausente (sem compilador no host?)"; \
	fi
	@echo "== config do editor (includes x compile_commands.json) =="
	@$(MAKE) --no-print-directory compdb >/dev/null 2>&1 || true; \
	python3 -m unittest tests.test_editor_config 2>&1 | grep -E "Ran|OK|FAILED" | tail -2
	@echo "== ferramentas python =="
	@python3 -m py_compile tools/make-screenshots.py \
	           tools/make-layout-proposal.py \
	           tools/check-buildroot-symbols.py tools/check-uboot.py \
	           tools/check-links.py tools/make-bootlogo.py \
	           tools/serial-capture.py tools/verify-card.py \
	           tools/check-dts-panel.py tools/make-font.py \
	           tools/preview-ui.py tools/make-theme-gallery.py \
	           tools/giochi-ctl.py tools/gen-compdb.py \
	           apps/store/giochi_store/*.py apps/forge/giochi_forge/*.py \
	           && echo "ok"

# 'all' antes dos testes: a suite de integracao roda o giochi-init REAL, e num
# clone limpo 'make test' sozinho nao o teria compilado.
#
# dump-geometry entra na MESMA conta (AGENTS.md §3.7): a suite 'temas' mede o
# efeito real do tema na grade executando esse binario, e o tests/run_tests.sh
# a PULA quando ele nao existe. Num clone limpo ela simplesmente desaparecia
# da contagem (40 -> 39 suites, verde do mesmo jeito) -- cobertura perdida em
# silencio, que e' exatamente o que este repositorio trata como defeito.
test: all $(TEST_BINS) $(BIN)/dump-geometry
	@sh tests/run_tests.sh "$(BIN)"

# Os jogos em Godot nao entram em `make test`: o motor nao e dependencia do
# repositorio (como SDL2/ALSA, ele e opcional no host). Cada runner sai com 0 e
# avisa quando nao encontra o Godot; use GODOT=/caminho/do/binario.
# Bench: quadros medidos por jogo e preset fixo (vazio = preset salvo).
FRAMES ?=
QUALITY ?=

# Os caminhos tem ESPACO ("Arcade Volleyball"): um `for` sobre $(GODOT_GAMES)
# parte o nome no meio e o runner recebe "Jogos/Arcade". Por isso os dois sao
# chamados explicitamente, com aspas.
godot-test:
	@echo "== Arcade Volleyball =="
	@GODOT=$(GODOT) sh "Jogos/Arcade Volleyball/godot/tests/run_tests.sh"
	@echo "== Spacewar =="
	@GODOT=$(GODOT) sh "Jogos/Spacewar/godot/tests/run_tests.sh"
	@echo "== Colossal Cave =="
	@GODOT=$(GODOT) sh "Jogos/Colossal Cave Adventure/godot/tests/run_tests.sh"

# Bench dos jogos padrao (assets/default-games.json): roda cada jogo no modo
# --bench e escreve um laudo JSON por jogo em build/bench/. E o mesmo comando
# no PC e no aparelho -- ver docs/GAMES.md.
#   make bench                 # 1800 quadros (~30 s) por jogo
#   make bench FRAMES=3600 QUALITY=2
bench:
	@GODOT=$(GODOT) OUT=$(BUILD_DIR)/bench FRAMES=$(FRAMES) QUALITY=$(QUALITY) \
		sh tools/bench-games.sh

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
