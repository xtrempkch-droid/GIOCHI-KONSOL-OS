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
  5. o atlas tem pixel desenhado — um atlas vazio passaria 1..4.
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
        check(len(cells) >= 95, f"{size['px']} px: {len(cells)} glifos (< 95)")

        cps = {int(c["cp"]) for c in cells}
        for cp in range(32, 127):
            check(cp in cps, f"{size['px']} px: falta o glifo {chr(cp)!r}")

        for cell in cells:
            x = int(cell["x"])
            adv = int(cell["advance"])
            check(adv > 0, f"{size['px']} px: avanco zero no cp {cell['cp']}")
            check(
                x >= 0 and x + adv <= width,
                f"{size['px']} px: glifo {cell['cp']} fora do atlas "
                f"({x}+{adv} > {width})",
            )

    check(
        int(meta.get("replacement_cp", 0)) > 0,
        "sem code point de substituicao: titulo em UTF-8 sumiria calado",
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
