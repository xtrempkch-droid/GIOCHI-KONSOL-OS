#!/usr/bin/env python3
"""Desenha o que o Launcher vai desenhar, no PC, para inspecao visual.

Por que isto existe: o render.cpp usa SDL2/KMSDRM, que precisa de /dev/dri. Nao
ha como olhar a tela do aparelho sem o aparelho, e "compilou" nao e "esta certo"
— a UI em pe' e o layout com uma coluna so' custaram dois ciclos de flash.

Regra deste script: ele NAO tem numero de layout e NAO tem texto. A geometria
vem de tools/dump-geometry (ligado ao mesmo ui_geometry.cpp e ao mesmo
i18n.cpp) e o texto vem do mesmo catalogo (assets/locales/*.json). Aqui so'
existe rasterizacao — se a imagem sair errada, o render.cpp tambem sai.

Uso:
  tools/preview-ui.py --lang pt-BR --tab 1        # Biblioteca, 1280x720
  tools/preview-ui.py --size 640x480 --tab 2      # Home no painel pequeno
  tools/preview-ui.py --lang en --settings 1      # menu de Configuracoes
"""
from __future__ import annotations

import argparse
import json
import math
import pathlib
import subprocess
import sys

try:
    from PIL import Image, ImageDraw
except ImportError:  # pragma: no cover
    sys.exit("erro: Pillow nao instalado (pip install Pillow)")

ROOT = pathlib.Path(__file__).resolve().parent.parent


def load_theme(geo: dict) -> dict[str, str]:
    """Cores do tema ATIVO, publicadas pelo dump-geometry.

    Antes este script lia assets/theme.json por conta propria: era uma segunda
    leitura do mesmo dado e, com varios temas, o preview mostraria um e o
    aparelho outro. O dump resolve o tema com a MESMA giochi_theme_resolve.
    """
    bloco = geo.get("theme")
    if not bloco:
        sys.exit("erro: dump-geometry sem bloco 'theme' (binario antigo?)")
    return dict(bloco["colors"])


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
        # (px, peso) -> faixa. O peso e' o mesmo nome do font.json ("regular"
        # | "bold"); cair para regular quando o peso falta e' melhor que nao
        # desenhar nada.
        self.faces = {(int(s["px"]), s.get("weight", "regular")): s
                      for s in self.meta["sizes"]}
        self.peso_padrao = self.meta.get("weights", ["regular"])[0]

    def face(self, px: int, weight: str = "regular") -> dict:
        if (px, weight) in self.faces:
            return self.faces[(px, weight)]
        # mesmo criterio do font.cpp: px mais proximo DENTRO do peso pedido
        mesmo = [s for (p, w), s in self.faces.items() if w == weight]
        if not mesmo:
            mesmo = list(self.faces.values())
        return min(mesmo, key=lambda s: abs(int(s["px"]) - px))

    def _cells(self, px: int, weight: str = "regular") -> dict:
        face = self.face(px, weight)
        return {int(c["cp"]): c for c in face["cells"]}

    def measure(self, text: str, px: int, weight: str = "regular") -> int:
        cells = self._cells(px, weight)
        repl = cells.get(0xFFFD, {"advance": px // 2})
        return sum(int(cells.get(ord(ch), repl)["advance"]) for ch in text)

    def truncate(self, text: str, px: int, max_w: int,
                 weight: str = "regular") -> str:
        if max_w <= 0 or self.measure(text, px, weight) <= max_w:
            return text
        ell = self.measure("...", px, weight)
        out = ""
        for ch in text:
            if self.measure(out + ch, px, weight) + ell > max_w:
                break
            out += ch
        return out + "..."

    def draw_now(self, draw: ImageDraw.ImageDraw, text: str, x: int,
                 baseline: int, px: int, rgb: str, max_w: int = 0,
                 weight: str = "regular") -> int:
        """Desenha usando o MESMO recorte por pixel do font.cpp."""
        face = self.face(px, weight)
        cells = self._cells(px, weight)
        repl = cells.get(0xFFFD)
        y0 = int(face["y"])
        line = int(face["line_height"])
        ascent = int(face["ascent"])
        shown = self.truncate(text, px, max_w, weight) if max_w > 0 else text
        colour = Image.new("RGB", (1, 1), rgb)

        pen = x
        for ch in shown:
            cell = cells.get(ord(ch), repl)
            if cell is None:
                continue
            # Bloco desenhado = `w` (>= avanco); o passo do cursor e' o
            # avanco. Cortar em `advance` comia a ponta de A, K, T, f, r.
            w = int(cell.get("w", cell["advance"]))
            glyph = self.image.crop(
                (int(cell["x"]), y0, int(cell["x"]) + w, y0 + line))
            tint = Image.new("RGB", glyph.size, colour.getpixel((0, 0)))
            draw._image.paste(tint, (pen, baseline - ascent), glyph)
            pen += int(cell["advance"])
        return pen - x


def dump(atlas: Atlas, args) -> dict:
    """Duas passadas: a primeira da' os rotulos, a segunda a geometria medida."""
    base = [str(ROOT / "build/host/bin/dump-geometry"), "--size", args.size,
            "--tab", str(args.tab), "--items", str(args.items),
            "--focus", str(args.focus), "--lang", args.lang,
            "--assets", str(ROOT / "assets"),
            "--settings", str(args.settings), "--level", str(args.level),
            "--index", str(args.index)]
    if args.theme:
        base += ["--theme", args.theme]

    primeiro = json.loads(subprocess.check_output(base, text=True,
                                                  stderr=subprocess.DEVNULL))
    px_tab = 14 if primeiro["screen"]["h"] <= 480 else 21
    # Aba e' medida em NEGRITO nos dois lados (render.cpp): a ativa e'
    # negrita e medir pelo regular faria ela transbordar a celula.
    larguras = [atlas.measure(t, px_tab, "bold")
                for t in primeiro["tabs_text"]]

    return json.loads(subprocess.check_output(
        base + ["--labels", ",".join(str(w) for w in larguras)],
        text=True, stderr=subprocess.DEVNULL))


def stroke_arc(draw, cx, cy, radius, thickness, from_deg, to_deg, cor):
    """Mesmo arco estampado do render.cpp (semantica identica)."""
    if radius <= 0 or thickness <= 0 or to_deg <= from_deg:
        return
    passos = (to_deg - from_deg) * 2
    for i in range(passos + 1):
        deg = from_deg + (to_deg - from_deg) * i / passos
        rad = math.radians(deg)
        x = cx + int(radius * math.cos(rad) + 0.5)
        y = cy - int(radius * math.sin(rad) + 0.5)
        draw.rectangle([x - thickness // 2, y - thickness // 2,
                        x - thickness // 2 + thickness - 1,
                        y - thickness // 2 + thickness - 1], fill=cor)


def draw_wifi(draw, cx, base_y, size, cor, sinal, up):
    """Tres arcos abertos para cima + ponto: Wi-Fi, nao barras de celular."""
    off = "#3A3A3A"
    esp = max(2, size // 9)
    lit = 0 if not up else (3 if sinal < 0 else 1 + (sinal * 3) // 100)
    for i in range(3):
        r = size // 3 + i * (size // 5)
        stroke_arc(draw, cx, base_y, r, esp, 40, 140, cor if i < lit else off)
    d = esp + 1
    draw.rectangle([cx - d // 2, base_y - d // 2,
                    cx - d // 2 + d - 1, base_y - d // 2 + d - 1],
                   fill=cor if up else off)


def draw_battery(draw, x, y, w, h, cor, pct, carregando):
    if pct < 0:
        return
    borda = max(1, h // 8)
    draw.rectangle([x, y, x + w - 1, y + borda - 1], fill=cor)
    draw.rectangle([x, y + h - borda, x + w - 1, y + h - 1], fill=cor)
    draw.rectangle([x, y, x + borda - 1, y + h - 1], fill=cor)
    draw.rectangle([x + w - borda, y, x + w - 1, y + h - 1], fill=cor)
    draw.rectangle([x + w, y + h // 3, x + w + borda - 1, y + h - h // 3 - 1],
                   fill=cor)
    cor_carga = "#3FB950" if carregando else ("#E50914" if pct <= 15 else cor)
    barra = (w - 2 * borda) * pct // 100
    draw.rectangle([x + borda, y + borda, x + borda + barra - 1, y + h - borda - 1],
                   fill=cor_carga)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--size", default="1280x720")
    ap.add_argument("--tab", type=int, default=1)
    ap.add_argument("--items", type=int, default=15)
    ap.add_argument("--focus", type=int, default=0)
    ap.add_argument("--lang", default="pt-BR")
    ap.add_argument("--settings", type=int, default=0)
    ap.add_argument("--level", type=int, default=1)
    ap.add_argument("--index", type=int, default=0)
    ap.add_argument("--theme", default=None,
                    help="id do tema (data/themes/<id>.json); vazio = padrao")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    atlas = Atlas(ROOT / "assets")
    geo = dump(atlas, args)

    width, height = geo["screen"]["w"], geo["screen"]["h"]
    tok = geo["tokens"]
    theme = load_theme(geo)

    canvas = Image.new("RGB", (width, height), theme["background"])
    draw = ImageDraw.Draw(canvas)

    def rr(box, raio, cor):
        x, y, w, h = box
        if w <= 0 or h <= 0:
            return
        if raio <= 0:
            draw.rectangle([x, y, x + w - 1, y + h - 1], fill=cor)
        else:
            draw.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=raio,
                                   fill=cor)

    # UiRole -> chave do tema. A ORDEM tem de ser a do enum em
    # src/launcher/ui_geometry.hpp; tests/test_ui_geometry.cpp cobra isso.
    papeis = {
        0: None,                    # UI_ROLE_NONE
        1: "background",
        2: "surface",
        3: "surface_elevated",
        4: "divider",
        5: "accent",
        6: "text_primary",
        7: "text_secondary",
    }

    def papel(role: int) -> str:
        chave = papeis.get(int(role))
        return theme[chave] if chave else theme["background"]

    # UiTextRole -> peso. A lista VEM do dump-geometry (campo font_weights),
    # que le a tabela de ui_text_weight(): o preview nao repete a regra.
    pesos = geo.get("font_weights") or []

    def peso(role: int) -> str:
        return pesos[role] if 0 <= role < len(pesos) else "regular"

    def borda(box, raio, cor, esp):
        x, y, w, h = box
        if w <= 0 or h <= 0:
            return
        draw.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=raio,
                               outline=cor, width=esp)

    def ring(box, t, cor):
        x, y, w, h = box
        if w <= 0 or h <= 0:
            return
        draw.rectangle([x, y, x + w - 1, y + t - 1], fill=cor)
        draw.rectangle([x, y + h - t, x + w - 1, y + h - 1], fill=cor)
        draw.rectangle([x, y, x + t - 1, y + h - 1], fill=cor)
        draw.rectangle([x + w - t, y, x + w - 1, y + h - 1], fill=cor)

    # ---------- conteudo da aba ----------
    textos = geo["texts"]
    if geo["tab_id"] == "library" and args.items > 0:
        for i, t in enumerate(geo["tiles"]):
            # O estilo (preenchimento, recorte, borda, cor do titulo, anel,
            # recuo) vem de ui_tile_style, pelo dump-geometry: e' a MESMA
            # regra que o render.cpp aplica. Antes dai o preview pintava todos
            # os tiles de `surface_elevated` e a grade virava parede de caixas
            # claras, com o foco apagado (§3.71).
            fundo = papel(t["fill"])
            rr((t["x"], t["y"], t["w"], t["h"]), tok["corner"], fundo)

            if t["inset"]:
                recuo = max(1, tok["corner"] // 2)
                rr((t["x"] + recuo, t["y"] + recuo,
                    t["w"] - 2 * recuo, t["h"] - 2 * recuo),
                   max(0, tok["corner"] - recuo), papel(t["inset"]))

            if t["outline"]:
                # Espessura por PAPEL, igual ao render.cpp: foco usa o anel,
                # tile comum usa a borda do tema (§ theme.metrics.tile_border).
                eh_foco = t["ring"] > 0
                esp = max(1, tok["ring"] if eh_foco
                          else int(geo["theme"].get("border", 0)) or 1)
                borda((t["x"], t["y"], t["w"], t["h"]), tok["corner"],
                      papel(t["outline"]), esp)

            base = t["y"] + t["h"] + t["title_dy"]
            titulo = f"Jogo {geo['first_index'] + i + 1}"
            atlas.draw_now(draw, titulo, t["x"] + t["pad"], base,
                           tok["font_title"], papel(t["title_role"]),
                           t["w"] - t["pad"], peso(2))

            if t["ring"] > 0:
                r = geo["focus_ring"]
                ring((r["x"], r["y"], r["w"], r["h"]), t["ring"],
                     theme["accent"])
        # scrollbar na sarjeta
        if args.items > tok["capacity"] and tok["capacity"] > 0:
            sb = tok["ring"]
            x = width - tok["inset"] - sb
            cel = tok["tile_h"] + tok["title_h"] + tok["gap"]
            trilho = tok["rows"] * cel - tok["gap"]
            paginas = (args.items + tok["capacity"] - 1) // tok["capacity"]
            barra = trilho // paginas
            pagina = geo["first_index"] // tok["capacity"]
            y = geo["topbar"]["h"] + tok["margin"]
            if paginas > 1:
                y += (trilho - barra) * pagina // (paginas - 1)
            draw.rectangle([x, geo["topbar"]["h"] + tok["margin"], x + sb - 1,
                            geo["topbar"]["h"] + tok["margin"] + trilho - 1],
                           fill=theme["divider"])
            draw.rectangle([x, y, x + sb - 1, y + max(tok["gap"], barra) - 1],
                           fill=theme["accent"])
        conta = textos["count"]
        wc = atlas.measure(conta, tok["font_body"], peso(3))
        linha_h = atlas.face(tok["font_body"])["line_height"]
        cy = geo["hint_bar"]["y"] - tok["margin"] - linha_h
        atlas.draw_now(draw, conta, width - tok["margin"] - wc,
                       cy + atlas.face(tok["font_body"])["ascent"],
                       tok["font_body"], theme["text_secondary"])
    else:
        painel = (tok["margin"], geo["topbar"]["h"] + tok["margin"],
                  width - 2 * tok["margin"], geo["content"]["h"] - 2 * tok["margin"])
        rr(painel, tok["corner"], theme["surface_elevated"])
        x0 = painel[0] + tok["margin"]
        draw.rectangle([x0, painel[1] + tok["margin"],
                        x0 + painel[2] // 8 - 1,
                        painel[1] + tok["margin"] + tok["accent"] - 1],
                       fill=theme["accent"])
        base = (painel[1] + tok["margin"] + tok["accent"]
                + atlas.face(tok["font_tab"], peso(7))["ascent"])
        tw = painel[2] - 2 * tok["margin"]
        titulo = textos["title"]
        atlas.draw_now(draw, titulo, x0, base, tok["font_tab"],
                       theme["text_primary"], tw, peso(7))
        base += atlas.face(tok["font_tab"])["line_height"] + tok["gap"] // 2
        if geo["tab_id"] == "library":
            linhas = [textos["empty"], textos["empty_hint"]]
        elif geo["tab_id"] == "home":
            linhas = [textos["home_tip1"], textos["home_tip2"]]
        elif geo["tab_id"] == "music":
            linhas = [textos["music_empty"], ""]
        elif geo["tab_id"] == "store":
            linhas = [textos["store_missing"], ""]
        else:
            linhas = [textos["ports_missing"], ""]
        for linha in linhas:
            if not linha:
                continue
            atlas.draw_now(draw, linha, x0, base, tok["font_body"],
                           theme["text_secondary"], tw)
            base += atlas.face(tok["font_body"])["line_height"] + tok["gap"] // 4

    # ---------- topbar ----------
    tb = geo["topbar"]
    draw.rectangle([0, 0, width - 1, tb["h"] - 1], fill=theme["surface"])
    div = max(1, int(tok.get("divider", 1)))
    draw.rectangle([0, tb["h"] - div, width - 1, tb["h"] - 1],
                   fill=theme["divider"])

    base = (tb["h"] + atlas.face(tok["font_tab"])["ascent"]) // 2
    for i, r in enumerate(geo["tabs"]):
        if r["w"] <= 0:
            continue
        ativo = i == geo["tab_active"]
        cor = theme["text_primary"] if ativo else theme["text_secondary"]
        rotulo = geo["tabs_text"][i]
        p_aba = peso(0 if ativo else 1)
        atlas.draw_now(draw, rotulo, r["x"], base, tok["font_tab"], cor,
                       r["w"], p_aba)
        if ativo:
            w = atlas.measure(rotulo, tok["font_tab"], p_aba)
            y = r["y"] + r["h"] - tok["accent"] - 1
            draw.rectangle([r["x"], y, r["x"] + w - 1, y + tok["accent"] - 1],
                           fill=theme["accent"])

    # indicadores a' direita: relogio, bateria+%, cabo, wifi
    st = geo["status"]
    gap = tok["gap"]
    icone = tb["h"] // 3
    meio = tb["h"] // 2
    x = width - tok["margin"]
    p_status = peso(6)
    if st["time"]:
        w = atlas.measure(st["time"], tok["font_body"], p_status)
        x -= w
        atlas.draw_now(draw, st["time"], x,
                       (tb["h"] + atlas.face(tok["font_body"], p_status)["ascent"]) // 2,
                       tok["font_body"], theme["text_secondary"], 0, p_status)
        x -= gap
    if st["battery"] >= 0:
        pct = f"{st['battery']}%"
        w = atlas.measure(pct, tok["font_body"], p_status)
        x -= w
        atlas.draw_now(draw, pct, x,
                       (tb["h"] + atlas.face(tok["font_body"], p_status)["ascent"]) // 2,
                       tok["font_body"], theme["text_secondary"], 0, p_status)
        x -= gap // 2
        bw = icone * 2
        x -= bw
        draw_battery(draw, x, meio - icone // 2, bw - icone // 3, icone,
                     theme["text_primary"], st["battery"], st["charging"])
        x -= gap
    if st["ethernet"]:
        x -= icone
        draw.rectangle([x, meio - icone // 2, x + icone - 1, meio - 1],
                       fill=theme["text_secondary"])
        x -= gap // 2
    if st["wifi_present"]:
        x -= icone
        draw_wifi(draw, x + icone // 2, meio + icone // 2 - 1, icone,
                  theme["text_secondary"], st["wifi_signal"], st["wifi_up"])

    # ---------- hint bar com chips ----------
    hb = geo["hint_bar"]
    draw.rectangle([hb["x"], hb["y"], hb["x"] + hb["w"] - 1, hb["y"] + hb["h"] - 1],
                   fill=theme["hint_bar_bg"])
    draw.rectangle([0, hb["y"], width - 1, hb["y"] + div - 1],
                   fill=theme["divider"])

    px = tok["font_body"]
    p_tecla = peso(4)
    p_acao = peso(5)
    px_tecla = tok["font_body"] - 2
    linha_h = atlas.face(px, p_acao)["line_height"]
    pad_y = (hb["h"] - linha_h) // 3
    chip_h = atlas.face(px_tecla, p_tecla)["line_height"] + 2 * pad_y
    pad_x = tok["gap"]
    base = hb["y"] + (hb["h"] + atlas.face(px, p_acao)["ascent"]) // 2
    x = hb["x"] + tok["margin"]
    limite = hb["x"] + hb["w"] - tok["margin"]

    for par in (geo["hint"] or "").split("|"):
        if not par:
            continue
        botao, _, acao = par.partition(":")
        bw = atlas.measure(botao, px_tecla, p_tecla)
        chip_w = bw + 2 * pad_x
        aw = atlas.measure(acao, px, p_acao) if acao else 0
        need = chip_w + (gap // 2 + aw if aw else 0)
        if x + need > limite:
            break
        rr((x, hb["y"] + (hb["h"] - chip_h) // 2, chip_w, chip_h),
           max(2, tok["corner"] // 2), theme["surface_elevated"])
        atlas.draw_now(draw, botao, x + pad_x, base, px_tecla,
                       theme["text_primary"], 0, p_tecla)
        x += chip_w
        if aw:
            x += gap // 2
            atlas.draw_now(draw, acao, x, base, px, theme["text_secondary"], 0,
                           p_acao)
            x += aw
        x += gap * 2

    # ---------- Configuracoes ----------
    cfg = geo["settings"]
    if cfg["open"]:
        pad = tok["margin"]
        painel = (tok["margin"] * 2, geo["topbar"]["h"] + tok["margin"],
                  width // 2, geo["content"]["h"] - 2 * tok["margin"])
        rr(painel, tok["corner"], theme["surface_elevated"])
        ring(painel, tok["ring"], theme["accent"])
        x0 = painel[0] + pad
        base_t = painel[1] + pad + atlas.face(tok["font_tab"])["ascent"]
        atlas.draw_now(draw, cfg["title"], x0, base_t, tok["font_tab"],
                       theme["text_primary"], painel[2] - 2 * pad, peso(7))
        draw.rectangle([x0, base_t + atlas.face(tok["font_body"])["line_height"] // 2,
                        x0 + painel[2] // 5 - 1,
                        base_t + atlas.face(tok["font_body"])["line_height"] // 2],
                       fill=theme["accent"])
        row_h = atlas.face(tok["font_tab"])["line_height"] + tok["gap"]
        y = base_t + atlas.face(tok["font_tab"])["line_height"] + tok["gap"]
        for i, item in enumerate(cfg["items"]):
            if y + row_h > painel[1] + painel[3] - pad:
                break
            sel = i == cfg["index"]
            linha = (painel[0] + pad // 2, y, painel[2] - pad, row_h)
            if sel:
                rr(linha, tok["corner"], theme["surface"])
                draw.rectangle([linha[0], linha[1] + gap // 2,
                                linha[0] + tok["accent"] - 1,
                                linha[1] + row_h - gap // 2 - 1],
                               fill=theme["accent"])
            p_linha = peso(8)
            base_r = (y + (row_h + atlas.face(tok["font_body"], p_linha)["ascent"]) // 2
                      - tok["font_body"] // 8)
            cor = theme["text_primary"] if sel else theme["text_secondary"]
            atlas.draw_now(draw, item["label"], linha[0] + pad, base_r,
                           tok["font_body"], cor, linha[2] // 2, p_linha)
            direita = ">" if item.get("sub") else item.get("value", "")
            if direita:
                w = atlas.measure(direita, tok["font_body"], p_linha)
                atlas.draw_now(draw, direita, linha[0] + linha[2] - pad - w,
                               base_r, tok["font_body"], cor, 0, p_linha)
            y += row_h

    nome = args.out or str(ROOT / "docs" / "proposta" /
                           f"ui-{args.size}-{geo['lang']}"
                           f"{'-config' if cfg['open'] else ''}.png")
    out = pathlib.Path(nome)
    out.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(out)
    print(f"{out} ({width}x{height}) lang={geo['lang']} "
          f"colunas={tok['columns']} tiles={len(geo['tiles'])} "
          f"foco={geo['focus_slot']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
