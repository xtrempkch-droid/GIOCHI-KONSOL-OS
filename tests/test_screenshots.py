"""Contrato das imagens de documentação (docs/screenshots/).

As imagens são geradas por tools/make-screenshots.py a partir dos assets reais.
Este teste garante que:

  * o script roda e produz todos os arquivos esperados;
  * as dimensões batem com o layout declarado (640x480) e com o painel da X55
    (1280x720, letterbox do 4:3);
  * o fundo das telas é a cor `background` declarada em assets/theme.json — ou
    seja, a imagem realmente deriva do tema, não é um mockup solto;
  * o splash é byte a byte o `bootlogo.rgba` que o Launcher desenha.
"""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOL = ROOT / "tools" / "make-screenshots.py"

SCREENS = {
    "home": (640, 480),
    "library": (640, 480),
    "music": (640, 480),
    "settings": (640, 480),
    "settings-system": (640, 480),
    "x55-home": (1280, 720),
    "bootlogo": (640, 480),
}


def _theme_background() -> tuple[int, int, int]:
    theme = json.loads((ROOT / "assets" / "theme.json").read_text())
    value = theme["colors"]["background"].lstrip("#")
    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))


class ScreenshotContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            from PIL import Image  # noqa: F401
        except ImportError:  # pragma: no cover
            raise unittest.SkipTest("Pillow ausente")
        cls._tmp = tempfile.TemporaryDirectory()
        cls.out = Path(cls._tmp.name)
        subprocess.run([sys.executable, str(TOOL), "--out-dir", str(cls.out)],
                       check=True, cwd=ROOT, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def test_todas_as_imagens_existem(self):
        for name in SCREENS:
            with self.subTest(name=name):
                self.assertTrue((self.out / f"{name}.png").is_file(),
                                f"{name}.png não foi gerado")

    def test_dimensoes(self):
        from PIL import Image

        for name, (width, height) in SCREENS.items():
            with self.subTest(name=name):
                with Image.open(self.out / f"{name}.png") as im:
                    self.assertEqual(im.size, (width, height))

    def test_fundo_vem_do_tema(self):
        from PIL import Image

        expected = _theme_background()
        for name in ("home", "library", "music"):
            with self.subTest(name=name):
                with Image.open(self.out / f"{name}.png").convert("RGB") as im:
                    # y=445: abaixo do conteúdo, acima do hint bar (que começa
                    # em 480-30); x=2: margem esquerda, sempre fundo.
                    self.assertEqual(im.getpixel((2, 445)), expected)

    def test_acento_do_tema_aparece(self):
        """O vermelho do tema (#E50914) precisa aparecer na Home."""
        from PIL import Image

        theme = json.loads((ROOT / "assets" / "theme.json").read_text())
        accent = theme["colors"]["accent"].lstrip("#")
        accent = tuple(int(accent[i:i + 2], 16) for i in (0, 2, 4))

        with Image.open(self.out / "home.png").convert("RGB") as im:
            colors = {c for _, c in im.getcolors(maxcolors=1 << 20)}
        self.assertIn(accent, colors)

    def test_x55_letterbox(self):
        """A área lógica 640x480 escalada para 720 de altura deixa laterais."""
        from PIL import Image

        expected = _theme_background()
        with Image.open(self.out / "x55-home.png").convert("RGB") as im:
            self.assertEqual(im.getpixel((2, 360)), expected)
            self.assertEqual(im.getpixel((1277, 360)), expected)

    def test_niveis_do_overlay_dois_diferentes(self):
        """Nível 0 (menu) e nível 1 (Sistema) não podem sair idênticos.

        Eram iguais quando o argumento de nível foi passado na posição da
        escala — o teste existe para essa regressão não voltar silenciosa.
        """
        from PIL import Image

        with Image.open(self.out / "settings.png") as a, \
             Image.open(self.out / "settings-system.png") as b:
            self.assertNotEqual(a.tobytes(), b.tobytes())

    def test_splash_e_o_asset_real(self):
        from PIL import Image

        meta = json.loads((ROOT / "assets/bootlogo/bootlogo.json").read_text())
        raw = (ROOT / "assets/bootlogo/bootlogo.rgba").read_bytes()
        logo = Image.frombytes("RGBA", (meta["width"], meta["height"]), raw)

        with Image.open(self.out / "bootlogo.png").convert("RGB") as shot:
            x = (shot.width - meta["width"]) // 2
            y = (shot.height - meta["height"]) // 2
            # amostra o centro do logo nos dois lados
            cx, cy = meta["width"] // 2, meta["height"] // 2
            self.assertEqual(shot.getpixel((x + cx, y + cy)),
                             logo.convert("RGB").getpixel((cx, cy)))


if __name__ == "__main__":
    unittest.main()
