#!/usr/bin/env python3
"""Renders da PROPOSTA de layout (docs/proposta/).

Diferenca em relacao a `tools/make-screenshots.py` (que documenta o que os
assets declaram hoje):

  * desenha NATIVO em cada painel — 640x480 e 1280x720. Nada de escalar o 4:3 e
    centralizar (letterbox), que e o que torna o texto da X55 borrado e nao
    mostra o que o painel 16:9 pode exibir;
  * as metricas vem de um PERFIL DE PAINEL (secao 3 de docs/LAYOUT-PROPOSTA.md):
    o layout canonico multiplicado por 1,5 e arredondado para inteiro inteiro;
  * toda caixa e todo texto passam por checagem geometrica. Texto que nao cabe,
    item de hint bar que colide, grade que invade a topbar/hint bar ou coluna
    que passa da margem FALHAM o script (exit 1). A proposta e verificada por
    execucao, nao por inspecao.

Fontes de dados (as mesmas do Launcher em tempo de execucao):

  assets/theme.json                 cores, tipografia e metricas
  assets/layout.json                ordem das abas, botoes, fonte da barra
  assets/home.json                  hero 112x63, recentes 80x45
  assets/library.json               tile 152x85, titulo 20, scrollbar 4
  assets/music.json                 capa 120x120, now playing 160, controles 50

Nenhuma cor nova e criada: a proposta usa apenas as 8 cores do tema.

Uso:
  tools/make-layout-proposal.py                  # docs/proposta/
  tools/make-layout-proposal.py --out-dir DIR
  tools/make-layout-proposal.py --only home      # um conjunto de telas
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # pragma: no cover - dependencia opcional
    sys.exit("make-layout-proposal: Pillow ausente (pip install pillow)")

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
DEFAULT_OUT = ROOT / "docs" / "proposta"

# Mesma resolucao de fonte de tools/make-screenshots.py. Sem os caminhos do
# Windows o script cai em load_default() (10 px fixos) e todo texto sai com
# metrica errada — inclusive nos renders de proposta.
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

TAB_LABELS = {
    "music": "Música",
    "library": "Biblioteca",
    "home": "Home",
    "store": "Loja",
    "ports": "Ports",
}

# Perfis de painel (secao 3 da proposta). 640x480 e o canonico da spec 4.1.3;
# 1280x720 e a area logica da X55 (720x1280 com rotation=270, F8). `dir` aponta
# para o override do aparelho: os valores do painel saem DE LA (fonte unica),
# nao de uma multiplicacao feita por este script.
PANELS = {
    "default": {"name": "default", "width": 640, "height": 480, "scale": 1.0,
                "label": "640x480", "dir": None},
    "x55": {"name": "x55", "width": 1280, "height": 720, "scale": 1.5,
            "label": "1280x720", "dir": "devices/x55"},
}

# Textos de exemplo: existem para dar escala ao tile, como em make-screenshots.
SAMPLE_GAMES = [
    ["Celeste", "Hollow Knight", "Stardew Valley", "Cuphead", "Hades"],
    ["Sonic Mania", "Dead Cells", "Ori", "Blasphemous", "Undertale"],
    ["Katana Zero", "Cave Story", "Super Metroid", "Tunic", "Celeste II"],
]

TIPS = [
    "Copie pacotes .giochi por USB (MTP) ou pelo cartão.",
    "Home + Start por 0,5 s encerra o jogo em execução.",
    "Modo Desenvolvedor libera SSH em 10.55.0.2 (USB).",
]

# Fila de exemplo da aba Musica: existem para mostrar o ritmo da lista (titulo,
# artista, duracao). Em /user/music vazio a aba mostra a mensagem de vazio.
SAMPLE_TRACKS = [
    ("Tema do Launcher", "Giochi Sound Team", "0:58"),
    ("Bossa do Cartão", "Estúdio Giochi", "3:12"),
    ("Chiptune de Teste", "Bench", "1:44"),
]


def load_json(path: Path) -> dict:
    with path.open(encoding="utf-8") as fp:
        return json.load(fp)


def first_existing(paths: list[str]) -> str | None:
    for path in paths:
        if Path(path).is_file():
            return path
    return None


def q(value: float, scale: float) -> int:
    """Metrica inteira: base * escala, arredondada para cima no .5.

    `round()` do Python usa arredondamento bancario e transformaria 22,5 em 22
    (meio pixel de diferenca em cada metrica de texto da X55).
    """
    return int(math.floor(value * scale + 0.5))


def luminance(hex_color: str) -> float:
    """Luminancia relativa WCAG 2.1 (0 = preto, 1 = branco)."""
    value = hex_color.lstrip("#")
    channels = [int(value[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    linear = [c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
              for c in channels]
    return 0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2]


def contrast(fg: str, bg: str) -> float:
    a, b = luminance(fg), luminance(bg)
    light, dark = max(a, b), min(a, b)
    return (light + 0.05) / (dark + 0.05)


class Tokens:
    """Metricas de um perfil de painel.

    Regra de fonte unica: o que o asset do PAINEL declara (topbar, hint, tile,
    hero, recentes, capa, controle) e usado **como esta** — os arquivos de
    `assets/devices/<id>/` ja trazem o valor do painel. A escala `s` (1,0 no
    canonico, 1,5 no 16:9) so multiplica o que **nao** tem onde ser declarado:
    espacamentos internos, fonte do titulo do tile (que vive no `theme.json`) e
    a tipografia da proposta. Multiplicar o que ja esta declarado aplicaria a
    escala duas vezes.
    """

    def __init__(self, theme: dict, layout: dict, home: dict, library: dict,
                 music: dict, panel: dict):
        self.panel = panel
        self.w = panel["width"]
        self.h = panel["height"]
        s = panel["scale"]
        self.scale = s

        metrics = theme["metrics"]
        typography = theme["typography"]

        # Regioes persistentes: `home.json` do painel manda; tema e fallback.
        self.topbar = int(home["regions"]["top_bar"]["height"])
        self.hint = int(home["regions"]["hint_bar"]["height"])
        self.corner = q(metrics["tile_corner_radius"], s)
        self.tab_font = int(layout["tabs"]["bar"]["font_size"])
        self.title_font = q(typography["tile_title_size"], s)

        # Espacamentos internos: proporcao da altura do painel.
        self.pad = q(12, s)
        self.gap = q(8, s)
        self.gap_large = q(18, s)
        self.tab_pad = q(12, s)
        self.tab_gap = q(20, s)
        self.tab_underline = q(3, s)

        # Tipografia da proposta (o tema declara so topbar/tile title).
        self.section_font = q(15, s)
        self.body_font = q(12, s)
        self.meta_font = q(11, s)
        self.chip_font = q(11, s)

        # Regioes declaradas nos assets do painel (valor final, sem escala).
        hero = home["regions"]["continue"]["size"]
        self.hero_w = int(hero["width"])
        self.hero_h = int(hero["height"])
        recent = home["regions"]["recent"]["item_size"]
        self.recent_w = int(recent["width"])
        self.recent_h = int(recent["height"])
        self.news_h = q(40, s)

        grid = library["regions"]["grid"]
        self.tile_w = int(grid["tile_size"]["width"])
        self.tile_h = int(grid["tile_size"]["height"])
        self.title_h = int(grid["title_height"])
        self.declared_columns = int(grid.get("columns", 0))
        self.scrollbar = int(library["regions"]["scrollbar"]["width"])

        self.art = int(music["regions"]["now_playing"]["album_art"]["size"]["width"])
        self.controls_h = int(music["regions"]["controls"]["height"])

        self.rows = library["regions"]["grid"]["rows"]
        self.columns = self._columns()

    # --- derivados -----------------------------------------------------
    def _columns(self) -> int:
        """Quantas colunas do tile declarado cabem na largura do painel.

        A margem minima e 4 px (q(4)): no canonico 4x152 + 3x8 = 632 de 640, ou
        seja a quarta coluna so cabe com 4 px de cada lado. E por isso que a
        grade e 4x3 e nao 5x3 no painel de 640 — numero declarado na proposta.
        """
        margin = q(4, self.panel["scale"])
        limit = self.w - 2 * margin
        cols = 1
        while True:
            need = (cols + 1) * self.tile_w + cols * self.gap
            if need > limit:
                return cols
            cols += 1

    @property
    def content_top(self) -> int:
        return self.topbar + self.pad

    @property
    def hint_top(self) -> int:
        return self.h - self.hint

    @property
    def grid_width(self) -> int:
        return self.columns * self.tile_w + (self.columns - 1) * self.gap

    @property
    def line(self) -> int:
        """Altura de uma linha de texto de secao (fonte + respiro)."""
        return q(22, self.panel["scale"])


class Sheet:
    """Canvas nativo do painel + registro de violacoes geometricas."""

    def __init__(self, theme: dict, tokens: Tokens):
        self.theme = theme
        self.t = tokens
        self.panel = tokens.panel
        self.w = tokens.w
        self.h = tokens.h
        self.img = Image.new("RGB", (self.w, self.h),
                             theme["colors"]["background"])
        self.d = ImageDraw.Draw(self.img)
        self.violations: list[str] = []
        self.regular = first_existing(FONT_CANDIDATES)
        self.bold = first_existing(FONT_BOLD_CANDIDATES) or self.regular
        if self.regular is None:
            sys.exit("make-layout-proposal: nenhuma fonte encontrada\n"
                     "  tentado: " + ", ".join(FONT_CANDIDATES))
        self._fonts: dict[tuple[int, bool], ImageFont.FreeTypeFont] = {}
        self.text_min = 0  # menor fonte usada, para o relatorio
        self.hint_used = 0  # maior largura ocupada pela hint bar

    # --- primitivas ----------------------------------------------------
    def color(self, name: str) -> str:
        return self.theme["colors"][name]

    def font(self, size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
        key = (size, bold)
        if key not in self._fonts:
            path = self.bold if bold else self.regular
            self._fonts[key] = ImageFont.truetype(path, size)
        return self._fonts[key]

    def measure(self, text: str, size: int, bold: bool = False) -> int:
        box = self.d.textbbox((0, 0), text, font=self.font(size, bold))
        return box[2] - box[0]

    def check(self, ok: bool, message: str) -> bool:
        if not ok:
            self.violations.append(message)
        return ok

    def box(self, xy, fill=None, outline=None, radius=0, width=1, content=False):
        """Retangulo. `content=True` exige que a caixa caiba na area util."""
        x0, y0, x1, y1 = xy
        if not self.check(0 <= x0 < x1 <= self.w and 0 <= y0 < y1 <= self.h,
                          f"caixa fora do painel: {xy}"):
            return
        if content:
            self.check(y0 >= self.t.topbar and y1 <= self.t.hint_top,
                       f"caixa invade topbar/hint bar: {xy}")
        if fill is None and outline is None:
            # Pillow >= 11 desenha contorno BRANCO quando fill e outline sao
            # None (o "ink" default virou branco). Desenhar nada e o esperado
            # para uma caixa sem preenchimento nem borda.
            return
        if radius > 0:
            self.d.rounded_rectangle(xy, radius=radius, fill=fill,
                                     outline=outline, width=width)
        else:
            self.d.rectangle(xy, fill=fill, outline=outline, width=width)

    def text(self, xy, text, size, fill="text_primary", bold=False,
             anchor="la", max_w=None, content=False):
        if size <= 0:
            self.check(False, f"fonte invalida ({size}px) em '{text}'")
            return 0
        self.text_min = size if self.text_min == 0 else min(self.text_min, size)
        width = self.measure(text, size, bold)
        if max_w is not None:
            self.check(width <= max_w,
                       f"texto nao cabe: '{text}' tem {width}px, "
                       f"limite {max_w}px ({size}px)")
        if content:
            self.check(xy[1] >= self.t.topbar and xy[1] <= self.t.hint_top,
                       f"texto fora da area util: '{text}' em {xy}")
        self.d.text(xy, text, font=self.font(size, bold), fill=self.color(fill),
                    anchor=anchor)
        return width

    def ellipsize(self, text, size, max_w, bold=False) -> str:
        if self.measure(text, size, bold) <= max_w:
            return text
        cut = text
        while cut and self.measure(cut + "…", size, bold) > max_w:
            cut = cut[:-1]
        return (cut.rstrip() + "…") if cut else "…"

    def wrap(self, text, size, max_w, bold=False) -> list[str]:
        words = text.split()
        lines: list[str] = []
        current = ""
        for word in words:
            candidate = f"{current} {word}".strip()
            if self.measure(candidate, size, bold) <= max_w:
                current = candidate
            else:
                if current:
                    lines.append(current)
                current = word
        if current:
            lines.append(current)
        return lines

    # --- regioes persistentes -----------------------------------------
    def topbar(self, tabs: list[str], active: str, battery: int = 82,
               clock: str = "12:34"):
        t = self.t
        self.box((0, 0, self.w, t.topbar), fill=self.color("surface"))
        self.d.rectangle((0, t.topbar, self.w, t.topbar + 1),
                         fill=self.color("divider"))

        x = t.tab_pad
        for tab in tabs:
            label = TAB_LABELS.get(tab, tab)
            selected = tab == active
            width = self.text((x, t.topbar // 2), label, t.tab_font,
                              fill="text_primary" if selected else "text_secondary",
                              bold=selected, anchor="lm")
            if selected:
                self.d.rectangle((x, t.topbar - t.tab_underline,
                                  x + width, t.topbar),
                                 fill=self.color("accent"))
            x += width + t.tab_gap
        tabs_end = x - t.tab_gap

        # Indicadores da direita, na ordem de leitura: rede, bateria (icone +
        # %), relogio. Posicionados da direita para a esquerda para nunca
        # encostarem nos rotulos das abas.
        right = self.w - t.tab_pad
        clock_w = self.text((right, t.topbar // 2), clock, t.meta_font,
                            fill="text_secondary", anchor="rm")
        right -= clock_w + t.gap * 2

        pct = f"{battery}%"
        pct_w = self.text((right, t.topbar // 2), pct, t.meta_font,
                          fill="text_secondary", anchor="rm")
        right -= pct_w + t.gap

        icon_w = q(22, self.panel["scale"])
        icon_h = q(10, self.panel["scale"])
        bx1 = right
        bx0 = bx1 - icon_w
        by0 = t.topbar // 2 - icon_h // 2
        self.d.rectangle((bx0, by0, bx1, by0 + icon_h),
                         outline=self.color("text_secondary"), width=1)
        fill_w = int((icon_w - 4) * battery / 100)
        if fill_w > 0:
            self.d.rectangle((bx0 + 2, by0 + 2, bx0 + 2 + fill_w, by0 + icon_h - 2),
                             fill=self.color("text_secondary"))
        right -= icon_w + t.gap * 2

        bars = q(14, self.panel["scale"])
        step = max(3, bars // 3)
        bar0 = right - bars
        for i in range(3):
            height = q(4 + 3 * i, self.panel["scale"])
            x0 = bar0 + i * step
            self.d.rectangle((x0, t.topbar // 2 + bars // 2 - height,
                              x0 + step - 1, t.topbar // 2 + bars // 2),
                             fill=self.color("text_secondary"))
        right = bar0

        self.check(tabs_end + t.tab_gap <= right,
                   f"topbar: abas terminam em {tabs_end}px, indicadores "
                   f"comecam em {right}px")

    def hint_bar(self, items, info: str | None = None):
        """`items` = [(botao, acao)]; itens inoperantes NAO entram na lista."""
        t = self.t
        top = t.hint_top
        self.box((0, top, self.w, self.h), fill=self.color("hint_bar_bg"))
        self.d.rectangle((0, top, self.w, top + 1), fill=self.color("divider"))

        chip_pad = q(5, self.panel["scale"])
        chip_gap = q(6, self.panel["scale"])
        item_gap = q(18, self.panel["scale"])
        center = top + t.hint // 2
        # Altura do chip vem da METRICA do texto, nao da fonte nominal: com
        # `chip_font + 2*pad` o "Select" encosta na borda inferior.
        chip_h = max(self.d.textbbox((0, 0), button,
                                     font=self.font(t.chip_font, True))[3]
                     for button, _ in items) + 2 * chip_pad
        self.check(chip_h <= t.hint - 2,
                   f"hint bar: chip de {chip_h}px nao cabe em {t.hint}px")

        x = t.tab_pad
        for button, action in items:
            chip_w = self.measure(button, t.chip_font, bold=True) + 2 * chip_pad
            self.box((x, center - chip_h // 2, x + chip_w, center + chip_h // 2),
                     fill=self.color("surface_elevated"),
                     radius=q(3, self.panel["scale"]))
            self.text((x + chip_w // 2, center), button, t.chip_font,
                      fill="text_primary", bold=True, anchor="mm")
            x += chip_w + chip_gap
            width = self.text((x, center), action, t.body_font,
                              fill="text_secondary", anchor="lm")
            x += width + item_gap
        used_end = x - item_gap
        self.hint_used = max(self.hint_used, used_end)

        if info is not None:
            info_w = self.measure(info, t.meta_font)
            self.text((self.w - t.tab_pad, center), info, t.meta_font,
                      fill="text_secondary", anchor="rm")
            self.check(used_end + t.gap * 2 <= self.w - t.tab_pad - info_w,
                       f"hint bar: acoes ate {used_end}px colidem com a dica "
                       f"'{info}' ({info_w}px)")
        else:
            self.check(used_end <= self.w - t.tab_pad,
                       f"hint bar: acoes somam {used_end}px, limite "
                       f"{self.w - t.tab_pad}px")

    # --- blocos reutilizados ------------------------------------------
    def section(self, y: int, title: str) -> int:
        """Titulo de secao; devolve o y do conteudo abaixo dele."""
        t = self.t
        self.text((t.pad, y), title, t.section_font, bold=True, anchor="la",
                  content=True)
        return y + t.line

    def transport_icon(self, cx: int, cy: int, radius: int, kind: str,
                       primary: bool):
        """Icone de transporte desenhado (triangulo/barras), sem glifo de fonte.

        Glifo de midia nao existe em toda fonte (U+23F8/U+23F9 faltam na
        DejaVu); desenhar primitivas e reproduzivel em qualquer painel.
        """
        fill = "text_primary" if primary else "text_secondary"
        self.d.ellipse((cx - radius, cy - radius, cx + radius, cy + radius),
                       fill=self.color("accent") if primary
                       else self.color("surface_elevated"))
        color = self.color("background" if primary else fill)
        r = radius // 2
        if kind == "play":
            self.d.polygon([(cx - r + 2, cy - r), (cx + r, cy),
                            (cx - r + 2, cy + r)], fill=color)
        elif kind == "pause":
            bar = max(2, r // 2)
            self.d.rectangle((cx - r, cy - r, cx - r + bar, cy + r), fill=color)
            self.d.rectangle((cx + r - bar, cy - r, cx + r, cy + r), fill=color)
        elif kind == "prev":
            bar = max(2, r // 3)
            self.d.rectangle((cx - r, cy - r, cx - r + bar, cy + r), fill=color)
            self.d.polygon([(cx + r, cy - r), (cx + r, cy + r),
                            (cx - r + bar, cy)], fill=color)
        elif kind == "next":
            bar = max(2, r // 3)
            self.d.rectangle((cx + r - bar, cy - r, cx + r, cy + r), fill=color)
            self.d.polygon([(cx - r, cy - r), (cx - r, cy + r),
                            (cx + r - bar, cy)], fill=color)

    def card(self, xy, title: str, lines: list[str], status: str | None):
        """Cartao de estado vazio/indisponivel (abas Loja e Ports)."""
        t = self.t
        x0, y0, x1, y1 = xy
        self.box(xy, fill=self.color("surface"), radius=t.corner, content=True)
        inner = q(24, self.panel["scale"])
        self.d.rectangle((x0, y0 + q(10, self.panel["scale"]),
                          x0 + q(3, self.panel["scale"]),
                          y1 - q(10, self.panel["scale"])),
                         fill=self.color("accent"))
        y = y0 + inner
        self.text((x0 + inner, y), title, q(16, self.panel["scale"]),
                  bold=True, anchor="la", max_w=x1 - x0 - 2 * inner,
                  content=True)
        y += q(28, self.panel["scale"])
        for line in lines:
            self.text((x0 + inner, y), line, t.body_font,
                      fill="text_secondary", anchor="la",
                      max_w=x1 - x0 - 2 * inner, content=True)
            y += t.line
        if status is not None:
            chip_w = self.measure(status, t.meta_font) + 2 * q(8, t.panel["scale"])
            chip_h = t.meta_font + 2 * q(3, t.panel["scale"])
            cy = y + q(6, self.panel["scale"])
            self.box((x0 + inner, cy, x0 + inner + chip_w, cy + chip_h),
                     fill=self.color("surface_elevated"), radius=chip_h // 2,
                     content=True)
            self.text((x0 + inner + chip_w // 2, cy + chip_h // 2), status,
                      t.meta_font, fill="text_secondary", anchor="mm",
                      content=True)

    # --- telas ---------------------------------------------------------
    def tab_home(self, tabs: list[str]):
        t = self.t
        self.topbar(tabs, "home")
        y = t.content_top

        y = self.section(y, "Continuar")
        hero_bottom = y + t.hero_h
        self.box((t.pad, y, t.pad + t.hero_w, hero_bottom),
                 fill=self.color("surface_elevated"), radius=t.corner,
                 content=True)
        # Sem capa (continue.json ausente): triangulo de midia em `divider`.
        # Cor decorativa do proprio tema — a proposta nao cria cor nova.
        glyph = q(16, self.panel["scale"])
        gx, gy = t.pad + t.hero_w // 2, y + t.hero_h // 2
        self.d.polygon([(gx - glyph // 2, gy - glyph // 2),
                        (gx + glyph // 2, gy),
                        (gx - glyph // 2, gy + glyph // 2)],
                       fill=self.color("divider"))

        info_x = t.pad + t.hero_w + t.gap * 2
        self.box((info_x, y, self.w - t.pad, hero_bottom),
                 fill=self.color("surface"), radius=t.corner, content=True)
        self.text((info_x + t.gap * 2, y + t.hero_h // 2 - t.line // 2),
                  "Nada em andamento", q(14, t.panel["scale"]), bold=True,
                  anchor="la", max_w=self.w - t.pad - info_x - t.gap * 4,
                  content=True)
        self.text((info_x + t.gap * 2, y + t.hero_h // 2 + t.line // 2),
                  "Abra um jogo da Biblioteca para começar.", t.body_font,
                  fill="text_secondary", anchor="la",
                  max_w=self.w - t.pad - info_x - t.gap * 4, content=True)
        y = hero_bottom + t.gap_large

        y = self.section(y, "Novidades")
        for tip in TIPS:
            self.box((t.pad, y, self.w - t.pad, y + t.news_h),
                     fill=self.color("surface"), radius=t.corner, content=True)
            self.d.rectangle((t.pad, y, t.pad + q(4, t.panel["scale"]),
                              y + t.news_h), fill=self.color("accent"))
            self.text((t.pad + q(12, t.panel["scale"]), y + t.news_h // 2),
                      tip, t.body_font, anchor="lm",
                      max_w=self.w - 2 * t.pad - q(20, t.panel["scale"]),
                      content=True)
            y += t.news_h + t.gap
        y += t.gap_large - t.gap

        y = self.section(y, "Recentes")
        for i in range(5):
            x = t.pad + i * (t.recent_w + t.gap)
            self.box((x, y, x + t.recent_w, y + t.recent_h),
                     fill=self.color("surface"), radius=t.corner,
                     outline=self.color("divider"), content=True)
            if i == 0:
                self.text((x + t.recent_w // 2, y + t.recent_h // 2), "vazio",
                          t.meta_font, fill="text_secondary", anchor="mm",
                          content=True)

        self.hint_bar([("A", "Abrir"), ("Select", "Configurações"),
                       ("Home", "Início")],
                      info="B inerte nesta aba")

    def tab_library(self, tabs: list[str]):
        t = self.t
        self.topbar(tabs, "library")

        grid_w = t.grid_width
        x0 = (self.w - grid_w) // 2
        y0 = t.content_top
        for row in range(t.rows):
            for col in range(t.columns):
                x = x0 + col * (t.tile_w + t.gap)
                y = y0 + row * (t.tile_h + t.title_h + t.gap)
                focused = row == 0 and col == 0
                title = SAMPLE_GAMES[row][col]
                self.box((x, y, x + t.tile_w, y + t.tile_h),
                         fill=self.color("surface_elevated" if focused
                                         else "surface"),
                         radius=t.corner,
                         outline=self.color("accent") if focused else None,
                         width=max(1, q(2, t.panel["scale"])), content=True)
                if focused:
                    # anel de foco: 2 px de acento por fora do tile
                    ring = max(1, q(2, t.panel["scale"]))
                    self.box((x - ring, y - ring, x + t.tile_w + ring,
                              y + t.tile_h + ring),
                             outline=self.color("accent"), width=ring,
                             radius=t.corner, content=True)
                self.text((x + q(6, t.panel["scale"]), y + t.tile_h
                           + t.title_h // 2),
                          self.ellipsize(title, t.title_font, t.tile_w),
                          t.title_font,
                          fill="text_primary" if focused else "text_secondary",
                          anchor="lm", max_w=t.tile_w, content=True)

        grid_bottom = y0 + t.rows * (t.tile_h + t.title_h) + (t.rows - 1) * t.gap
        self.check(grid_bottom <= t.hint_top,
                   f"grade: termina em {grid_bottom}px, hint bar comeca em "
                   f"{t.hint_top}px")

        # Scrollbar na sarjeta direita, sem sobrepor a ultima coluna.
        sb_x = self.w - t.scrollbar
        self.box((sb_x, t.content_top, self.w, t.hint_top),
                 fill=self.color("surface"))
        self.check(sb_x >= x0 + grid_w,
                   f"scrollbar ({sb_x}px) sobrepoe a grade (fim em "
                   f"{x0 + grid_w}px)")
        self.box((sb_x, t.content_top + t.gap, self.w,
                  t.content_top + t.gap + q(80, t.panel["scale"])),
                 fill=self.color("text_secondary"))

        self.hint_bar([("A", "Jogar"), ("B", "Voltar"),
                       ("Select", "Configurações")],
                      info="Cole .giochi por USB")

    def tab_music(self, tabs: list[str]):
        t = self.t
        self.topbar(tabs, "music")
        y = t.content_top

        self.box((t.pad, y, t.pad + t.art, y + t.art),
                 fill=self.color("surface_elevated"), radius=t.corner,
                 content=True)
        tx = t.pad + t.art + t.gap * 2
        max_w = self.w - t.pad - tx
        self.text((tx, y + q(10, t.panel["scale"])), "Faixa de exemplo",
                  q(16, t.panel["scale"]), bold=True, anchor="la",
                  max_w=max_w, content=True)
        self.text((tx, y + q(36, t.panel["scale"])), "Artista", q(13, t.panel["scale"]),
                  fill="text_secondary", anchor="la", max_w=max_w, content=True)
        self.text((tx, y + q(56, t.panel["scale"])), "Álbum",
                  t.body_font, fill="text_secondary", anchor="la",
                  max_w=max_w, content=True)
        self.text((tx, y + t.art - t.meta_font), "MP3 · FLAC · OGG",
                  t.meta_font, fill="text_secondary", anchor="la",
                  max_w=max_w, content=True)

        py = y + t.art + t.gap_large
        bar = q(4, t.panel["scale"])
        self.box((t.pad, py, self.w - t.pad, py + bar),
                 fill=self.color("divider"), content=True)
        self.box((t.pad, py, t.pad + (self.w - 2 * t.pad) // 3, py + bar),
                 fill=self.color("accent"), content=True)
        self.text((t.pad, py + bar + t.gap), "1:12", t.meta_font,
                  fill="text_secondary", anchor="la", content=True)
        self.text((self.w - t.pad, py + bar + t.gap), "3:45", t.meta_font,
                  fill="text_secondary", anchor="ra", content=True)

        cy = py + bar + t.line + t.controls_h // 2
        r = q(13, t.panel["scale"])
        for kind, dx, primary in (("prev", -q(70, t.panel["scale"]), False),
                                  ("play", 0, True),
                                  ("next", q(70, t.panel["scale"]), False)):
            self.transport_icon(self.w // 2 + dx, cy,
                                q(16, t.panel["scale"]) if primary else r,
                                kind, primary)
        self.check(cy + q(16, t.panel["scale"]) <= t.hint_top,
                   "controles de transporte invadem a hint bar")

        ly = cy + q(16, t.panel["scale"]) + t.gap_large
        ly = self.section(ly, f"Fila ({len(SAMPLE_TRACKS)})")
        s = t.panel["scale"]
        row_h = q(34, s)
        for i, (title, artist, duration) in enumerate(SAMPLE_TRACKS):
            focused = i == 0
            self.box((t.pad, ly, self.w - t.pad, ly + row_h),
                     fill=self.color("surface") if focused else None,
                     radius=t.corner, content=True)
            if focused:
                self.d.rectangle((t.pad, ly, t.pad + q(3, s), ly + row_h),
                                 fill=self.color("accent"))
            inner = t.pad + q(12, s)
            center_y = ly + row_h // 2
            dur_w = self.measure(duration, t.meta_font)
            self.text((self.w - t.pad - q(10, s), center_y), duration,
                      t.meta_font, fill="text_secondary", anchor="rm",
                      content=True)
            limit = self.w - t.pad - q(10, s) - dur_w - t.gap * 2
            title_max = int((limit - inner) * 0.6)
            shown = self.ellipsize(title, t.body_font, title_max)
            title_w = self.text((inner, center_y), shown, t.body_font,
                                fill="text_primary" if focused else "text_secondary",
                                anchor="lm", max_w=title_max, content=True)
            artist_x = inner + title_w + t.gap * 2
            artist_max = limit - artist_x
            if artist_max >= self.measure(artist, t.meta_font):
                self.text((artist_x, center_y), artist, t.meta_font,
                          fill="text_secondary", anchor="lm",
                          max_w=artist_max, content=True)
            ly += row_h
        self.check(ly <= t.hint_top,
                   f"fila de faixas termina em {ly}px, hint bar em {t.hint_top}px")

        self.hint_bar([("A", "Reproduzir"), ("Y", "Anterior"),
                       ("X", "Próxima"), ("B", "Voltar")],
                      info="Música para a tela")

    def tab_placeholder(self, tabs: list[str], tab: str, title: str,
                        lines: list[str], status: str):
        t = self.t
        self.topbar(tabs, tab)
        card_w = min(self.w - 2 * t.pad, q(430, t.panel["scale"]))
        card_h = q(180, t.panel["scale"])
        x0 = (self.w - card_w) // 2
        y0 = t.content_top + (t.hint_top - t.content_top - card_h) // 2
        self.card((x0, y0, x0 + card_w, y0 + card_h), title, lines, status)
        self.hint_bar([("B", "Voltar"), ("Home", "Início")])

    def tab_store(self, tabs: list[str]):
        self.tab_placeholder(
            tabs, "store", "Loja",
            ["O componente giochi-store ainda não está nesta imagem.",
             "Quando existir: catálogo de pacotes .giochi assinados,",
             "verificação de assinatura e arch antes de instalar."],
            "componente ausente")

    def tab_ports(self, tabs: list[str]):
        self.tab_placeholder(
            tabs, "ports", "Ports",
            ["O componente portmaster ainda não está nesta imagem.",
             "Quando existir: wrappers ARM64 em /user/ports, sem",
             "distribuir arquivos originais dos jogos."],
            "componente ausente")


def render_all(theme: dict, layout: dict, panel: dict, assets: dict,
               only: set[str] | None = None) -> tuple[dict[str, Image.Image],
                                                      list[str], dict]:
    tokens = Tokens(theme, layout, assets["home"], assets["library"],
                    assets["music"], panel)
    tabs = layout["tabs"]["order"]
    screens: dict[str, Image.Image] = {}
    violations: list[str] = []

    # O asset declara um numero de colunas; a formula diz outro. Um dos dois
    # esta errado (o caso classico: tile grande demais para a largura).
    if tokens.declared_columns and tokens.declared_columns != tokens.columns:
        violations.append(
            f"painel[{panel['label']}]: asset declara {tokens.declared_columns} "
            f"colunas, a formula cabe {tokens.columns} "
            f"(tile {tokens.tile_w}px + gap {tokens.gap}px em {tokens.w}px)")

    def build(name, fn):
        sheet = Sheet(theme, tokens)
        fn(sheet)
        screens[name] = sheet.img
        for v in sheet.violations:
            violations.append(f"{name}[{panel['label']}]: {v}")
        build.hint_used = max(getattr(build, "hint_used", 0), sheet.hint_used)
        return sheet

    last = None
    if only is None or "home" in only:
        last = build("home", lambda s: s.tab_home(tabs))
    if only is None or "library" in only:
        last = build("library", lambda s: s.tab_library(tabs))
    if only is None or "music" in only:
        last = build("music", lambda s: s.tab_music(tabs))
    if only is None or "store" in only:
        last = build("store", lambda s: s.tab_store(tabs))
    if only is None or "ports" in only:
        last = build("ports", lambda s: s.tab_ports(tabs))

    stats = {
        "panel": panel["label"],
        "scale": panel["scale"],
        "topbar": tokens.topbar,
        "hint": tokens.hint,
        "tile": f"{tokens.tile_w}x{tokens.tile_h}",
        "columns": tokens.columns,
        "grid_width": tokens.grid_width,
        "grid_bottom": (tokens.content_top + tokens.rows
                        * (tokens.tile_h + tokens.title_h)
                        + (tokens.rows - 1) * tokens.gap),
        "content_top": tokens.content_top,
        "hint_top": tokens.hint_top,
        "section_font": tokens.section_font,
        "body_font": tokens.body_font,
        "meta_font": tokens.meta_font,
        "title_font": tokens.title_font,
        "tab_font": tokens.tab_font,
        "min_font": last.text_min if last is not None else 0,
        "hint_used": getattr(build, "hint_used", 0),
        "hint_limit": panel["width"] - tokens.tab_pad,
    }
    return screens, violations, stats


def contrast_report(theme: dict) -> list[tuple[str, float, str]]:
    colors = theme["colors"]
    pairs = [
        ("text_primary", "background"), ("text_primary", "surface"),
        ("text_secondary", "background"), ("text_secondary", "surface"),
        ("text_secondary", "hint_bar_bg"), ("text_primary", "surface_elevated"),
        ("accent", "background"), ("accent", "surface"),
        ("divider", "background"), ("divider", "hint_bar_bg"),
    ]
    out = []
    for fg, bg in pairs:
        ratio = contrast(colors[fg], colors[bg])
        if fg == "divider" or fg == "accent":
            verdict = "nao-texto"
        elif ratio >= 7.0:
            verdict = "AAA"
        elif ratio >= 4.5:
            verdict = "AA"
        elif ratio >= 3.0:
            verdict = "AA-grande"
        else:
            verdict = "ABAIXO"
        out.append((f"{fg} / {bg}", ratio, verdict))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out-dir", default=str(DEFAULT_OUT))
    ap.add_argument("--only", default=None,
                    help="lista separada por virgula: home,library,music,store,ports")
    ap.add_argument("--quiet", action="store_true", help="nao imprime o relatorio")
    args = ap.parse_args()

    theme = load_json(ASSETS / "theme.json")

    def panel_assets(panel: dict) -> dict:
        """Assets do painel: base ou o override de `assets/devices/<id>/`."""
        base = ASSETS if panel.get("dir") is None else ASSETS / panel["dir"]
        return {
            "layout": load_json(base / "layout.json"),
            "home": load_json(base / "home.json"),
            "library": load_json(base / "library.json"),
            "music": load_json(base / "music.json"),
        }

    only = None
    if args.only:
        only = {name.strip() for name in args.only.split(",") if name.strip()}
        unknown = only - {"home", "library", "music", "store", "ports"}
        if unknown:
            sys.exit(f"make-layout-proposal: tela desconhecida: {sorted(unknown)}")

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    all_violations: list[str] = []
    all_stats: dict[str, dict] = {}
    generated: list[Path] = []

    for panel in PANELS.values():
        assets = panel_assets(panel)
        screens, violations, stats = render_all(theme, assets["layout"],
                                                panel, assets, only)
        all_violations.extend(violations)
        all_stats[panel["name"]] = stats
        for name, img in screens.items():
            path = out_dir / f"{name}-{panel['label']}.png"
            img.save(path, optimize=True)
            generated.append(path)

    if not args.quiet:
        print("== metricas por painel ==")
        keys = ["scale", "topbar", "hint", "tile", "columns", "grid_width",
                "grid_bottom", "hint_top", "hint_used", "hint_limit",
                "tab_font", "title_font", "section_font", "body_font",
                "meta_font"]
        for name, stats in all_stats.items():
            print(f"-- {name} ({stats['panel']})")
            for key in keys:
                print(f"   {key:14} {stats[key]}")
        print("== contraste (WCAG 2.1) ==")
        for pair, ratio, verdict in contrast_report(theme):
            print(f"   {pair:34} {ratio:5.2f}:1  {verdict}")
        print("== arquivos ==")
        for path in generated:
            rel = path.relative_to(ROOT) if path.is_relative_to(ROOT) else path
            print(f"   {rel} ({path.stat().st_size // 1024} KB)")

    if all_violations:
        print("== VIOLACOES ==", file=sys.stderr)
        for violation in all_violations:
            print(f"   {violation}", file=sys.stderr)
        return 1
    if not args.quiet:
        print("ok: nenhuma violacao geometrica")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
