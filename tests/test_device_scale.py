"""O override do aparelho é o layout canônico ×1,5 — e nada além disso.

`assets/devices/x55/*.json` já esteve fora de escala: cada campo tinha sido
escolhido à mão, com sete fatores implícitos diferentes (1,28 a 2,00). Isso não
é cosmético: 24 texturas de 288×162 RGBA dão 4,48 MB, acima do teto de 4 MB da
§4.1.6, enquanto o perfil ×1,5 (228×128) dá 2,80 MB.

Este teste fecha a porta: para **cada** número do arquivo do aparelho, ele exige
uma destas três coisas:

  * o valor é `round(base × 1,5)` (métrica que escala com o painel);
  * o valor está numa lista explícita de política (`vram_budget_kb`, `rows`,
    `max_textures_per_tab`, …) e é **igual** ao do arquivo base;
  * o valor é um dos especiais do painel (`screen.*`, `columns`), conferidos
    contra as dimensões do painel e contra a aritmética da grade.

Número novo no arquivo do aparelho que não se encaixe em nenhum dos três falha
o teste — é o ponto: a próxima métrica entra aqui de propósito, não por engano.
"""

from __future__ import annotations

import json
import math
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
DEVICE = "x55"
SCALE = 1.5

# Dimensões lógicas do painel (720x1280 com rotation=270, §F8).
PANEL = {"screen.width": 1280, "screen.height": 720}

# Política de produto, não métrica de layout: o valor é o mesmo do arquivo base.
# Comparado pelo NOME do campo (último segmento do caminho): a mesma política
# aparece em arquivos diferentes (`rows` na biblioteca, `max_items` na home).
POLICY = {
    "vram_budget_kb",
    "max_textures_per_tab",
    "max_items",
    "rows",
    "overscan_rows",
    "title_preview_delay_ms",
    "home_short_press_ms",
    "exit_game_combo_hold_ms",
    "rk3326",
    "rk3566",
}

FILES = ("layout.json", "home.json", "library.json", "music.json")


def flatten(node, prefix="") -> dict[str, object]:
    out: dict[str, object] = {}
    if isinstance(node, dict):
        for key, value in node.items():
            out.update(flatten(value, f"{prefix}{key}." if prefix else f"{key}."))
    elif isinstance(node, list):
        # Listas de string (ordem das abas, widgets) não entram na conta.
        return out
    else:
        out[prefix[:-1]] = node
    return out


def scaled(base: int) -> int:
    """Mesma regra do gerador de renders: meio para cima, não bancário."""
    return int(math.floor(base * SCALE + 0.5))


class DeviceScaleContract(unittest.TestCase):
    def test_escala_15(self):
        for name in FILES:
            base_raw = json.loads((ASSETS / name).read_text(encoding="utf-8"))
            device_raw = json.loads(
                (ASSETS / "devices" / DEVICE / name).read_text(encoding="utf-8"))
            base = flatten(base_raw)
            device = flatten(device_raw)

            for path, value in device.items():
                if isinstance(value, (str, bool)):
                    continue  # ids, textos, caminhos e flags: comparados abaixo
                with self.subTest(arquivo=name, campo=path):
                    self.assertIn(path, base, "campo novo sem par no arquivo base")

                    if path in PANEL:
                        self.assertEqual(value, PANEL[path])
                    elif path.split(".")[-1] in POLICY:
                        self.assertEqual(value, base[path],
                                         "política não deveria mudar no override")
                    elif path == "regions.grid.columns":
                        self.assertEqual(value, 5)
                    else:
                        self.assertIsInstance(base[path], (int, float))
                        self.assertEqual(value, scaled(base[path]),
                                         f"esperado round({base[path]} x 1,5)")

            # Textos e ids têm de ser idênticos: o override troca métrica, não
            # conteúdo (fonte, socket, fallback, nomes de widget).
            for path, value in device.items():
                if isinstance(value, str) and path != "tab":
                    with self.subTest(arquivo=name, texto=path):
                        self.assertEqual(value, base.get(path))

    def test_colunas_cabem(self):
        grid = json.loads(
            (ASSETS / "devices" / DEVICE / "library.json").read_text())["regions"]["grid"]
        tile_w = grid["tile_size"]["width"]
        gap = scaled(8)  # espacamento interno da grade, base 8
        columns = grid["columns"]
        need = columns * tile_w + (columns - 1) * gap
        self.assertLessEqual(need, 1280 - 2 * scaled(4),
                            f"{columns} colunas de {tile_w}px pedem {need}px")

    def test_orcamento_de_vram(self):
        """24 thumbnails do painel têm de caber no teto de 4 MB da §4.1.6."""
        library = json.loads(
            (ASSETS / "devices" / DEVICE / "library.json").read_text())
        grid = library["regions"]["grid"]
        thumb = grid["thumbnail_size"]
        total = library["max_textures_per_tab"] * thumb["width"] * thumb["height"] * 4
        self.assertLessEqual(total, 4 * 1024 * 1024,
                             f"{total} B de thumbnail estoura o teto de 4 MB")


if __name__ == "__main__":
    unittest.main()
