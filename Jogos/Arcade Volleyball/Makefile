# Arcade Volleyball - build de host (SDL2 via pkg-config).
#
#   make -C "Jogos/Arcade Volleyball"          # gera o binario (host)
#   make -C "Jogos/Arcade Volleyball" console  # gera para o APARELHO (aarch64)
#   make -C "Jogos/Arcade Volleyball" clean
#
# A saida vai para build/host/bin (host) ou dist/<id>/game-stage (console), e
# nao para dentro da pasta do jogo.
#
# POR QUE O ALVO `console` EXISTE
# -------------------------------
# O projeto Godot deste jogo vai na imagem, mas NAO roda no aparelho: nao ha
# engine Godot no rootfs e nao existe export template aarch64-musl (ADR-03).
# Quem roda hoje e' este port nativo em C++/SDL2 — o SDL2 do aparelho e' o
# mesmo que o Launcher usa (KMSDRM sobre /dev/dri/card0).
#
# O binario sai com ~80 KB e depende de libSDL2, libstdc++, libgcc_s e libc,
# todas ja' presentes no rootfs (verificado).

CXX         ?= c++
CXXSTD      ?= -std=c++17
OPT         ?= -O2 -g
WARN        ?= -Wall -Wextra -Wshadow -Wformat=2 -Wundef
PKG_CONFIG  ?= pkg-config

SDL2_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null)
SDL2_LIBS   ?= $(shell $(PKG_CONFIG) --libs sdl2 2>/dev/null)

BINDIR ?= ../../build/host/bin
BIN    := $(BINDIR)/arcade-volleyball
SRC    := arcade_volleyball.cpp

# --- alvo console -------------------------------------------------------
# MONTAGEM=1 escolhe o kit do Buildroot em dist/<id>/build/host. O sysroot ja'
# tem o sdl2.pc, entao o proprio pkg-config resolve as flags; nao ha caminho
# de include escrito a mao aqui (a licao do AGENTS.md §3.126).
GAME_DEVICE ?= x55
HOSTTREE    ?= ../../dist/$(GAME_DEVICE)/build/host
TUPLE       ?= aarch64-buildroot-linux-musl
SYSROOT     := $(abspath $(HOSTTREE)/$(TUPLE)/sysroot)
CONSOLE_BIN := $(abspath ../../dist/$(GAME_DEVICE)/game-stage)/arcade-volleyball

all: $(BIN)

$(BIN): $(SRC)
	@command -v $(PKG_CONFIG) >/dev/null 2>&1 || \
		{ echo "arcade-volleyball: pkg-config ausente" >&2; exit 1; }
	@$(PKG_CONFIG) --exists sdl2 || \
		{ echo "arcade-volleyball: SDL2 nao encontrada (instale libsdl2-dev)" >&2; exit 1; }
	@mkdir -p $(BINDIR)
	$(CXX) $(CXXSTD) $(OPT) $(WARN) $(SDL2_CFLAGS) $(SRC) -o $@ $(SDL2_LIBS)

console:
	@[ -d "$(SYSROOT)/usr/include/SDL2" ] || { \
	    echo "arcade-volleyball: sysroot do alvo ausente em $(SYSROOT)" >&2; \
	    echo "  construa a imagem uma vez: tools/build-image.sh $(GAME_DEVICE)" >&2; \
	    exit 1; }
	@mkdir -p $(dir $(CONSOLE_BIN))
	$(abspath $(HOSTTREE)/bin/$(TUPLE)-g++) $(CXXSTD) $(OPT) $(WARN) \
	    -D_GNU_SOURCE -isystem $(SYSROOT)/usr/include/SDL2 \
	    $(SRC) -o $(CONSOLE_BIN) \
	    $(SYSROOT)/usr/lib/libSDL2-2.0.so.0
	@echo "arcade-volleyball: $(CONSOLE_BIN)"
	@file -b $(CONSOLE_BIN) | cut -d, -f1-2

clean:
	rm -f $(BIN) $(CONSOLE_BIN)

.PHONY: all clean console

