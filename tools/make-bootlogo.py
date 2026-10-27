#!/usr/bin/env python3
"""Giochi Konsol OS v1.2 — gera os assets de bootsplash a partir do logo.

Produz, para cada aparelho de buildroot/board/giochi/devices/*.conf:

  assets/bootlogo/<device>/splash.bmp   24-bit BMP do tamanho do painel
                                       (fundo do tema + logo centralizado).
                                       Consumido pelo U-Boot (CONFIG_SPLASH_*).
  assets/bootlogo/bootlogo.rgba         RGBA cru (max 640x360, aspect-fit),
  assets/bootlogo/bootlogo.json         dimensoes + origem. Consumido pelo
                                       Launcher no primeiro frame.

Seguro para rodar sem Pillow? Nao: o script sai com codigo 3 e o build segue
sem splash (o suporte em runtime trata a ausencia como "sem splash").

Uso:
    tools/make-bootlogo.py [--source docs/logo.jpg] [--out assets/bootlogo]
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover - depende do ambiente de build
    Image = None  # type: ignore[assignment]

EXIT_OK = 0
EXIT_USAGE = 2
EXIT_NO_PIL = 3

# Fundo do tema escuro (assets/theme.json -> colors.background).
BACKGROUND = (0x0F, 0x0F, 0x0F)

# Teto do RGBA do Launcher: 640x360x4 = 900 KB (bem abaixo do teto de VRAM
# de UI de 4 MB). O splash e desenhado com escala para o tamanho da tela.
RGBA_MAX_W = 640
RGBA_MAX_H = 360


def device_panels(devices_dir: Path) -> dict[str, tuple[int, int]]:
    """Le GIOCHI_PANEL/GIOCHI_ROTATION de cada <aparelho>.conf."""
    panels: dict[str, tuple[int, int]] = {}
    for conf in sorted(devices_dir.glob("*.conf")):
        values: dict[str, str] = {}
        for line in conf.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip().strip('"')

        device = values.get("GIOCHI_ID")
        panel = values.get("GIOCHI_PANEL", "640x480")
        rotation = values.get("GIOCHI_ROTATION", "0")
        if not device:
            continue
        try:
            width, height = (int(v) for v in panel.lower().split("x", 1))
        except ValueError:
            print(f"bootlogo: painel invalido em {conf.name}: {panel}",
                  file=sys.stderr)
            continue
        # Painel rotacionado 90/270 e usado na orientacao logica do splash.
        if rotation in ("90", "270"):
            width, height = height, width
        panels[device] = (width, height)
    return panels


def fit(image: "Image.Image", box_w: int, box_h: int) -> "Image.Image":
    """Redimensiona preservando aspecto para caber em box_w x box_h."""
    scale = min(box_w / image.width, box_h / image.height)
    size = (max(1, int(image.width * scale)), max(1, int(image.height * scale)))
    return image.resize(size, Image.LANCZOS)


def compose_splash(logo: "Image.Image", width: int, height: int) -> "Image.Image":
    """Fundo do tema + logo centralizado, com margem de 6%."""
    canvas = Image.new("RGB", (width, height), BACKGROUND)
    margin_w = int(width * 0.94)
    margin_h = int(height * 0.94)
    scaled = fit(logo, margin_w, margin_h)
    canvas.paste(scaled, ((width - scaled.width) // 2,
                          (height - scaled.height) // 2))
    return canvas


def write_rgba(logo: "Image.Image", out_dir: Path) -> tuple[int, int]:
    """Grava bootlogo.rgba + bootlogo.json (RGBA, bottom-up? nao: top-down)."""
    scaled = fit(logo.convert("RGBA"), RGBA_MAX_W, RGBA_MAX_H)
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "bootlogo.rgba").write_bytes(scaled.tobytes())
    meta = {
        "schema_version": "1.0",
        "format": "rgba8888",
        "order": "top-down",
        "width": scaled.width,
        "height": scaled.height,
        "stride": scaled.width * 4,
        "source": "docs/logo.jpg",
        "note": "Gerado por tools/make-bootlogo.py. Nao editar a mao.",
    }
    (out_dir / "bootlogo.json").write_text(
        json.dumps(meta, indent=2) + "\n", encoding="utf-8")
    return scaled.width, scaled.height


def main(argv: list[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(
        prog="make-bootlogo",
        description="Gera splash.bmp (U-Boot) e bootlogo.rgba (Launcher).",
    )
    parser.add_argument("--source", default=str(root / "docs" / "logo.jpg"),
                        help="logo de origem (default: docs/logo.jpg)")
    parser.add_argument("--out", default=str(root / "assets" / "bootlogo"),
                        help="diretorio de saida (default: assets/bootlogo)")
    parser.add_argument("--devices", default=str(
        root / "buildroot" / "board" / "giochi" / "devices"),
        help="diretorio dos <aparelho>.conf")
    args = parser.parse_args(argv)

    source = Path(args.source)
    if not source.is_file():
        print(f"bootlogo: logo ausente: {source}", file=sys.stderr)
        return EXIT_USAGE

    if Image is None:
        print("bootlogo: Pillow ausente; splash sera omitido "
              "(instale python3-pil para gera-lo)", file=sys.stderr)
        return EXIT_NO_PIL

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    logo = Image.open(source).convert("RGB")
    rgba_w, rgba_h = write_rgba(logo, out_dir)
    print(f"bootlogo: bootlogo.rgba {rgba_w}x{rgba_h}")

    panels = device_panels(Path(args.devices))
    if not panels:
        print("bootlogo: nenhum aparelho encontrado; apenas RGBA gerado",
              file=sys.stderr)
        return EXIT_OK

    for device, (width, height) in panels.items():
        splash = compose_splash(logo, width, height)
        target = out_dir / device / "splash.bmp"
        target.parent.mkdir(parents=True, exist_ok=True)
        # 24-bit BMP nao comprimido: formato aceito pelo U-Boot splash.
        splash.save(target, format="BMP")
        print(f"bootlogo: {device}/splash.bmp {width}x{height} "
              f"({target.stat().st_size // 1024} KB)")

    return EXIT_OK


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
