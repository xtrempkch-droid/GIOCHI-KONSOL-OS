#!/usr/bin/env python3
"""Verifica links relativos em Markdown (quebrados falham o lint).

Confere duas coisas, as duas silenciosas quando erradas:

  * o ARQUIVO alvo existe (inclusive em <img src="...">);
  * a ANCORA (#secao) existe no arquivo alvo — um link para `#bootloader` que
    nao existe mais no destino leva o leitor para o topo da pagina, sem erro.
"""

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
HEADING = re.compile(r"^#{1,6}\s+(.*)$", re.M)


def slug(heading: str) -> str:
    """Slug no estilo do GitHub: minusculas, pontuacao fora, espaco -> '-'."""
    text = heading.strip().lower()
    text = re.sub(r"[^\w\s-]", "", text, flags=re.UNICODE)
    return re.sub(r"\s+", "-", text)


def anchors(path: str) -> set[str]:
    try:
        text = open(path, encoding="utf-8").read()
    except OSError:
        return set()
    return {slug(h) for h in HEADING.findall(text)}


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
            if link.startswith(("http://", "https://", "mailto:")):
                continue
            # Ancora no proprio arquivo.
            if link.startswith("#"):
                if link[1:] and link[1:] not in anchors(rel):
                    print(f"ancora inexistente: {rel} -> {link}",
                          file=sys.stderr)
                    broken += 1
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
                continue
            anchor = link.split("#", 1)[1] if "#" in link else ""
            if anchor and os.path.isfile(full):
                if anchor not in anchors(full):
                    print(f"ancora inexistente: {rel} -> {link}", file=sys.stderr)
                    broken += 1
    if broken:
        print(f"check-links: {broken} link(s) quebrado(s)", file=sys.stderr)
        return 1
    print(f"ok ({checked} arquivo(s))")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
