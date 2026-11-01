#!/usr/bin/env python3
"""Gera a galeria dos temas: docs/temas/<id>-<WxH>.png + contato + indice.

POR QUE EXISTE
--------------
O mantenedor pediu "mais opcoes de layout" e mandou cinco mockups HTML. Escolher
tema lendo JSON e' ruim; escolher olhando e' o que ele ja' fazia com o preview.
Este script rasteriza UMA imagem por tema, pelo mesmo caminho do preview
(tools/preview-ui.py -> dump-geometry -> ui_geometry.cpp), e monta:

  docs/temas/<theme_id>-1280x720.png   a tela da Biblioteca em cada tema
  docs/temas/<theme_id>-640x480.png    a mesma em painel pequeno
  docs/temas/contato-1280x720.png      os seis lado a lado para comparar
  docs/temas/index.json                id, nome e o que muda em cada um

Regra que vale aqui: **nada e' desenhado por este script**. Se ele pintasse por
conta propria, a galeria poderia mostrar um layout que o aparelho nao faz — foi
exatamente o defeito do "preview bugado" (§3.71).

USO
    tools/make-theme-gallery.py            # todos os temas do indice
    tools/make-theme-gallery.py neon_rose  # um so'
"""

import json
import pathlib
import subprocess
import sys

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    sys.exit("erro: Pillow nao instalado (pip install Pillow)")

ROOT = pathlib.Path(__file__).resolve().parent.parent
TEMAS = ROOT / "assets" / "themes"
SAIDA = ROOT / "docs" / "temas"
PREVIEW = ROOT / "tools" / "preview-ui.py"

TAMANHOS = ("1280x720", "640x480")


def carregar(nome: str) -> dict:
    return json.loads((TEMAS / nome).read_text(encoding="utf-8"))


def catalogo() -> dict:
    return carregar("index.json")


def resumo(tema: dict) -> str:
    """Uma linha dizendo o que MUDA em relacao ao tema padrao.

    Interessa ao mantenedor escolher por diferenca, nao por paleta: duas saidas
    com as mesmas cores e densidades diferentes sao opcoes diferentes.
    """
    m = tema["metrics"]
    partes = [f"densidade {m['density']}"]
    if m.get("tile_border"):
        partes.append(f"borda {m['tile_border']}px")
    partes.append(f"canto {m['tile_corner_radius']}px")
    if tema.get("features", {}).get("hints") == "completo":
        partes.append("dicas com teclado")
    return ", ".join(partes)


def gerar(theme_id: str, verbose: bool = True) -> list[pathlib.Path]:
    saidas = []
    for tam in TAMANHOS:
        destino = SAIDA / f"{theme_id}-{tam}.png"
        cmd = [sys.executable, str(PREVIEW), "--size", tam, "--tab", "1",
               "--items", "15", "--theme", theme_id, "--out", str(destino)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(f"erro: preview falhou para {theme_id} {tam}:\n"
                     f"{r.stdout}{r.stderr}")
        if verbose:
            print(f"  {destino.relative_to(ROOT)}")
        saidas.append(destino)
    return saidas


def contato(ids: list[str]) -> pathlib.Path:
    """Folha 2x3 com os temas, reduzidos a metade, para comparar de uma vez."""
    largura, altura = 640, 360
    colunas = 2
    linhas = (len(ids) + colunas - 1) // colunas
    folha = Image.new("RGB", (largura * colunas, altura * linhas), (0, 0, 0))

    for i, tid in enumerate(ids):
        src = SAIDA / f"{tid}-1280x720.png"
        im = Image.open(src).convert("RGB").resize((largura, altura),
                                                   Image.LANCZOS)
        folha.paste(im, ((i % colunas) * largura, (i // colunas) * altura))

    destino = SAIDA / "contato-1280x720.png"
    folha.save(destino)
    return destino


def main() -> int:
    filtro = sys.argv[1] if len(sys.argv) > 1 else None
    cat = catalogo()

    ids = [t["theme_id"] for t in cat["themes"]]
    if filtro:
        if filtro not in ids:
            sys.exit(f"erro: tema '{filtro}' nao esta em "
                     f"assets/themes/index.json ({', '.join(ids)})")
        ids = [filtro]

    SAIDA.mkdir(parents=True, exist_ok=True)

    indice = {"schema_version": "1.0", "default": cat["default"], "themes": []}

    print(f"temas: {len(ids)}")
    for tid in ids:
        tema = carregar(f"{tid}.json")
        if tema["theme_id"] != tid:
            sys.exit(f"erro: {tid}.json declara theme_id "
                     f"'{tema['theme_id']}'")
        gerar(tid)
        indice["themes"].append({
            "theme_id": tid,
            "name": tema["name"],
            "origin": tema.get("origin", ""),
            "muda": resumo(tema),
            "images": {
                "1280x720": f"docs/temas/{tid}-1280x720.png",
                "640x480": f"docs/temas/{tid}-640x480.png",
            },
        })

    if not filtro:
        folha = contato(ids)
        print(f"  {folha.relative_to(ROOT)}")

    (SAIDA / "index.json").write_text(
        json.dumps(indice, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    print(f"  {(SAIDA / 'index.json').relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
