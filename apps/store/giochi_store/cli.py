"""Linha de comando da Loja (lado PC)."""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path

from .catalogo import catalogo, novidades, sha256_de, varrer


def escrever(destino: Path, nome: str, doc: dict) -> Path:
    destino.mkdir(parents=True, exist_ok=True)
    alvo = destino / nome
    alvo.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n",
                    encoding="utf-8")
    return alvo


def instalar(pacotes, games: Path, dry_run: bool) -> tuple[int, int]:
    """Copia os pacotes validos para `games`, conferindo o sha256 no DESTINO.

    Conferir na origem nao serve: copiar e confiar no `shutil.copy` nao pega
    cartao que grava lixo — o mesmo motivo do tools/verify-card.py (§3.42).
    """
    copiados = falhas = 0

    if not dry_run:
        games.mkdir(parents=True, exist_ok=True)

    for p in pacotes:
        if not p.ok:
            falhas += 1
            print(f"  pulado   {p.caminho.name:32s} {p.motivo}")
            continue
        if dry_run:
            print(f"  copiaria {p.caminho.name:32s} {p.tamanho // 1024} KB")
            copiados += 1
            continue

        alvo = games / p.caminho.name
        shutil.copy2(p.caminho, alvo)
        if sha256_de(alvo) != p.sha256:
            alvo.unlink(missing_ok=True)
            falhas += 1
            print(f"  ERRO     {p.caminho.name:32s} copia nao bate o sha256")
            continue
        copiados += 1
        print(f"  copiado  {p.caminho.name:32s} {p.tamanho // 1024} KB")

    return copiados, falhas


def cmd_catalogo(args) -> int:
    origem = Path(args.src).expanduser()
    if not origem.is_dir():
        print(f"giochi-store: '{origem}' nao e' um diretorio", file=sys.stderr)
        return 2

    pacotes = varrer(origem)
    cat = catalogo(pacotes)
    novo = novidades(pacotes)

    if args.dry_run:
        print(f"(dry-run) {len(pacotes)} pacote(s) em {origem}")
        for p in pacotes:
            print(f"  {'ok    ' if p.ok else 'recusa'} {p.caminho.name:30s} "
                  f"{p.motivo or p.titulo}")
        return 0

    destino = Path(args.out).expanduser()
    c = escrever(destino, "store.json", cat)
    n = escrever(destino, "news.json", novo)
    print(f"{len(cat['itens'])} de {len(pacotes)} pacote(s) no catalogo")
    print(f"  {c}")
    print(f"  {n}")

    for p in pacotes:
        if not p.ok:
            print(f"  recusado {p.caminho.name:30s} {p.motivo}")
    return 0


def cmd_instalar(args) -> int:
    pacotes = varrer(Path(args.src).expanduser())
    if not pacotes:
        print(f"giochi-store: nenhum .giochi em '{args.src}'", file=sys.stderr)
        return 2
    copiados, falhas = instalar(pacotes, Path(args.games).expanduser(),
                                args.dry_run)
    print(f"{copiados} instalado(s), {falhas} recusado(s)")
    return 0 if falhas == 0 else 1


def cmd_listar(args) -> int:
    pacotes = varrer(Path(args.src).expanduser())
    if args.json:
        print(json.dumps(catalogo(pacotes), indent=2, ensure_ascii=False))
        return 0
    if not pacotes:
        print(f"nenhum .giochi em '{args.src}'", file=sys.stderr)
        return 0
    for p in pacotes:
        if p.ok:
            print(f"  {p.titulo:28s} {str(p.meta.get('version', '')):8s} "
                  f"{str(p.meta.get('runtime', '')):10s} "
                  f"{p.tamanho // 1024:6d} KB")
        else:
            print(f"  {p.caminho.name:28s} RECUSADO: {p.motivo}")
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        prog="giochi-store",
        description="Catalogo e instalacao da Loja (lado PC). "
                    "O console nao empacota nem assina nada.")
    sub = ap.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("catalogo", help="gera store.json + news.json")
    c.add_argument("--src", required=True, help="pasta com os .giochi")
    c.add_argument("--out", required=True,
                   help="destino (ex.: /media/eu/GIOCHI_USER/.cache)")
    c.add_argument("--dry-run", action="store_true")
    c.set_defaults(func=cmd_catalogo)

    i = sub.add_parser("instalar", help="copia para GIOCHI_USER/games")
    i.add_argument("--src", required=True)
    i.add_argument("--games", required=True,
                   help="ex.: /media/eu/GIOCHI_USER/games")
    i.add_argument("--dry-run", action="store_true")
    i.set_defaults(func=cmd_instalar)

    l = sub.add_parser("listar", help="mostra o catalogo em texto")
    l.add_argument("--src", required=True)
    l.add_argument("--json", action="store_true")
    l.set_defaults(func=cmd_listar)

    args = ap.parse_args(argv)
    return args.func(args)
