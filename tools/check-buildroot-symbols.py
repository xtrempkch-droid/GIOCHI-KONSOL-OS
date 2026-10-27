#!/usr/bin/env python3
"""Valida os defconfigs contra a arvore do Buildroot que sera usada no build.

Motivo: o Kconfig *descarta em silencio* simbolos que nao existem. Um
`BR2_PACKAGE_LIBMPG123=y` (o pacote chama-se mpg123) nao gera erro nenhum —
simplesmente nao entra no .config, o pacote nao e compilado e o aparelho fica
sem o recurso. Pior: simbolos que viraram *legacy* ligam BR2_LEGACY e o build
morre em Makefile.legacy com uma mensagem que nao diz QUAL simbolo e o culpado.

Este script compara cada `BR2_*` dos nossos defconfigs com as declaracoes
`config`/`menuconfig` do Buildroot + do nosso BR2_EXTERNAL e falha listando:

  LEGACY       -> existe apenas em Config.in.legacy (aborta o build)
  INEXISTENTE  -> nao existe (sera descartado em silencio)

Uso:
  tools/check-buildroot-symbols.py --buildroot /caminho/buildroot
  BUILDROOT_DIR=/caminho tools/check-buildroot-symbols.py
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXTERNAL = ROOT / "buildroot"
CONFIGS = sorted(glob.glob(str(EXTERNAL / "configs" / "*_defconfig")))

DECL = re.compile(r"^\s*(?:menu)?config\s+(BR2_[A-Z0-9_]+)", re.M)
USED = re.compile(r"^(BR2_[A-Z0-9_]+)=")


def collect(buildroot: Path) -> tuple[set[str], set[str]]:
    defined: set[str] = set()
    legacy: set[str] = set()
    for root in (buildroot, EXTERNAL):
        if not root.is_dir():
            continue
        for path in root.rglob("Config.in*"):
            try:
                text = path.read_text(errors="replace")
            except OSError:
                continue
            target = legacy if path.name == "Config.in.legacy" else defined
            target.update(DECL.findall(text))
    return defined, legacy


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--buildroot",
                    default=os.environ.get("BUILDROOT_DIR", ""),
                    help="arvore do Buildroot (default: $BUILDROOT_DIR)")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not args.buildroot:
        print("check-buildroot-symbols: BUILDROOT_DIR nao definido; pulando",
              file=sys.stderr)
        return 0
    buildroot = Path(args.buildroot)
    if not (buildroot / "Makefile").is_file():
        print(f"check-buildroot-symbols: {buildroot} nao parece um Buildroot",
              file=sys.stderr)
        return 1
    if not CONFIGS:
        print("check-buildroot-symbols: nenhum defconfig em buildroot/configs",
              file=sys.stderr)
        return 1

    defined, legacy = collect(buildroot)
    if not defined:
        print("check-buildroot-symbols: nenhum simbolo lido da arvore",
              file=sys.stderr)
        return 1

    problems = 0
    for conf in CONFIGS:
        name = Path(conf).name
        used = {m.group(1) for m in
                (USED.match(line.strip()) for line in open(conf, encoding="utf-8"))
                if m}
        unknown = sorted(used - defined - legacy)
        deprecated = sorted(used & legacy)

        for sym in deprecated:
            print(f"{name}: LEGACY {sym} (liga BR2_LEGACY; use o substituto)",
                  file=sys.stderr)
        for sym in unknown:
            print(f"{name}: INEXISTENTE {sym} (seria descartado em silencio)",
                  file=sys.stderr)
        problems += len(unknown) + len(deprecated)
        if args.verbose:
            print(f"  {name}: {len(used)} simbolos conferidos")

    if problems:
        print(f"check-buildroot-symbols: {problems} simbolo(s) invalido(s)",
              file=sys.stderr)
        return 1

    print(f"ok ({len(CONFIGS)} defconfig(s) contra Buildroot em {buildroot})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
