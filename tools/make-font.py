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
expande para ARGB8888 uma unica vez no boot e tinge com
SDL_SetTextureColorMod, que e' o que permite o mesmo atlas servir para
text_primary, text_secondary e accent.

Custo medido da versao com dois pesos (2026-10-31): atlas 2899x158 = 458 042 B
(68 KiB no SquashFS, xz com o json) e textura ARGB8888 de **1,75 MiB** em
RAM/VRAM — dentro do teto de 4 MB da §4.1.6, com 2,25 MB de folga.

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

# (nome, candidatos do TTF). O nome e' o que vai no font.json e o que o
# render/`preview` pedem; a ordem aqui e' a ordem das faixas no atlas.
WEIGHTS = (
    ("regular", (
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/Library/Fonts/Arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
    )),
    ("bold", (
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
        "/Library/Fonts/Arial Bold.ttf",
        "C:/Windows/Fonts/segoeuib.ttf",
        "C:/Windows/Fonts/arialbd.ttf",
    )),
)

# ASCII imprimivel + Latin-1 Supplement. A segunda faixa nao e' luxo: o
# repositorio e' em portugues, e "Inicio", "Musica", "Configuracoes" tem acento.
# Sem ela o texto sai cheio de caixas vazadas — e o atlas e' a unica fonte que o
# console tem.
RANGES = ((32, 126), (160, 255))
REPLACEMENT = 0xFFFD  # fora das faixas: vira um retangulo vazado

def resolvel_weight(nome: str, candidatos: tuple[str, ...],
                    explicito: str | None) -> str:
    """Caminho do TTF de um peso.

    `--font` explicito so' vale para `regular`: derivar o negrito de um caminho
    arbitrario e' adivinhacao. Se o negrito nao existir, o gerador usa o
    regular e AVISA — atlas sem negrito e' melhor que atlas nenhum, mas nao
    pode passar calado.
    """
    if explicito and nome == "regular":
        if not pathlib.Path(explicito).is_file():
            sys.exit(f"erro: fonte '{explicito}' nao existe")
        return explicito
    for caminho in candidatos:
        if pathlib.Path(caminho).is_file():
            return caminho
    return ""


def render_face(font: ImageFont.FreeTypeFont, px: int) -> tuple[Image.Image, dict]:
    """Desenha todos os glifos de um tamanho e devolve (faixa, metrica).

    Cada glifo e' desenhado num bloco cuja LARGURA e' max(avanco, tinta): 22
    glifos do DejaVu passam 1 px do proprio avanco (A, K, T, V, f, r, ...), e
    recortar no avanco come a ponta do 'A'. A altura do bloco tem de ser a
    linha inteira — era aqui que o atlas antigo perdia a METADE DE BAIXO de
    todas as letras (o par (w, line_h) saia trocado na montagem e o recorte
    ficava 22x11 em vez de 13x22). Ver AGENTS.md §3.72.
    """
    ascent, descent = font.getmetrics()
    line_height = ascent + descent

    codepoints = [cp for lo, hi in RANGES for cp in range(lo, hi + 1)]
    codepoints.append(REPLACEMENT)

    blocos: list[tuple[Image.Image, dict]] = []
    pen_x = 1  # 1 px de folga, para nao amostrar a borda do vizinho
    max_x = pen_x

    for cp in codepoints:
        if cp == REPLACEMENT:
            advance = max(px // 2, 6)
            block_w = advance
            mask = Image.new("L", (block_w, line_height), 0)
            ImageDraw.Draw(mask).rectangle(
                [0, ascent - px // 2, block_w - 1, ascent], outline=255)
        else:
            ch = chr(cp)
            advance = int(round(font.getlength(ch)))
            if advance <= 0:
                advance = max(1, px // 2)
            # Espaco em branco ainda precisa de bloco, senao o avanco nao
            # corresponde a nenhuma coluna do atlas.
            sonda = Image.new("L", (advance + px, line_height), 0)
            ImageDraw.Draw(sonda).text((0, 0), ch, font=font, fill=255)
            tinta = sonda.getbbox()
            block_w = max(advance, tinta[2] if tinta else 0)
            mask = Image.new("L", (block_w, line_height), 0)
            ImageDraw.Draw(mask).text((0, 0), ch, font=font, fill=255)

        cell = {"cp": cp, "x": pen_x, "w": block_w, "advance": advance}
        blocos.append((mask, cell))
        pen_x += block_w + 1
        max_x = pen_x

    faixa = Image.new("L", (max_x, line_height), 0)
    for mask, cell in blocos:
        faixa.paste(mask, (cell["x"], 0))

    meta = {
        "line_height": line_height,
        "ascent": ascent,
        "descent": descent,
        "cells": [cell for _, cell in blocos],
    }
    return faixa, meta


def build(explicito: str | None) -> tuple[bytes, dict]:
    """Monta o atlas e devolve (bytes A8, metadados)."""
    caminhos: dict[str, str] = {}

    for nome, candidatos in WEIGHTS:
        caminho = resolvel_weight(nome, candidatos, explicito)

        if not caminho:
            if nome == "regular":
                sys.exit(
                    "erro: nenhuma fonte TTF encontrada. Passe --font CAMINHO. "
                    "O atlas e gerado no PC; o console nunca precisa da fonte.")
            print(f"aviso: sem TTF '{nome}'; usando o regular no lugar",
                  file=sys.stderr)
            caminho = caminhos["regular"]
        caminhos[nome] = caminho

    faixas: list[tuple[str, int, Image.Image, dict]] = []
    largura = 0
    altura = 0

    for nome, _ in WEIGHTS:
        caminho = caminhos[nome]

        for px in SIZES:
            fonte = ImageFont.truetype(caminho, px)
            faixa, meta = render_face(fonte, px)
            faixas.append((nome, px, faixa, meta))
            largura = max(largura, faixa.width)
            altura += faixa.height

    largura += 1
    imagem = Image.new("L", (largura, altura), 0)

    rows: list[dict] = []
    y = 0

    for nome, px, faixa, meta in faixas:
        imagem.paste(faixa, (0, y))
        rows.append({
            "px": px,
            "weight": nome,
            "font": pathlib.Path(caminhos[nome]).name,
            "y": y,
            "line_height": meta["line_height"],
            "ascent": meta["ascent"],
            "descent": meta["descent"],
            "cells": meta["cells"],
        })
        y += faixa.height

    largura = imagem.width
    metadados = {
        "schema_version": "2.0",
        "family": "builtin_sans",
        "width": largura,
        "height": altura,
        "ranges": [list(r) for r in RANGES],
        "replacement_cp": REPLACEMENT,
        "weights": [nome for nome, _ in WEIGHTS],
        "sizes": rows,
    }
    return imagem.tobytes(), metadados


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default="assets", help="diretorio de saida")
    ap.add_argument("--font", default=None, help="TTF a usar")
    args = ap.parse_args()

    data, meta = build(args.font)

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "font.a8").write_bytes(data)
    (out / "font.json").write_text(
        json.dumps(meta, indent=1) + "\n", encoding="utf-8")

    total = sum(hi - lo + 1 for lo, hi in RANGES) + 1
    print(f"atlas: {meta['width']}x{meta['height']} = {len(data)} B "
          f"({len(SIZES)} tamanhos x {len(WEIGHTS)} pesos, {total} glifos)")
    for row in meta["sizes"]:
        print(f"  {row['weight']:7s} {row['px']:2d} px: linha "
              f"{row['line_height']}, ascent {row['ascent']}, "
              f"y {row['y']}, {row['font']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
