#!/usr/bin/env python3
"""Verifica links relativos em Markdown (quebrados falham o lint)."""

from __future__ import annotations

import glob
import os
import re
import sys

# A especificacao normativa vive em docs/architecture-*.md, nao no README.
# Por isso os arquivos sao DESCOBERTOS, e nao listados a mao: a lista antiga
# esquecia justamente a arquitetura (e o hardware), deixando a documentacao
# mais importante sem verificacao de links.
FILES = sorted(
    glob.glob("*.md")
    + glob.glob("docs/*.md")
    + ["buildroot/board/giochi/dts/README"]
)

LINK = re.compile(r"\]\(([^)\s]+)\)")
# Imagens/HTML tambem contam: um <img src="..."> quebrado passa despercebido
# pelo padrao de markdown e quebra a pagina no GitHub.
HTML_SRC = re.compile(r"<img\s[^>]*src=\"([^\"]+)\"", re.I)


def main() -> int:
    broken = 0
    checked = 0
    for rel in FILES:
        if not os.path.exists(rel):
            continue
        checked += 1
        base = os.path.dirname(rel) or "."
        text = open(rel, encoding="utf-8").read()
        for link in LINK.findall(text) + HTML_SRC.findall(text):
            if link.startswith(("http://", "https://", "mailto:", "#")):
                continue
            path = link.split("#", 1)[0]
            if not path:
                continue
            # Placeholder de exemplo em documentacao ("...", "${VAR}").
            if path in ("...", "\u2026") or "..." in path or "{" in path:
                continue
            full = os.path.normpath(os.path.join(base, path))
            if not os.path.exists(full):
                print(f"link quebrado: {rel} -> {link}", file=sys.stderr)
                broken += 1
    if broken:
        print(f"check-links: {broken} link(s) quebrado(s)", file=sys.stderr)
        return 1
    print(f"ok ({checked} arquivo(s))")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
