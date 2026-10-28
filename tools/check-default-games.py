#!/usr/bin/env python3
"""Confere a lista de jogos padrao (assets/default-games.json).

A regra que este script protege e a decisao do mantenedor: **so entra na lista
de jogos padrao do sistema o jogo cuja licenca permitir**. A lista e usada
como teste e benchmark de cada aparelho, entao ela tambem nao pode apontar para
projeto inexistente nem para jogo sem bench.

Falha quando:

  * um jogo da lista tem licenca `copyleft` ou `proprietary`;
  * o projeto, a documentacao ou a cena de bench nao existem;
  * o projeto nao tem o modo `--bench` (scripts/bench.gd + tests/run_tests.sh);
  * o jogo tem bench mas o projeto nao tem testes;
  * a lista ficou vazia (o aparelho viria sem nenhum jogo);
  * `excluded` cita jogo que esta em `games` (contradicao).

Uso: check-default-games.py [-v]
"""

from __future__ import annotations

import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, "assets", "default-games.json")

## Licencas que PODEM vir no aparelho.
ALLOWED_KINDS = {"public_domain", "freeware_permission", "clean_room"}

def main(argv: list[str]) -> int:
    verbose = "-v" in argv or "--verbose" in argv

    try:
        with open(MANIFEST, encoding="utf-8") as handle:
            manifest = json.load(handle)
    except OSError as exc:
        print(f"default-games: manifesto ilegivel: {exc}", file=sys.stderr)
        return 1
    except json.JSONDecodeError as exc:
        print(f"default-games: JSON invalido: {exc}", file=sys.stderr)
        return 1

    problems: list[str] = []
    games = manifest.get("games", [])
    excluded = manifest.get("excluded", [])

    if not games:
        problems.append("a lista de jogos padrao esta vazia")

    seen_ids: set[str] = set()
    for game in games:
        game_id = game.get("id", "?")
        if game_id in seen_ids:
            problems.append(f"id duplicado: {game_id}")
        seen_ids.add(game_id)

        license_kind = game.get("license", {}).get("kind", "")
        if license_kind not in ALLOWED_KINDS:
            problems.append(
                f"{game_id}: licenca '{license_kind}' nao pode vir no aparelho "
                f"(permitidas: {', '.join(sorted(ALLOWED_KINDS))})"
            )

        project = os.path.join(ROOT, game.get("project", ""))
        if not os.path.isdir(project):
            problems.append(f"{game_id}: projeto ausente: {game.get('project')}")
            continue

        doc = os.path.join(ROOT, game.get("doc", ""))
        if not os.path.isfile(doc):
            problems.append(f"{game_id}: documentacao ausente: {game.get('doc')}")

        # O modo bench e o que permite medir o aparelho sem humano.
        bench = os.path.join(project, "scripts", "bench.gd")
        if not os.path.isfile(bench):
            problems.append(f"{game_id}: falta scripts/bench.gd (modo --bench)")

        tests = os.path.join(project, "tests", "run_tests.sh")
        if not os.path.isfile(tests):
            problems.append(f"{game_id}: falta tests/run_tests.sh")
        elif not os.access(tests, os.X_OK):
            problems.append(f"{game_id}: tests/run_tests.sh nao e executavel")

        scene = game.get("bench_scene", "")
        scene_path = os.path.join(project, scene.replace("res://", ""))
        if not os.path.isfile(scene_path):
            problems.append(f"{game_id}: cena de bench ausente: {scene}")

        if verbose:
            print(f"  {game_id:<18} {license_kind:<20} {game.get('title')}")

    for item in excluded:
        item_id = item.get("id", "?")
        if item_id in seen_ids:
            problems.append(
                f"{item_id}: esta em 'excluded' e em 'games' ao mesmo tempo"
            )
        if verbose:
            print(f"  (fora) {item_id:<11} {item['license'].get('kind')}")

    if problems:
        for problem in problems:
            print(f"  ERRO {problem}", file=sys.stderr)
        print(
            f"default-games: {len(problems)} problema(s) na lista de jogos padrao",
            file=sys.stderr,
        )
        return 1

    print(f"ok ({len(games)} jogo(s) padrao, {len(excluded)} fora da lista)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
