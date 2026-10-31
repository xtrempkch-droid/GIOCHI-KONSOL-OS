#!/usr/bin/env python3
"""Gera as imagens de documentacao a partir dos assets REAIS do sistema.

Fonte de verdade (as mesmas que o Launcher le em tempo de execucao):

  assets/theme.json            cores, tipografia e metricas (topbar 40, hint 30)
  assets/layout.json           abas, ordem, central, botoes
  assets/home.json             regioes da aba Home (hero, noticias, recentes)
  assets/library.json          grid 4x3, tile 152x85, titulo 20, scrollbar 4
  assets/music.json            now playing 120x120, progresso, controles, lista
  assets/devices/x55/layout.json  override da X55 (1280x720)
  assets/bootlogo/bootlogo.rgba   splash real desenhado pelo Launcher

O desenho aqui reproduz a GEOMETRIA e as CORES declaradas nesses arquivos.
Ele nao e um screenshot de um aparelho: o desenho dos widgets no backend SDL2
ainda e esboco (ver TODO(v1.2) em src/launcher/render.cpp). Por isso
docs/SCREENSHOTS.md e o README dizem, com todas as letras, o que estas imagens
sao e o que nao sao.

Uso:
  tools/make-screenshots.py                 # gera tudo em docs/screenshots/
  tools/make-screenshots.py --out-dir DIR
  tools/make-screenshots.py --scale 2       # composicao 2x (canvas X55)
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # pragma: no cover - dependencia opcional
    sys.exit("make-screenshots: Pillow ausente (pip install pillow)")

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
DEFAULT_OUT = ROOT / "docs" / "screenshots"

# DejaVu tambem vem instalada no Windows (C:\Windows\Fonts). Sem estes caminhos
# o script cai em ImageFont.load_default() fora do Linux e todo texto sai com
# ~10 px e metricas erradas — o render deixa de refletir os tamanhos declarados
# (aconteceu ao rodar no Windows: a barra de dicas saiu com rotulos colados).
FONT_CANDIDATES = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "C:/Windows/Fonts/DejaVuSans.ttf",
    "C:/Windows/Fonts/segoeui.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
]
FONT_BOLD_CANDIDATES = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
    "C:/Windows/Fonts/DejaVuSans-Bold.ttf",
    "C:/Windows/Fonts/segoeuib.ttf",
    "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
]


def load_json(path: Path) -> dict:
    with path.open(encoding="utf-8") as fp:
        return json.load(fp)


def first_existing(paths: list[str]) -> str | None:
    for path in paths:
        if Path(path).is_file():
            return path
    return None


class Canvas:
    """Canvas logico em 640x480 com escala inteira para o PNG final."""

    def __init__(self, theme: dict, scale: int):
        self.theme = theme
        self.scale = scale
        self.width = 640
        self.height = 480
        self.img = Image.new("RGB", (self.width, self.height),
                             theme["colors"]["background"])
        self.draw = ImageDraw.Draw(self.img)
        self._fonts: dict[tuple[int, bool], ImageFont.FreeTypeFont] = {}
        self.regular = first_existing(FONT_CANDIDATES)
        self.bold = first_existing(FONT_BOLD_CANDIDATES) or self.regular

    # --- helpers -------------------------------------------------------
    def color(self, name: str) -> str:
        return self.theme["colors"][name]

    def font(self, size: int, bold: bool = False):
        key = (size, bold)
        if key not in self._fonts:
            path = self.bold if bold else self.regular
            if path is None:
                self._fonts[key] = ImageFont.load_default()
            else:
                self._fonts[key] = ImageFont.truetype(path, size)
        return self._fonts[key]

    def rect(self, box, fill=None, outline=None, width=1, radius=0):
        if radius > 0:
            self.draw.rounded_rectangle(box, radius=radius, fill=fill,
                                        outline=outline, width=width)
        else:
            self.draw.rectangle(box, fill=fill, outline=outline, width=width)

    def text(self, xy, text, size=12, fill="text_primary", bold=False,
             anchor=None):
        self.draw.text(xy, text, font=self.font(size, bold), fill=self.color(fill),
                       anchor=anchor)

    def text_width(self, text, size=12, bold=False) -> int:
        box = self.draw.textbbox((0, 0), text, font=self.font(size, bold))
        return box[2] - box[0]

    # --- regioes persistentes ------------------------------------------
    def topbar(self, tabs: list[str], active: str, battery="82%"):
        h = self.theme["metrics"]["topbar_height"]
        self.rect((0, 0, self.width, h), fill=self.color("surface"))

        x = 12
        for tab in tabs:
            label = TAB_LABELS.get(tab, tab.capitalize())
            size = 14
            w = self.text_width(label, size, bold=(tab == active))
            selected = tab == active
            self.text((x, h // 2), label, size=size,
                      fill="text_primary" if selected else "text_secondary",
                      bold=selected, anchor="lm")
            if selected:
                self.rect((x, h - 3, x + w, h), fill=self.color("accent"))
            x += w + 24

        # indicadores da direita (battery_indicator, network_indicator, clock)
        self.text((self.width - 12, h // 2), "12:34", size=12,
                  fill="text_secondary", anchor="rm")
        self.text((self.width - 62, h // 2), battery, size=12,
                  fill="text_secondary", anchor="rm")
        if GLYPH_WIFI and self.regular:
            self.draw.text((self.width - 108, h // 2), GLYPH_WIFI,
                           font=self.font(13), fill=self.color("text_secondary"),
                           anchor="rm")
        self.rect((0, h, self.width, h + 1), fill=self.color("divider"))

    def hint_bar(self, hint: str):
        h = self.theme["metrics"]["hint_bar_height"]
        top = self.height - h
        self.rect((0, top, self.width, self.height), fill=self.color("hint_bar_bg"))
        self.rect((0, top, self.width, top + 1), fill=self.color("divider"))
        self.text((12, top + h // 2), hint, size=12, fill="text_secondary",
                  anchor="lm")

    # --- exportacao ----------------------------------------------------
    def image(self) -> Image.Image:
        return self.img

    def save(self, path: Path):
        path.parent.mkdir(parents=True, exist_ok=True)
        save_png(self.img, path, self.scale)
        return path


def save_png(img: Image.Image, path: Path, scale: int = 1) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    if scale != 1:
        img = img.resize((img.width * scale, img.height * scale), Image.LANCZOS)
    img.save(path, optimize=True)
    return path


def fit_screen(img: Image.Image, theme: dict, size=(1280, 720)) -> Image.Image:
    """Encaixa a composicao 640x480 num canvas de painel, sem distorcer.

    O layout declara 640x480; a X55 tem painel 1280x720 (16:9). Aqui o conteudo
    e escalado para a ALTURA e centralizado na largura, com as laterais na cor
    de fundo (letterbox) — em vez de esticar e mentir sobre a proporcao.
    """
    canvas = Image.new("RGB", size, theme["colors"]["background"])
    factor = size[1] / img.height
    scaled = img.resize((int(img.width * factor), size[1]), Image.LANCZOS)
    if scaled.width > size[0]:
        left = (scaled.width - size[0]) // 2
        scaled = scaled.crop((left, 0, left + size[0], size[1]))
    canvas.paste(scaled, ((size[0] - scaled.width) // 2, 0))
    return canvas


TAB_LABELS = {
    "music": "Música",
    "library": "Biblioteca",
    "home": "Home",
    "store": "Loja",
    "ports": "Ports",
}

# Acentos do hint bar (prefixo de plataforma nos botoes).
GLYPH_WIFI = ""
try:  # DejaVu cobre o bloco "Geometric Shapes"/"Misc Symbols" parcialmente.
    from PIL import ImageFont as _F

    _probe = _F.truetype(first_existing(FONT_CANDIDATES) or "", 12)
    if _probe.getmask("\u25cf").getbbox() is not None:
        GLYPH_WIFI = "\u25cf"
except Exception:  # noqa: BLE001 - glifo e opcional
    GLYPH_WIFI = ""


def draw_tile(c: Canvas, box, title, kind="game", accent=False):
    """Tile do grid: capa + titulo (152x85 + 20 de titulo declarados)."""
    x0, y0, x1, y1 = box
    radius = c.theme["metrics"]["tile_corner_radius"]
    surface = "surface_elevated" if accent else "surface"
    c.rect(box, fill=c.color(surface), radius=radius)

    # "capa": faixa com gradiente discreto para dar volume, sem inventar arte
    top = y1 - 22
    c.rect((x0, top, x1, y1), fill=c.color("background"))
    c.rect((x0, y1 - 20, x1, y1), fill=c.color("background"))
    c.text((x0 + 6, y1 - 11), title, size=12,
           fill="text_primary" if accent else "text_secondary", anchor="lm")


def screen_home(theme, layout, home, scale):
    c = Canvas(theme, scale)
    tabs = layout["tabs"]["order"]
    c.topbar(tabs, "home")

    y = theme["metrics"]["topbar_height"] + 16

    # continue: hero_tile 112x63 (declarado em home.json)
    hero = home["regions"]["continue"]
    hw, hh = hero["size"]["width"], hero["size"]["height"]
    c.text((12, y), "Continuar", size=15, bold=True)
    y += 24
    c.rect((12, y, 12 + hw, y + hh), fill=c.color("surface_elevated"), radius=4)
    c.rect((12 + hw + 16, y, c.width - 12, y + hh),
           fill=c.color("surface"), radius=4)
    c.text((12 + hw + 28, y + 16), "Nada em andamento", size=14, bold=True)
    c.text((12 + hw + 28, y + 38),
           hero.get("fallback_text", ""), size=12, fill="text_secondary")
    y += hh + 22

    # news: news_list com fallback builtin_tips
    c.text((12, y), "Novidades", size=15, bold=True)
    y += 24
    tips = [
        "Copie pacotes .giochi por USB (MTP) ou pelo cartão.",
        "Home + Start por 0,5 s encerra o jogo em execução.",
        "Modo Desenvolvedor libera SSH em 10.55.0.2 (USB).",
    ]
    for tip in tips[:2]:
        c.rect((12, y, c.width - 12, y + 40), fill=c.color("surface"), radius=4)
        c.rect((12, y, 16, y + 40), fill=c.color("accent"))
        c.text((26, y + 20), tip[:72], size=12, anchor="lm")
        y += 48

    # recent: horizontal_list, item 80x45, ate 5 itens
    y += 4
    c.text((12, y), "Recentes", size=15, bold=True)
    y += 24
    recent = home["regions"]["recent"]
    iw, ih = recent["item_size"]["width"], recent["item_size"]["height"]
    for i in range(recent["max_items"]):
        x = 12 + i * (iw + 8)
        c.rect((x, y, x + iw, y + ih), fill=c.color("surface"), radius=4)
        # sem cache de recentes: o texto de vazio e o que o Launcher mostraria
        if i == 0:
            c.text((x + iw // 2, y + ih // 2), "vazio", size=11,
                   fill="text_secondary", anchor="mm")

    c.hint_bar("A abrir   B voltar   Select configurações   Home início")
    return c.image()


def screen_library(theme, layout, library, scale):
    c = Canvas(theme, scale)
    c.topbar(layout["tabs"]["order"], "library")

    grid = library["regions"]["grid"]
    tile_w = grid["tile_size"]["width"]
    tile_h = grid["tile_size"]["height"]
    cols, rows = grid["columns"], grid["rows"]

    gap = 8
    total_w = cols * tile_w + (cols - 1) * gap
    x0 = (c.width - total_w) // 2
    y = theme["metrics"]["topbar_height"] + 14

    names = [
        ["Celeste", "Hollow Knight", "Stardew Valley", "Cuphead"],
        ["Sonic Mania", "Dead Cells", "Ori", "Hades"],
        ["Blasphemous", "Undertale", "Katana Zero", ""],
    ]
    for row in range(rows):
        for col in range(cols):
            x = x0 + col * (tile_w + gap)
            yy = y + row * (tile_h + grid["title_height"] + gap)
            title = names[row][col]
            if not title:
                c.rect((x, yy, x + tile_w, yy + tile_h),
                       fill=c.color("surface"), outline=c.color("divider"),
                       radius=c.theme["metrics"]["tile_corner_radius"])
                continue
            draw_tile(c, (x, yy, x + tile_w, yy + tile_h), title,
                      accent=(row == 0 and col == 0))

    # scrollbar 4 px na direita
    sb = library["regions"]["scrollbar"]["width"]
    c.rect((c.width - sb - 2, theme["metrics"]["topbar_height"],
            c.width - 2, c.height - theme["metrics"]["hint_bar_height"]),
           fill=c.color("surface"))
    c.rect((c.width - sb - 2, theme["metrics"]["topbar_height"] + 4,
            c.width - 2, theme["metrics"]["topbar_height"] + 84),
           fill=c.color("text_secondary"))

    c.hint_bar("A jogar   B voltar   Select configurações   .giochi via USB")
    return c.image()


def screen_music(theme, layout, music, scale):
    c = Canvas(theme, scale)
    c.topbar(layout["tabs"]["order"], "music")

    np = music["regions"]["now_playing"]
    art = np["album_art"]["size"]
    y = theme["metrics"]["topbar_height"] + 12
    c.rect((12, y, 12 + art["width"], y + art["height"]),
           fill=c.color("surface_elevated"), radius=4)

    tx = 12 + art["width"] + 16
    c.text((tx, y + 16), "Faixa de exemplo", size=16, bold=True)
    c.text((tx, y + 40), "Artista", size=13, fill="text_secondary")
    c.text((tx, y + 60), "Álbum", size=12, fill="text_secondary")
    c.text((tx, y + 92), "MP3 · FLAC · OGG", size=11, fill="text_secondary")

    # progresso (progress_bar + tempo)
    py = y + np["height"] + 6
    c.rect((12, py, c.width - 12, py + 4), fill=c.color("divider"))
    c.rect((12, py, 12 + (c.width - 24) // 3, py + 4), fill=c.color("accent"))
    c.text((12, py + 14), "1:12", size=11, fill="text_secondary")
    c.text((c.width - 12, py + 14), "3:45", size=11, fill="text_secondary",
           anchor="ra")

    # controles de transporte (Y anterior, A play, X proximo)
    ctl = music["regions"]["controls"]
    cy = py + 26
    cx = c.width // 2
    for label, dx, primary in (("Y", -70, False), ("A", 0, True),
                               ("X", 70, False)):
        r = 16 if primary else 13
        c.rect((cx + dx - r, cy + 14 - r, cx + dx + r, cy + 14 + r),
               fill=c.color("accent") if primary else c.color("surface_elevated"),
               radius=r)
        c.text((cx + dx, cy + 14), label, size=13, bold=True,
               fill="text_primary", anchor="mm")

    # playlist (track_list)
    ly = cy + ctl["height"] + 4
    c.text((12, ly), "Fila (0)", size=13, bold=True)
    ly += 20
    c.text((12, ly + 14), "Nenhuma faixa em /user/music", size=12,
           fill="text_secondary", anchor="lm")

    c.hint_bar("A reproduzir   Y anterior   X próxima   B voltar")
    return c.image()


def screen_settings(theme, scale, level=0):
    c = Canvas(theme, scale)
    # fundo = tela atras escurecida (overlay sobre a Home)
    c.rect((0, 0, c.width, c.height), fill=c.color("background"))
    c.rect((0, 0, c.width, c.height), fill=c.color("surface"))

    items = ["Aparência", "Sistema", "Som", "Loja", "Rede", "Sobre", "Desligar"]
    sub = ["Idioma", "Fuso horário", "Brilho", "Suspensão", "Modo USB",
           "Modo Desenvolvedor"]
    shown = items if level == 0 else sub
    active = 1 if level == 0 else 5

    x0, y0 = 96, 56
    x1, y1 = c.width - 96, c.height - 56
    radius = theme["metrics"]["tile_corner_radius"]
    c.rect((x0, y0, x1, y1), fill=c.color("surface_elevated"), radius=radius)
    c.text((x0 + 20, y0 + 24), "Configurações" if level == 0 else "Sistema",
           size=16, bold=True)
    c.rect((x0, y0 + 52, x1, y0 + 53), fill=c.color("divider"))

    y = y0 + 70
    for i, item in enumerate(shown):
        selected = i == active
        if selected:
            c.rect((x0 + 8, y - 6, x1 - 8, y + 22), fill=c.color("surface"),
                   radius=4)
            c.rect((x0 + 8, y - 6, x0 + 11, y + 22), fill=c.color("accent"))
        label = item
        if level == 1 and item == "Modo Desenvolvedor":
            label = "Modo Desenvolvedor   desligado"
        if level == 1 and item == "Modo USB":
            label = "Modo USB   MTP"
        c.text((x0 + 24, y + 8), label, size=13,
               fill="text_primary" if selected else "text_secondary",
               bold=selected, anchor="lm")
        y += 34

    c.text((x0 + 20, y1 - 18),
           "A aplicar   B voltar" if level == 1 else "A entrar   B fechar",
           size=11, fill="text_secondary", anchor="lm")
    return c.image()


def screen_bootlogo(theme, scale, out: Path):
    """Splash REAL: o Launcher desenha este arquivo nos primeiros frames."""
    meta = load_json(ASSETS / "bootlogo" / "bootlogo.json")
    w, h = meta["width"], meta["height"]
    raw = (ASSETS / "bootlogo" / "bootlogo.rgba").read_bytes()
    if len(raw) != w * h * 4:
        sys.exit(f"make-screenshots: bootlogo.rgba tem {len(raw)} bytes, "
                 f"esperado {w * h * 4}")

    logo = Image.frombytes("RGBA", (w, h), raw)
    canvas = Image.new("RGB", (640, 480), theme["colors"]["background"])
    canvas.paste(logo, ((640 - w) // 2, (480 - h) // 2), logo)
    if scale != 1:
        canvas = canvas.resize((640 * scale, 480 * scale), Image.LANCZOS)
    out.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(out, optimize=True)
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=str(DEFAULT_OUT))
    ap.add_argument("--scale", type=int, default=1,
                    help="escala inteira do canvas 640x480 (2 gera 1280x720)")
    args = ap.parse_args()

    out_dir = Path(args.out_dir)
    theme = load_json(ASSETS / "theme.json")
    layout = load_json(ASSETS / "layout.json")
    home = load_json(ASSETS / "home.json")
    library = load_json(ASSETS / "library.json")
    music = load_json(ASSETS / "music.json")
    x55_layout = load_json(ASSETS / "devices" / "x55" / "layout.json")

    base = {
        "home": screen_home(theme, layout, home, 1),
        "library": screen_library(theme, layout, library, 1),
        "music": screen_music(theme, layout, music, 1),
        "settings": screen_settings(theme, 0, level=0),
        "settings-system": screen_settings(theme, 0, level=1),
    }
    base["x55-home"] = fit_screen(
        screen_home(theme, x55_layout, home, 1), theme)

    generated = [save_png(img, out_dir / f"{name}.png", args.scale)
                 for name, img in base.items()]
    generated.append(screen_bootlogo(theme, args.scale,
                                     out_dir / "bootlogo.png"))

    for path in generated:
        rel = path.relative_to(ROOT) if path.is_relative_to(ROOT) else path
        print(f"gerado: {rel} ({path.stat().st_size // 1024} KB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
