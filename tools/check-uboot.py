#!/usr/bin/env python3
"""Confere os defconfigs de U-Boot e os blobs do rkbin declarados por aparelho.

Motivo: `devices/*.conf` declara o defconfig de U-Boot de cada placa. Se o
nome estiver errado (ou se o defconfig nao existir naquela versao do U-Boot),
o build do Buildroot falha **horas depois**, ja no meio da compilacao. E se
BL31/TPL do rkbin nao existirem no commit pinado, o SPL do RK3566 nao liga a
DDR e a placa simplesmente nao inicia — sem mensagem clara.

O que ele valida:
  - `GIOCHI_UBOOT_UPSTREAM=yes` tem `GIOCHI_UBOOT_DEFCONFIG`;
  - `configs/<defconfig>_defconfig` existe na arvore de U-Boot;
  - todo `CONFIG_*` desse defconfig e do `uboot.fragment` existe nos Kconfig;
  - `GIOCHI_UBOOT_TPL` e `GIOCHI_UBOOT_BL31` existem no tree do rkbin.

Sem `--uboot-dir`/`--rkbin-dir` (ou `UBOOT_DIR`/`RKBIN_DIR`) ele apenas avisa e
sai com 0, para nao quebrar `make lint` em maquina sem as arvores.

Uso:
  tools/check-uboot.py --uboot-dir ~/u-boot --rkbin-dir ~/rkbin
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEVICES = sorted(glob.glob(str(ROOT / "buildroot" / "board" / "giochi" / "devices" / "*.conf")))
FRAGMENT = ROOT / "buildroot" / "board" / "giochi" / "uboot.fragment"

KCONFIG_DECL = re.compile(r"^\s*(?:menu)?config\s+([A-Za-z0-9_]+)", re.M)
CONF_LINE = re.compile(r"^(CONFIG_[A-Za-z0-9_]+)=")


def read_conf(path: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        out[key] = value.strip().strip('"')
    return out


def kconfig_symbols(uboot: Path) -> set[str]:
    symbols: set[str] = set()
    for path in uboot.rglob("Kconfig*"):
        if not path.is_file():
            continue
        try:
            symbols.update(KCONFIG_DECL.findall(path.read_text(errors="replace")))
        except OSError:
            continue
    return symbols


def conf_symbols(path: Path) -> set[str]:
    out: set[str] = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        m = CONF_LINE.match(line.strip())
        if m:
            out.add(m.group(1))
    return out


def strip_config(symbols: set[str]) -> set[str]:
    return {s[len("CONFIG_"):] for s in symbols}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--uboot-dir", default=os.environ.get("UBOOT_DIR", ""))
    ap.add_argument("--rkbin-dir", default=os.environ.get("RKBIN_DIR", ""))
    args = ap.parse_args()

    if not args.uboot_dir and not args.rkbin_dir:
        print("check-uboot: sem --uboot-dir/--rkbin-dir; pulando", file=sys.stderr)
        return 0

    problems = 0
    defined: set[str] = set()
    if args.uboot_dir:
        uboot = Path(args.uboot_dir)
        if not (uboot / "configs").is_dir():
            print(f"check-uboot: {uboot}/configs ausente", file=sys.stderr)
            return 1
        defined = kconfig_symbols(uboot)
        if not defined:
            print("check-uboot: nenhum simbolo lido dos Kconfig", file=sys.stderr)
            return 1

    fragment = strip_config(conf_symbols(FRAGMENT)) if FRAGMENT.is_file() else set()
    fragment_missing = sorted(fragment - defined) if defined else []

    for conf_path in DEVICES:
        conf = read_conf(Path(conf_path))
        name = Path(conf_path).stem
        if conf.get("GIOCHI_UBOOT_UPSTREAM") != "yes":
            continue
        defconfig = conf.get("GIOCHI_UBOOT_DEFCONFIG", "")
        if not defconfig:
            print(f"{name}: UBOOT_UPSTREAM=yes sem GIOCHI_UBOOT_DEFCONFIG", file=sys.stderr)
            problems += 1
            continue

        if args.uboot_dir:
            path = Path(args.uboot_dir) / "configs" / f"{defconfig}_defconfig"
            if not path.is_file():
                print(f"{name}: U-Boot nao tem configs/{defconfig}_defconfig", file=sys.stderr)
                problems += 1
            else:
                used = strip_config(conf_symbols(path)) | fragment
                missing = sorted(used - defined)
                for sym in missing:
                    print(f"{name}: U-Boot sem CONFIG_{sym}", file=sys.stderr)
                problems += len(missing)

        for key, label in (("GIOCHI_UBOOT_TPL", "TPL/DDR"), ("GIOCHI_UBOOT_BL31", "BL31")):
            value = conf.get(key, "")
            if not value:
                continue
            if args.rkbin_dir:
                if not (Path(args.rkbin_dir) / value).is_file():
                    print(f"{name}: rkbin sem {label} '{value}'", file=sys.stderr)
                    problems += 1

    if fragment_missing:
        for sym in fragment_missing:
            print(f"uboot.fragment: U-Boot sem CONFIG_{sym}", file=sys.stderr)
        problems += len(fragment_missing)

    if problems:
        print(f"check-uboot: {problems} problema(s)", file=sys.stderr)
        return 1

    checked = sum(1 for p in DEVICES
                  if read_conf(Path(p)).get("GIOCHI_UBOOT_UPSTREAM") == "yes")
    print(f"ok ({checked} aparelho(s) com U-Boot mainline)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
