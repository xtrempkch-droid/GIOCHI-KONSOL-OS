#!/usr/bin/env python3
"""Gera o atlas da fonte embutida do Launcher.

O tema pede `builtin_sans` (assets/theme.json) e nao existe fonte no
repositorio. Duas saidas faceis eram erradas para este projeto:

  * depender de SDL2_ttf/freetype no console: ~1,4 MB de biblioteca + um .ttf
    de 700 KB para desenhar 60 rotulos curtos, e mais um simbolo de Kconfig para
    ser descartado em silencio (§3.2);
  * desenhar so retangulos: a tela fica sem nenhum rotulo e "nao subiu o
    Launcher" continua indistinguivel de "subiu e nao desenhou nada".

Entao o atlas e GERADO no PC e versionado, como assets/bootlogo/* e
docs/screenshots/*. O console so le bytes.

Formato (assets/font.a8 + assets/font.json):

  font.a8    imagem 8 bits por pixel (cobertura do glifo), uma faixa por
             tamanho, glifos em sequencia horizontal
  font.json  metrica por tamanho e a posicao de cada glifo

A cobertura (nao RGBA) deixa o arquivo 4x menor no SquashFS; o renderizador
expande para ARGB8888 uma unica vez no boot (~380 KB de RAM, dentro do teto de
4 MB da §4.1.6) e tinge com SDL_SetTextureColorMod, que e o que permite o mesmo
atlas servir para text_primary, text_secondary e accent.

Uso: tools/make-font.py [--out assets] [--font /usr/share/fonts/.../X.ttf]
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # pragma: no cover
    sys.exit("erro: Pillow nao instalado (pip install Pillow)")

# Tamanhos que a UI realmente pede (docs/LAYOUT-PROPOSTA.md §3):
#   painel base 640x480 -> aba 14, titulo 12, corpo 12
#   X55 1280x720 (x1,5) -> aba 21, titulo 18, corpo 18
SIZES = (12, 14, 18, 21)

FIRST_CP = 32
LAST_CP = 126
REPLACEMENT = 0xFFFD  # nao existe na faixa ASCII: vira um retangulo vazado

DEFAULT_FONTS = (
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    "/Library/Fonts/Arial.ttf",
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/arial.ttf",
)


def find_font(explicit: str | None) -> str:
    if explicit:
        if not pathlib.Path(explicit).is_file():
            sys.exit(f"erro: fonte '{explicit}' nao existe")
        return explicit
    for path in DEFAULT_FONTS:
        if pathlib.Path(path).is_file():
            return path
    sys.exit(
        "erro: nenhuma fonte TTF encontrada. Passe --font CAMINHO. "
        "O atlas e gerado no PC; o console nunca precisa da fonte."
    )


def build(font_path: str) -> tuple[bytes, dict]:
    """Monta o atlas e devolve (bytes A8, metadados)."""
    rows: list[dict] = []
    atlas_width = 0
    total_height = 0
    rendered: list[tuple[int, int, int, Image.Image, dict]] = []

    for px in SIZES:
        font = ImageFont.truetype(font_path, px)
        ascent, descent = font.getmetrics()
        line_height = ascent + descent

        cells = []
        pen_x = 1  # 1 px de folga, para nao amostrar a borda do vizinho
        glyphs: list[tuple[int, int, int, Image.Image, dict]] = []
        max_x = 0

        for cp in range(FIRST_CP, LAST_CP + 1):
            ch = chr(cp)
            advance = int(round(font.getlength(ch)))
            if advance <= 0:
                advance = max(1, px // 2)
            # A caixa do glifo pode ser mais larga que o avanco (italico,
            # acento); desenhamos dentro de max(advance, px) para nao cortar.
            cell_w = max(advance, 1)
            mask = Image.new("L", (cell_w + 2, line_height), 0)
            ImageDraw.Draw(mask).text((0, 0), ch, font=font, fill=255)
            cell = {"cp": cp, "x": pen_x, "advance": advance}
            cells.append(cell)
            glyphs.append((pen_x, line_height, cell_w, mask, cell))
            pen_x += cell_w + 1
            max_x = pen_x

        # Um glifo de substituicao: retangulo vazado, para texto desconhecido
        # (titulo de jogo em UTF-8) aparecer como "nao sei desenhar" em vez de
        # sumir calado.
        cell_w = max(px // 2, 6)
        mask = Image.new("L", (cell_w + 2, line_height), 0)
        d = ImageDraw.Draw(mask)
        d.rectangle([0, ascent - px // 2, cell_w - 1, ascent], outline=255)
        cell = {"cp": REPLACEMENT, "x": pen_x, "advance": cell_w}
        cells.append(cell)
        glyphs.append((pen_x, line_height, cell_w, mask, cell))
        pen_x += cell_w + 1
        max_x = pen_x

        row = {
            "px": px,
            "line_height": line_height,
            "ascent": ascent,
            "descent": descent,
            "y": total_height,
            "cells": cells,
        }
        rows.append(row)
        rendered.extend((y_off, w, line_h, m, c) for (y_off, line_h, w, m, c) in
                        [(total_height, line_height, w, m, c)
                         for (_, line_height, w, m, c) in glyphs])
        total_height += line_height
        atlas_width = max(atlas_width, max_x)

    atlas_width += 1
    image = Image.new("L", (atlas_width, total_height), 0)

    for y_off, line_h, w, mask, cell in rendered:
        # mask tem w+2 colunas; recorta para w para casar com o avanco/atlas.
        image.paste(mask.crop((0, 0, w, line_h)), (cell["x"], y_off))

    meta = {
        "schema_version": "1.0",
        "family": "builtin_sans",
        "width": atlas_width,
        "height": total_height,
        "first_cp": FIRST_CP,
        "last_cp": LAST_CP,
        "replacement_cp": REPLACEMENT,
        "sizes": rows,
    }
    return image.tobytes(), meta


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default="assets", help="diretorio de saida")
    ap.add_argument("--font", default=None, help="TTF a usar")
    args = ap.parse_args()

    font_path = find_font(args.font)
    data, meta = build(font_path)

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "font.a8").write_bytes(data)
    (out / "font.json").write_text(
        json.dumps(meta, indent=1) + "\n", encoding="utf-8")

    print(f"fonte: {font_path}")
    print(f"atlas: {meta['width']}x{meta['height']} = {len(data)} B "
          f"({len(SIZES)} tamanhos, {LAST_CP - FIRST_CP + 2} glifos cada)")
    for row in meta["sizes"]:
        print(f"  {row['px']} px: linha {row['line_height']}, "
              f"ascent {row['ascent']}, y {row['y']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
