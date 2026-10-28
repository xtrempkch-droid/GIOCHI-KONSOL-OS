# Arcade Volleyball - build de host (SDL2 via pkg-config).
#
#   make -C "Jogos/Arcade Volleyball"          # gera o binario
#   make -C "Jogos/Arcade Volleyball" clean
#
# A saida vai para build/host/bin (diretorio ja ignorado pelo git e aceito por
# tools/check-tracked.sh), e nao para dentro da pasta do jogo.
#
# Cross para o console (Buildroot): aponte o compilador e o pkg-config do
# sysroot, por exemplo
#   make CXX=aarch64-linux-g++ PKG_CONFIG=aarch64-linux-pkg-config

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

all: $(BIN)

$(BIN): $(SRC)
	@command -v $(PKG_CONFIG) >/dev/null 2>&1 || \
		{ echo "arcade-volleyball: pkg-config ausente" >&2; exit 1; }
	@$(PKG_CONFIG) --exists sdl2 || \
		{ echo "arcade-volleyball: SDL2 nao encontrada (instale libsdl2-dev)" >&2; exit 1; }
	@mkdir -p $(BINDIR)
	$(CXX) $(CXXSTD) $(OPT) $(WARN) $(SDL2_CFLAGS) $(SRC) -o $@ $(SDL2_LIBS)

clean:
	rm -f $(BIN)

.PHONY: all clean
