#!/usr/bin/env python3
"""Desenha o que o Launcher vai desenhar, no PC, para inspecao visual.

Por que isto existe: o render.cpp usa SDL2/KMSDRM, que precisa de /dev/dri. Nao
ha como olhar a tela do aparelho sem o aparelho, e "compilou" nao e "esta certo"
— o defeito do §3.44 (tela apagada) e do Launcher sem driver de video so
apareceram no cartao, depois de um build de 40 min cada.

Entao a geometria vem do MESMO ui_geometry.cpp (via tools/dump-geometry, que nao
tem SDL) e o texto vem do MESMO atlas (assets/font.a8 + font.json) que vai para
a imagem. Este script nao tem nenhum numero de layout: ele so pinta o JSON que o
C++ imprimiu. Se a imagem sair errada, o render.cpp tambem vai sair.

Uso:
  tools/preview-ui.py                      # X55 1280x720, aba Biblioteca
  tools/preview-ui.py --size 640x480       # painel base
  tools/preview-ui.py --tab 2 --items 0    # Home
"""
from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
import tempfile

try:
    from PIL import Image, ImageDraw
except ImportError:  # pragma: no cover
    sys.exit("erro: Pillow nao instalado (pip install Pillow)")

ROOT = pathlib.Path(__file__).resolve().parent.parent

# Cores de assets/theme.json (dark_default). Lidas do arquivo, nao fixadas.
def load_theme() -> dict[str, str]:
    data = json.loads((ROOT / "assets" / "theme.json").read_text(encoding="utf-8"))
    return dict(data["colors"])


class Atlas:
    """Le assets/font.a8 + font.json — o mesmo par que o console le."""

    def __init__(self, base: pathlib.Path) -> None:
        self.meta = json.loads((base / "font.json").read_text(encoding="utf-8"))
        raw = (base / "font.a8").read_bytes()
        self.w = int(self.meta["width"])
        self.h = int(self.meta["height"])
        if len(raw) < self.w * self.h:
            raise SystemExit("font.a8 truncado")
        self.image = Image.frombytes("L", (self.w, self.h), raw)
        self.faces = {int(s["px"]): s for s in self.meta["sizes"]}

    def face(self, px: int) -> dict:
        if px in self.faces:
            return self.faces[px]
        return min(self.faces.values(), key=lambda s: abs(int(s["px"]) - px))

    def measure(self, text: str, px: int) -> int:
        face = self.face(px)
        adv = {int(c["cp"]): int(c["advance"]) for c in face["cells"]}
        return sum(adv.get(ord(ch), adv.get(0xFFFD, px // 2)) for ch in text)

    def truncate(self, text: str, px: int, max_w: int) -> str:
        if max_w <= 0 or self.measure(text, px) <= max_w:
            return text
        ell = self.measure("...", px)
        out = ""
        for ch in text:
            if self.measure(out + ch, px) + ell > max_w:
                break
            out += ch
        return out + "..."

    def draw(self, canvas: Image.Image, text: str, x: int, baseline: int,
             px: int, rgb: str, max_w: int = 0) -> int:
        face = self.face(px)
        cells = {int(c["cp"]): c for c in face["cells"]}
        repl = cells.get(0xFFFD)
        y0 = int(face["y"])
        line = int(face["line_height"])
        ascent = int(face["ascent"])
        shown = self.truncate(text, px, max_w) if max_w > 0 else text

        pen = x
        for ch in shown:
            cell = cells.get(ord(ch), repl)
            if cell is None:
                continue
            w = int(cell["advance"])
            glyph = self.image.crop(
                (int(cell["x"]), y0, int(cell["x"]) + w, y0 + line))
            # Tingir: cobertura x cor (mesmo efeito do SDL_SetTextureColorMod).
            tint = Image.new("RGB", glyph.size, rgb)
            canvas.paste(tint, (pen, baseline - ascent), glyph)
            pen += w
        return pen - x


def hex2rgb(value: str) -> str:
    return "#" + value.lstrip("#")


def rounded(draw: ImageDraw.ImageDraw, box: tuple[int, int, int, int],
            radius: int, fill: str) -> None:
    x, y, w, h = box
    if w <= 0 or h <= 0:
        return
    if radius <= 0:
        draw.rectangle([x, y, x + w - 1, y + h - 1], fill=fill)
        return
    draw.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=radius, fill=fill)


def ring(draw: ImageDraw.ImageDraw, box: tuple[int, int, int, int], t: int,
         colour: str) -> None:
    x, y, w, h = box
    if w <= 0 or h <= 0 or t <= 0:
        return
    draw.rectangle([x, y, x + w - 1, y + t - 1], fill=colour)
    draw.rectangle([x, y + h - t, x + w - 1, y + h - 1], fill=colour)
    draw.rectangle([x, y, x + t - 1, y + h - 1], fill=colour)
    draw.rectangle([x + w - t, y, x + w - 1, y + h - 1], fill=colour)


LABELS = ["Musica", "Biblioteca", "Home", "Loja", "Ports"]
HINTS = {
    0: "A Tocar   Select Configuracoes   Home Inicio",
    1: "A Abrir   Select Configuracoes   Home Inicio",
    2: "A Abrir   Select Configuracoes   Home Inicio   B inerte nesta aba",
    3: "app externo: giochi-store",
    4: "app externo: portmaster",
}
TITLES = [
    "Arcade Volleyball", "Spacewar", "Colossal Cave Adventure",
    "Um titulo bem comprido para forcar o recorte", "Outro jogo", "Jogo 6",
    "Jogo 7", "Jogo 8", "Jogo 9", "Jogo 10", "Jogo 11", "Jogo 12",
    "Jogo 13", "Jogo 14", "Jogo 15",
]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--size", default="1280x720")
    ap.add_argument("--tab", type=int, default=1)
    ap.add_argument("--items", type=int, default=15)
    ap.add_argument("--focus", type=int, default=0)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    width, height = (int(v) for v in args.size.split("x"))
    binary = ROOT / "build" / "host" / "bin" / "dump-geometry"

    if not binary.is_file():
        sys.exit(f"erro: {binary} nao existe (rode `make build/host/bin/dump-geometry`)")

    out = args.out or str(ROOT / "docs" / "proposta" / f"ui-{args.size}.png")

    atlas_dir = ROOT / "assets"
    atlas = Atlas(atlas_dir)
    labels = [atlas.measure(name, 14 if height <= 480 else 21) for name in LABELS]

    cmd = [str(binary), "--size", args.size, "--tab", str(args.tab),
           "--items", str(args.items), "--focus", str(args.focus),
           "--labels", ",".join(str(v) for v in labels)]
    geo = json.loads(subprocess.check_output(cmd, text=True))

    theme = load_theme()
    bg = theme["background"]
    canvas = Image.new("RGB", (width, height), bg)
    draw = ImageDraw.Draw(canvas)
    tok = geo["tokens"]

    # --- topbar -------------------------------------------------------
    tb = geo["topbar"]
    draw.rectangle([tb["x"], tb["y"], tb["x"] + tb["w"] - 1,
                    tb["y"] + tb["h"] - 1], fill=theme["surface"])
    draw.rectangle([0, tb["h"] - 1, width - 1, tb["h"] - 1],
                   fill=theme["divider"])

    baseline = (tb["h"] + atlas.face(tok["font_tab"])["ascent"]) // 2
    for i, rect in enumerate(geo["tabs"]):
        if rect["w"] <= 0:
            continue
        active = i == geo["tab_active"]
        colour = theme["text_primary"] if active else theme["text_secondary"]
        atlas.draw(canvas, LABELS[i], rect["x"], baseline, tok["font_tab"],
                   colour, rect["w"])
        if active:
            w = atlas.measure(LABELS[i], tok["font_tab"])
            y = rect["y"] + rect["h"] - tok["accent"] - 1
            draw.rectangle([rect["x"], y, rect["x"] + w - 1,
                            y + tok["accent"] - 1], fill=theme["accent"])

    # --- conteudo -----------------------------------------------------
    tab_id = LABELS[geo["tab_active"]]
    if args.tab == 1 and args.items > 0:
        for i, t in enumerate(geo["tiles"]):
            rounded(draw, (t["x"], t["y"], t["w"], t["h"]), tok["corner"],
                    theme["surface_elevated"])
            index = geo["first_index"] + i
            title = TITLES[index % len(TITLES)]
            base = t["y"] + t["h"] + atlas.face(tok["font_title"])["ascent"] + tok["gap"] // 2
            atlas.draw(canvas, title, t["x"], base, tok["font_title"],
                       theme["text_primary"], t["w"])
        r = geo["focus_ring"]
        if r["w"] > 0:
            ring(draw, (r["x"], r["y"], r["w"], r["h"]), tok["ring"],
                 theme["accent"])
    else:
        c = geo["content"]
        msg = ("Copie um pacote .giochi via USB" if tab_id == "Biblioteca"
               else f"{tab_id}: painel da aba")
        px = tok["font_body"]
        w = atlas.measure(msg, px)
        atlas.draw(canvas, msg, c["x"] + (c["w"] - w) // 2,
                   c["y"] + (c["h"] + atlas.face(px)["ascent"]) // 2 - px // 4,
                   px, theme["text_secondary"], c["w"])

    # --- hint bar -----------------------------------------------------
    hb = geo["hint_bar"]
    draw.rectangle([hb["x"], hb["y"], hb["x"] + hb["w"] - 1,
                    hb["y"] + hb["h"] - 1], fill=theme["hint_bar_bg"])
    draw.rectangle([0, hb["y"], width - 1, hb["y"]], fill=theme["divider"])
    px = tok["font_body"]
    base = hb["y"] + (hb["h"] + atlas.face(px)["ascent"]) // 2
    atlas.draw(canvas, HINTS.get(args.tab, ""), hb["x"] + tok["margin"], base,
               px, theme["text_secondary"], hb["w"] - 2 * tok["margin"])

    out_path = pathlib.Path(out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(out_path)
    print(f"{out_path} ({width}x{height}) colunas={tok['columns']} "
          f"tiles={len(geo['tiles'])} foco={geo['focus_slot']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
