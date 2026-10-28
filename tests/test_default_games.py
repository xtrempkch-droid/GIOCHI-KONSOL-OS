"""Contrato dos jogos padrao do sistema (assets/default-games.json).

A lista de jogos padrao e usada como **teste e benchmark de cada aparelho**,
entao ela precisa ser mais do que uma lista de nomes:

  * nenhum jogo com licenca que nao permita distribuicao (a decisao do
    mantenedor: GPL/proprietario fica fora);
  * todo jogo da lista tem projeto, documentacao, cena, modo bench e testes;
  * nenhum jogo esta em 'games' e em 'excluded' ao mesmo tempo;
  * 'excluded' explica o motivo (para o proximo agente nao reverter sem saber);
  * a lista nao fica vazia (o aparelho viria sem jogo nenhum).

Roda o mesmo script do lint, para o teste e o CI nao divergirem.
"""

from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "assets" / "default-games.json"
SCHEMA = ROOT / "schemas" / "default-games.schema.json"
CHECKER = ROOT / "tools" / "check-default-games.py"

ALLOWED_KINDS = {"public_domain", "freeware_permission", "clean_room"}


def load_manifest() -> dict:
    return json.loads(MANIFEST.read_text(encoding="utf-8"))


class DefaultGames(unittest.TestCase):
    def test_checker_passa(self) -> None:
        result = subprocess.run([sys.executable, str(CHECKER)],
                                cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0,
                         msg=result.stdout + result.stderr)

    def test_nenhuma_licenca_proibida(self) -> None:
        for game in load_manifest()["games"]:
            kind = game["license"]["kind"]
            self.assertIn(kind, ALLOWED_KINDS,
                          f"{game['id']}: licenca '{kind}' nao pode vir no aparelho")

    def test_todo_jogo_tem_evidencia_de_licenca(self) -> None:
        # "e dominio publico" sem dizer por que nao serve: o proximo agente
        # precisa poder conferir sem refazer a pesquisa.
        for game in load_manifest()["games"]:
            self.assertGreaterEqual(len(game["license"]["evidence"]), 10,
                                    f"{game['id']}: evidencia de licenca curta")

    def test_excluidos_explicam_motivo(self) -> None:
        manifest = load_manifest()
        ids = {game["id"] for game in manifest["games"]}
        for item in manifest.get("excluded", []):
            self.assertNotIn(item["id"], ids,
                             f"{item['id']} esta em games e em excluded")
            self.assertGreaterEqual(len(item["reason"]), 10,
                                    f"{item['id']}: motivo curto")

    def test_gpl_fica_fora(self) -> None:
        # Trava a decisao do mantenedor (2026-10-27): GPL nao entra na lista.
        manifest = load_manifest()
        excluded = {item["license"]["id"]: item for item in manifest.get("excluded", [])}
        self.assertIn("GPL-3.0", excluded,
                      "o jogo GPL-3.0 saiu da lista; registre isso em 'excluded'")
        for game in manifest["games"]:
            self.assertNotEqual(game["license"]["id"], "GPL-3.0")

    def test_schema_declara_os_kinds(self) -> None:
        schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
        kinds = set(
            schema["properties"]["games"]["items"]["properties"]["license"]
            ["properties"]["kind"]["enum"]
        )
        self.assertEqual(kinds, ALLOWED_KINDS | {"copyleft", "proprietary"})

    def test_bench_registrado_no_manifesto(self) -> None:
        for game in load_manifest()["games"]:
            self.assertTrue(game["bench_args"],
                            f"{game['id']}: sem argumentos de bench")
            self.assertIn("--bench", game["bench_args"],
                          f"{game['id']}: bench_args sem --bench")


if __name__ == "__main__":
    unittest.main()
