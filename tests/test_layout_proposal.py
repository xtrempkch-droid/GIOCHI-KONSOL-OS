"""Contrato dos renders da proposta de layout (docs/proposta/).

As imagens são geradas por tools/make-layout-proposal.py a partir de
assets/theme.json + os assets de cada aba. Este teste garante que:

  * o gerador roda e sai com 0 — ou seja, **nenhuma** asserção geométrica dele
    falhou (texto que não cabe, item de hint bar que colide, caixa que invade
    topbar/hint bar, grade fora da área útil);
  * as 5 abas existem nos dois perfis de painel, com as dimensões do painel;
  * a imagem deriva do tema: a cor `background` e o `accent` aparecem;
  * Loja e Ports não saem idênticas (um copy-paste entre as duas telas passaria
    batido em qualquer revisão visual);
  * **nenhum pixel branco puro** — o tema não tem branco e o Pillow >= 11
    desenha contorno branco quando `fill` e `outline` são None (foi assim que
    linhas de faixa ganharam borda branca). O teste existe para essa regressão
    não voltar em silêncio.

Não usa tempfile: no Windows o diretório de `mkdtemp` nasce com DACL restritiva
(mode 0o700) e o processo filho do gerador não consegue escrever nele. O
diretório de saída é `build/test-proposta/`, criado com `mkdir` normal.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOL = ROOT / "tools" / "make-layout-proposal.py"
OUT = ROOT / "build" / "test-proposta"

TABS = ("home", "library", "music", "store", "ports")
PANELS = {"default": (640, 480), "x55": (1280, 720)}


def _theme_color(name: str) -> tuple[int, int, int]:
    theme = json.loads((ROOT / "assets" / "theme.json").read_text())
    value = theme["colors"][name].lstrip("#")
    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))


class LayoutProposalContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            from PIL import Image  # noqa: F401
        except ImportError:  # pragma: no cover
            raise unittest.SkipTest("Pillow ausente")

        if OUT.exists():
            shutil.rmtree(OUT)
        OUT.mkdir(parents=True)
        cls.proc = subprocess.run(
            [sys.executable, str(TOOL), "--out-dir", str(OUT), "--quiet"],
            cwd=ROOT, capture_output=True, text=True)

    @classmethod
    def tearDownClass(cls):
        if OUT.exists():
            shutil.rmtree(OUT, ignore_errors=True)

    def path(self, tab: str, panel: str) -> Path:
        return OUT / f"{tab}-{PANELS[panel][0]}x{PANELS[panel][1]}.png"

    def test_gerador_sem_violacao_geometrica(self):
        self.assertEqual(self.proc.returncode, 0, self.proc.stderr)

    def test_todas_as_abas_nos_dois_paineis(self):
        for panel in PANELS:
            for tab in TABS:
                with self.subTest(panel=panel, tab=tab):
                    self.assertTrue(self.path(tab, panel).is_file(),
                                    f"falta {self.path(tab, panel).name}")

    def test_dimensoes_do_painel(self):
        from PIL import Image

        for panel, size in PANELS.items():
            for tab in TABS:
                with self.subTest(panel=panel, tab=tab):
                    with Image.open(self.path(tab, panel)) as im:
                        self.assertEqual(im.size, size)

    def test_deriva_do_tema(self):
        from PIL import Image

        background = _theme_color("background")
        accent = _theme_color("accent")
        for panel in PANELS:
            for tab in TABS:
                with self.subTest(panel=panel, tab=tab):
                    with Image.open(self.path(tab, panel)).convert("RGB") as im:
                        colors = {c for _, c in im.getcolors(maxcolors=1 << 20)}
                    self.assertIn(background, colors)
                    self.assertIn(accent, colors)

    def test_loja_e_ports_nao_sao_a_mesma_tela(self):
        from PIL import Image

        for panel in PANELS:
            with self.subTest(panel=panel):
                a = self.path("store", panel).read_bytes()
                b = self.path("ports", panel).read_bytes()
                self.assertNotEqual(a, b)

    def test_nenhum_pixel_branco(self):
        """Tema sem branco: branco puro indica contorno de `fill=None`."""
        from PIL import Image

        for panel in PANELS:
            for tab in TABS:
                with self.subTest(panel=panel, tab=tab):
                    with Image.open(self.path(tab, panel)).convert("RGB") as im:
                        colors = {c for _, c in im.getcolors(maxcolors=1 << 20)}
                    self.assertNotIn((255, 255, 255), colors)


if __name__ == "__main__":
    unittest.main()
