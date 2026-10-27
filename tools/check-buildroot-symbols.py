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



def values(conf: str) -> dict[str, str]:
    """Le 'BR2_X=y', 'BR2_X="valor"' e 'BR2_X=valor' por defconfig."""
    out: dict[str, str] = {}
    for line in open(conf, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        out[key] = value.strip().strip('"')
    return out


def check_semantics(conf: str) -> list[str]:
    """Erros de VALOR, que a checagem de simbolo nao pega.

    Caso real: BR2_LINUX_KERNEL_USE_DEFCONFIG=y com
    BR2_LINUX_KERNEL_DEFCONFIG="defconfig". Os dois simbolos existem, entao a
    validacao de simbolo passa — mas o Buildroot faz
    LINUX_KCONFIG_DEFCONFIG = <valor>_defconfig, o comando vira
    'make defconfig_defconfig' e o kernel procura
    arch/arm64/configs/defconfig_defconfig, que nao existe. O build morre
    depois de baixar o kernel e compilar o host.
    """
    errs: list[str] = []
    v = values(conf)

    use_defconfig = v.get("BR2_LINUX_KERNEL_USE_DEFCONFIG") == "y"
    use_arch = v.get("BR2_LINUX_KERNEL_USE_ARCH_DEFAULT_CONFIG") == "y"
    use_custom = v.get("BR2_LINUX_KERNEL_USE_CUSTOM_CONFIG") == "y"
    name = v.get("BR2_LINUX_KERNEL_DEFCONFIG", "")

    if v.get("BR2_LINUX_KERNEL") == "y":
        chosen = [k for k, on in (
            ("USE_DEFCONFIG", use_defconfig),
            ("USE_ARCH_DEFAULT_CONFIG", use_arch),
            ("USE_CUSTOM_CONFIG", use_custom),
        ) if on]
        if len(chosen) != 1:
            errs.append(
                "escolha de configuracao do kernel invalida "
                f"({', '.join(chosen) or 'nenhuma'}); marque exatamente uma de "
                "BR2_LINUX_KERNEL_USE_DEFCONFIG / "
                "BR2_LINUX_KERNEL_USE_ARCH_DEFAULT_CONFIG / "
                "BR2_LINUX_KERNEL_USE_CUSTOM_CONFIG")

        if use_defconfig:
            if name in ("", "defconfig"):
                errs.append(
                    f'BR2_LINUX_KERNEL_DEFCONFIG="{name}" vira '
                    f'"{name}_defconfig" (o Buildroot acrescenta o sufixo). '
                    "Para arm64 use BR2_LINUX_KERNEL_USE_ARCH_DEFAULT_CONFIG=y")
            elif name.endswith("_defconfig"):
                errs.append(
                    f'BR2_LINUX_KERNEL_DEFCONFIG="{name}" ja tem o sufixo; '
                    f'o Buildroot produziria "{name}_defconfig"')
        elif name:
            errs.append(
                'BR2_LINUX_KERNEL_DEFCONFIG so vale com '
                "BR2_LINUX_KERNEL_USE_DEFCONFIG=y (aqui ele seria ignorado)")

    return errs


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--buildroot",
                    default=os.environ.get("BUILDROOT_DIR", ""),
                    help="arvore do Buildroot (default: $BUILDROOT_DIR)")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    # As checagens de valor nao precisam da arvore do Buildroot: rodam sempre,
    # inclusive em `make lint` numa maquina sem ele.
    semantic = 0
    for conf in CONFIGS:
        for err in check_semantics(conf):
            print(f"{Path(conf).name}: {err}", file=sys.stderr)
            semantic += 1

    if not args.buildroot:
        if semantic:
            print(f"check-buildroot-symbols: {semantic} erro(s) de valor",
                  file=sys.stderr)
            return 1
        print("check-buildroot-symbols: BUILDROOT_DIR nao definido; "
              "valores ok, simbolos nao conferidos")
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

    problems += semantic
    if problems:
        print(f"check-buildroot-symbols: {problems} problema(s)",
              file=sys.stderr)
        return 1

    print(f"ok ({len(CONFIGS)} defconfig(s) contra Buildroot em {buildroot})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
