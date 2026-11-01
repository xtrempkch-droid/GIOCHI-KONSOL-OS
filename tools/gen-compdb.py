#!/usr/bin/env python3
"""Gera `compile_commands.json` a partir das flags do Makefile.

POR QUE ESTE SCRIPT EXISTE
--------------------------
O editor (VS Code/Copilot, clangd, clang-tidy, ccls) nao le o Makefile. Sem
`compile_commands.json` ele adivinha os caminhos de include, e o sintoma e' um
erro que NAO existe no build:

    apps/launcher/input.hpp:12: 'giochi_hotkey.h' file not found

O header esta' em `src/common/`, o Makefile passa `-Isrc/common` e o `make`
compila limpo — quem estava errado era o editor, por falta de informacao.

A alternativa comum (escrever a lista de `includePath` num
`.vscode/c_cpp_properties.json`) e' uma SEGUNDA copia das flags: no dia em que
alguem mover um diretorio, o build quebra (e a gente conserta) e o editor
continua apontando para o lugar antigo, ou o contrario. Este repositorio ja'
pagou esse preco duas vezes (AGENTS.md §3.20 e §3.124).

Aqui as flags vem do PROPRIO Makefile, por variavel de ambiente, e a lista de
fontes tambem — nenhum caminho e' reescrito neste arquivo.

USO (o Makefile chama; nao precisa rodar a mao)
    GIOCHI_CC=cc GIOCHI_CXX=c++ \
    GIOCHI_CFLAGS='...' GIOCHI_CXXFLAGS='...' \
    GIOCHI_ASSET_DIR=/caminho/assets \
    python3 tools/gen-compdb.py \
        --c src/common/a.c ... --cxx apps/launcher/main.cpp ... \
        --test-cxx tests/test_x.cpp ...
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import shlex
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def argumentos(texto: str) -> list[str]:
    """Divide a string de flags em argv como o shell faria.

    `shlex.split` e' o que preserva `-DGIOCHI_SOC="x55"` como UM argumento com
    aspas internas — que e' exatamente o formato que o compilador recebe da
    recipe do Makefile (o shell come as aspas EXTERNAS e mantem as de dentro).
    """
    return shlex.split(texto)


def entradas(fontes: list[str], compilador: str, flags: list[str],
             extras: list[str] | None = None) -> list[dict]:
    saida = []
    for fonte in fontes:
        argv = [compilador, *flags, *(extras or []), "-c", fonte]
        saida.append({
            "directory": str(ROOT),
            "file": str(ROOT / fonte),
            "command": " ".join(shlex.quote(a) for a in argv),
            "arguments": argv,
            "output": "/dev/null",
        })
    return saida


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--c", nargs="*", default=[], help="fontes C do projeto")
    p.add_argument("--cxx", nargs="*", default=[], help="fontes C++ do projeto")
    p.add_argument("--test-cxx", nargs="*", default=[],
                   help="fontes C++ de teste (recebem -DGIOCHI_ASSET_DIR)")
    p.add_argument("--saida", default="compile_commands.json")
    args = p.parse_args()

    cc = os.environ.get("GIOCHI_CC", "cc")
    cxx = os.environ.get("GIOCHI_CXX", "c++")
    cflags = argumentos(os.environ.get("GIOCHI_CFLAGS", ""))
    cxxflags = argumentos(os.environ.get("GIOCHI_CXXFLAGS", ""))
    asset_dir = os.environ.get("GIOCHI_ASSET_DIR", "")

    if not cflags and not cxxflags:
        print("gen-compdb: GIOCHI_CFLAGS/GIOCHI_CXXFLAGS vazios; nada a fazer",
              file=sys.stderr)
        return 1

    testes_extra = ([f'-DGIOCHI_ASSET_DIR="{asset_dir}"'] if asset_dir else [])

    banco = [
        *entradas(args.c, cc, cflags),
        *entradas(args.cxx, cxx, cxxflags),
        *entradas(args.test_cxx, cxx, cxxflags, testes_extra),
    ]

    # Um arquivo que nao existe (fonte removido e a lista nao acompanhou) gera
    # entrada fantasma: o editor mostra erro no arquivo errado e ninguem
    # entende. Melhor avisar agora.
    fantasmas = [e["file"] for e in banco if not os.path.isfile(e["file"])]
    if fantasmas:
        for f in fantasmas:
            print(f"gen-compdb: fonte listado nao existe: {f}", file=sys.stderr)
        return 1

    destino = pathlib.Path(args.saida)
    if not destino.is_absolute():
        destino = ROOT / destino
    destino.write_text(json.dumps(banco, indent=1, ensure_ascii=False) + "\n",
                       encoding="utf-8")
    print(f"gen-compdb: {len(banco)} entradas em {destino.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
