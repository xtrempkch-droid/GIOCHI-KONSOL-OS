#!/usr/bin/env python3
"""Contrato do atlas da fonte embutida (assets/font.a8 + assets/font.json).

O atlas e GERADO por tools/make-font.py e versionado. Se ele e o codigo que o
consome saírem de sincronia, o sintoma no aparelho e mudo: os rotulos somem e a
tela fica so com as caixas — indistinguivel de "o Launcher nao subiu", que e
exatamente o defeito que consumo 4 sessoes de depuracao (§3.44/§3.55).

Este teste amarra os dois lados:
  1. o arquivo tem exatamente width*height bytes (o loader recusa o resto);
  2. os tamanhos do atlas existem de verdade e cobrem os tokens de
     docs/LAYOUT-PROPOSTA.md §3 (14/12 no base, 21/18 na X55);
  3. todo glifo ASCII imprimivel esta na faixa declarada;
  4. nenhuma celula cai fora da imagem (o SDL_RenderCopy receberia src
     negativo);
  5. o atlas tem pixel desenhado — um atlas vazio passaria 1..4;
  6. todo bloco de glifo tem a ALTURA DA LINHA: a primeira versao recortava
     `w` x `line_height` com o par trocado e guardava 22x11 em vez de 13x22,
     cortando a METADE DE BAIXO de todas as letras. O sintoma na tela era
     "fonte ruim/borrada", nao "fonte quebrada" — e passou por cinco imagens.
     O check 7 olha a metade de baixo da linha, que e' o que sumia;
  7. existe a face em NEGRITO, e ela e' mais pesada que a regular — titulo de
     tile, aba ativa e tecla da hint bar sao negrito na proposta.
"""
from __future__ import annotations

import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"

# Tokens que a UI pede (docs/LAYOUT-PROPOSTA.md §3).
NEEDED_SIZES = {12, 14, 18, 21}

failures = 0
checks = 0


def check(cond: bool, msg: str) -> None:
    global failures, checks

    checks += 1
    if not cond:
        failures += 1
        print(f"FALHOU: {msg}", file=sys.stderr)


def main() -> int:
    jpath = ASSETS / "font.json"
    apath = ASSETS / "font.a8"

    check(jpath.is_file(), "assets/font.json ausente (rode make font)")
    check(apath.is_file(), "assets/font.a8 ausente (rode make font)")
    if failures:
        return finish()

    meta = json.loads(jpath.read_text(encoding="utf-8"))
    width = int(meta["width"])
    height = int(meta["height"])
    data = apath.read_bytes()

    check(width > 0 and height > 0, f"dimensoes invalidas: {width}x{height}")
    check(
        len(data) == width * height,
        f"font.a8 tem {len(data)} B, esperado {width * height} B "
        f"({width}x{height})",
    )
    check(
        len(data) >= width * height,
        "atlas truncado: o loader recusa e a UI fica sem rotulo",
    )

    sizes = meta.get("sizes") or []
    check(len(sizes) > 0, "font.json sem nenhum tamanho")

    # Faixas: ASCII + Latin-1. A segunda nao e' luxo — o repositorio e' em
    # portugues e "Início"/"Música"/"Configurações" tem acento. Sem ela o
    # texto sai cheio de caixas vazadas.
    ranges = meta.get("ranges") or []
    check(len(ranges) >= 2, f"atlas so' com as faixas {ranges}: falta Latin-1")
    cobertos = set()
    for lo, hi in ranges:
        cobertos.update(range(int(lo), int(hi) + 1))
    for letra in "áàâãéêíóôõúüçÁÀÂÃÉÊÍÓÔÕÚÜÇ":
        check(ord(letra) in cobertos,
              f"falta o glifo acentuado {letra!r} (cp {ord(letra)})")

    available = {int(s["px"]) for s in sizes}
    missing = sorted(NEEDED_SIZES - available)
    check(
        not missing,
        f"o atlas nao tem os tamanhos pedidos pela UI: {missing}",
    )

    # Ordem crescente e faixas sem sobreposicao: duas faces no mesmo y fariam
    # o desenho pegar o glifo do tamanho errado.
    prev_bottom = 0
    for size in sorted(sizes, key=lambda s: int(s["y"])):
        y = int(size["y"])
        line_h = int(size["line_height"])
        check(y >= prev_bottom, f"faixa de {size['px']} px sobrepoe a anterior")
        prev_bottom = y + line_h
        check(
            y + line_h <= height,
            f"faixa de {size['px']} px termina em {y + line_h} > {height}",
        )
        check(
            int(size["ascent"]) <= line_h,
            f"ascent {size['ascent']} maior que a linha {line_h}",
        )

        cells = size.get("cells") or []
        check(len(cells) >= len(cobertos),
              f"{size['px']} px: {len(cells)} glifos, esperado {len(cobertos)}")

        cps = {int(c["cp"]) for c in cells}
        for cp in cobertos:
            check(cp in cps, f"{size['px']} px: falta o glifo cp {cp}")

        rotulo = f"{size.get('weight', 'regular')} {size['px']} px"

        for cell in cells:
            x = int(cell["x"])
            adv = int(cell["advance"])
            # `w` e' o bloco desenhado; `advance` e' o passo. Sao diferentes
            # de proposito: 22 glifos do DejaVu passam 1 px do proprio avanco
            # (A, K, T, V, f, r) e recortar no avanco come a ponta.
            bloco = int(cell.get("w", adv))
            check(adv > 0, f"{rotulo}: avanco zero no cp {cell['cp']}")
            check(
                bloco >= adv,
                f"{rotulo}: bloco {bloco} < avanco {adv} no cp {cell['cp']}",
            )
            check(
                x >= 0 and x + bloco <= width,
                f"{rotulo}: glifo {cell['cp']} fora do atlas "
                f"({x}+{bloco} > {width})",
            )

    check(
        int(meta.get("replacement_cp", 0)) > 0,
        "sem code point de substituicao: titulo em UTF-8 sumiria calado",
    )

    # --- pesos ------------------------------------------------------------
    pesos = meta.get("weights") or []
    check("regular" in pesos, f"font.json sem o peso regular (tem {pesos})")
    check(
        "bold" in pesos,
        f"font.json sem o peso negrito (tem {pesos}): titulo, aba ativa e "
        f"tecla da hint bar sairiam finos num painel de ~290 ppi",
    )

    presentes = {(int(s["px"]), s.get("weight", "regular")) for s in sizes}
    for px in NEEDED_SIZES:
        for peso in pesos:
            check(
                (px, peso) in presentes,
                f"falta a face {peso} de {px} px",
            )

    # --- cobertura: a METADE DE BAIXO da linha tem de estar pintada -------
    # Foi o defeito do atlas 1.x: o recorte ficava `w` x `line_height` com o
    # par trocado e a metade de baixo de cada letra ia embora. Olhar so' o
    # total de pixels pintados nao pegaria isso (a metade de cima continuava
    # la'), entao o check e' por REGIAO: letras que tem tinta na base ou
    # descendente ('g', 'p', 'y', 'Q') precisam de tinta na metade de baixo.
    def cobertura(px: int, peso: str, cp: int) -> tuple[int, int, int]:
        """(tinta no topo, tinta na base, altura da linha) de um glifo."""
        face = next((s for s in sizes
                     if int(s["px"]) == px and s.get("weight", "regular") == peso),
                    None)
        if face is None:
            return (0, 0, 0)
        cell = next((c for c in face["cells"] if int(c["cp"]) == cp), None)
        if cell is None:
            return (0, 0, 0)
        y0 = int(face["y"])
        lh = int(face["line_height"])
        x0 = int(cell["x"])
        bloco = int(cell.get("w", cell["advance"]))
        meio = lh // 2
        topo = base = 0
        for y in range(lh):
            linha = data[(y0 + y) * width + x0:(y0 + y) * width + x0 + bloco]
            tinta = sum(1 for b in linha if b > 32)
            if y < meio:
                topo += tinta
            else:
                base += tinta
        return (topo, base, lh)

    for px in sorted(NEEDED_SIZES):
        topo, base, lh = cobertura(px, "regular", ord("g"))
        check(lh > 0, f"{px} px: face regular ausente para a checagem de base")
        check(
            base > 0,
            f"{px} px: o 'g' nao tem tinta na metade de baixo da linha — o "
            f"atlas esta recortado (foi o defeito do par (w, line_height) "
            f"trocado em tools/make-font.py; ver AGENTS.md §3.72)",
        )
        check(topo > 0, f"{px} px: o 'g' nao tem tinta na metade de cima")

    # --- negrito e' mais pesado que regular ------------------------------
    def tinta(px: int, peso: str, texto: str) -> int:
        face = next((s for s in sizes
                     if int(s["px"]) == px and s.get("weight", "regular") == peso),
                    None)
        if face is None:
            return 0
        cells = {int(c["cp"]): c for c in face["cells"]}
        y0 = int(face["y"])
        lh = int(face["line_height"])
        total = 0
        for ch in texto:
            cell = cells.get(ord(ch))
            if cell is None:
                continue
            x0 = int(cell["x"])
            bloco = int(cell.get("w", cell["advance"]))
            for y in range(lh):
                linha = data[(y0 + y) * width + x0:(y0 + y) * width + x0 + bloco]
                total += sum(1 for b in linha if b > 32)
        return total

    for px in sorted(NEEDED_SIZES):
        regular = tinta(px, "regular", "Biblioteca")
        negrito = tinta(px, "bold", "Biblioteca")
        check(
            negrito > regular,
            f"{px} px: o negrito ({negrito}) nao e' mais pesado que o regular "
            f"({regular}) — a face 'bold' provavelmente e' o proprio regular",
        )

    # Nao basta ter bytes: tem de ter cobertura desenhada.
    painted = sum(1 for b in data[: min(len(data), 400000)] if b > 32)
    check(painted > 1000, f"atlas praticamente vazio ({painted} px pintados)")

    return finish()


def finish() -> int:
    if failures == 0:
        print(f"test_font: {checks} checagens, 0 falha(s)")
        return 0
    print(f"test_font: {checks} checagens, {failures} falha(s)", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
