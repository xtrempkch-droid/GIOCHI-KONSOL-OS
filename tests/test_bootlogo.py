"""Valida os assets de bootsplash (formato e dimensoes).

Roda no host sem SDL2: confere o contrato consumido pelo render.cpp
(bootlogo.rgba = RGBA8888 top-down, tamanho == width*height*4) e o BMP
do U-Boot (24-bit, tamanho do painel do aparelho).
"""

from __future__ import annotations

import json
import struct
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BOOTLOGO = ROOT / "assets" / "bootlogo"
DEVICES = ROOT / "buildroot" / "board" / "giochi" / "devices"


def devices_panels() -> dict[str, tuple[int, int]]:
    panels: dict[str, tuple[int, int]] = {}
    for conf in sorted(DEVICES.glob("*.conf")):
        values = {}
        for line in conf.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip().strip('"')
        if "GIOCHI_ID" not in values:
            continue
        width, height = (int(v) for v in values["GIOCHI_PANEL"].lower().split("x"))
        if values.get("GIOCHI_ROTATION") in ("90", "270"):
            width, height = height, width
        panels[values["GIOCHI_ID"]] = (width, height)
    return panels


class BootlogoTest(unittest.TestCase):
    def test_rgba_matches_metadata(self):
        meta_path = BOOTLOGO / "bootlogo.json"
        rgba_path = BOOTLOGO / "bootlogo.rgba"
        self.assertTrue(meta_path.is_file(), "rode 'make bootlogo'")
        self.assertTrue(rgba_path.is_file(), "rode 'make bootlogo'")

        meta = json.loads(meta_path.read_text(encoding="utf-8"))
        self.assertEqual(meta["format"], "rgba8888")
        self.assertEqual(meta["order"], "top-down")

        data = rgba_path.read_bytes()
        expected = meta["width"] * meta["height"] * 4
        self.assertEqual(len(data), expected)
        self.assertEqual(meta["stride"], meta["width"] * 4)

        # Teto de VRAM do splash: precisa caber com folga nos 4 MB da UI.
        self.assertLess(len(data), 4 * 1024 * 1024)

        # Nao pode ser uma imagem vazia/transparente.
        opaque = any(data[i] != 0 for i in range(3, len(data), 4))
        self.assertTrue(opaque, "splash totalmente transparente")

    def test_splash_bmp_per_device(self):
        panels = devices_panels()
        self.assertTrue(panels, "nenhum devices/*.conf encontrado")

        for device, (width, height) in panels.items():
            bmp = BOOTLOGO / device / "splash.bmp"
            if not bmp.is_file():
                self.skipTest(f"{device}: splash.bmp nao gerado")
            raw = bmp.read_bytes()
            self.assertEqual(raw[:2], b"BM", f"{device}: nao e BMP")

            # cabecalho BITMAPINFOHEADER (offset 14)
            header_size = struct.unpack_from("<I", raw, 14)[0]
            self.assertEqual(header_size, 40, f"{device}: header inesperado")
            bmp_w, bmp_h = struct.unpack_from("<ii", raw, 18)
            bpp = struct.unpack_from("<H", raw, 28)[0]

            self.assertEqual(bmp_w, width, f"{device}: largura")
            self.assertEqual(abs(bmp_h), height, f"{device}: altura")
            self.assertEqual(bpp, 24, f"{device}: profundidade (U-Boot aceita 24bpp)")

    def test_generator_exists(self):
        self.assertTrue((ROOT / "tools" / "make-bootlogo.py").is_file())
        doc = (ROOT / "docs" / "BOOTSPLASH.md").read_text(encoding="utf-8")
        self.assertIn("GIOCHI_NO_SPLASH", doc)
        self.assertIn("PRE-PRONTO", doc.replace("pré-pronto", "PRE-PRONTO").upper())


if __name__ == "__main__":
    unittest.main()
