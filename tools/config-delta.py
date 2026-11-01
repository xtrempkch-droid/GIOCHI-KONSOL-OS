#!/usr/bin/env python3
"""Diz quais pacotes do Buildroot precisam ser reconstruídos quando o .config muda.

POR QUE ISSO EXISTE
-------------------
O Buildroot **não reconstrói um pacote quando a configuração dele muda**. Os
stamps (`.stamp_configured`, `.stamp_built`…) continuam válidos e o build
termina verde com o binário antigo dentro da imagem.

Medido neste projeto (AGENTS.md §3.56): depois de ligar
`BR2_PACKAGE_SDL2_OPENGLES=y` (que destrava `SDL2_KMSDRM`), o build rodou,
gerou imagem, foi gravado e verificado byte a byte — e o SDL2 da imagem
continuava compilado **sem nenhum driver de vídeo**, com stamps de 6 horas
antes. O sintoma no aparelho é "render: SDL2 indisponivel" com `/dev/dri/card0`
funcionando, que manda a investigação para o lado errado.

USO
    tools/config-delta.py .config.antigo .config.novo
    tools/config-delta.py --packages .config.antigo .config.novo   # só nomes

A saída é a lista de pacotes (nome do diretório em package/) cuja configuração
mudou. O build-image.sh usa `--packages` e apaga os stamps deles.
"""

import argparse
import os
import re
import sys

# Símbolos globais que mudam o comportamento de TODOS os pacotes. Mudá-los
# invalida o build inteiro; não tentamos ser espertos, apenas avisamos.
GLOBAIS = (
    "BR2_TOOLCHAIN_", "BR2_USE_", "BR2_TOOLCHAIN_BUILDROOT_",
    "BR2_TARGET_OPTIMIZATION", "BR2_ENABLE_",
)

# Pacote -> símbolos que não começam por BR2_PACKAGE_ mas o afetam.
EXTRAS = {
    "busybox": ("BR2_INIT_BUSYBOX", "BR2_SYSTEM_BIN_SH_BUSYBOX"),
    "linux": ("BR2_LINUX_KERNEL", "BR2_TARGET_UBOOT"),
    "uboot": ("BR2_TARGET_UBOOT",),
}


def ler(caminho):
    valores = {}
    with open(caminho, encoding="utf-8", errors="replace") as fp:
        for linha in fp:
            linha = linha.strip()
            if not linha:
                continue
            if linha.startswith("# CONFIG_") and linha.endswith(" is not set"):
                valores[linha[len("# "):-len(" is not set")]] = "n"
                continue
            if linha.startswith("#") or "=" not in linha:
                continue
            simbolo, valor = linha.split("=", 1)
            valores[simbolo] = valor
    return valores


def pacote_do_simbolo(simbolo):
    """BR2_PACKAGE_SDL2_KMSDRM -> 'sdl2'; BR2_PACKAGE_MESA3D_OPENGL_ES -> 'mesa3d'."""
    if not simbolo.startswith("BR2_PACKAGE_"):
        return None
    resto = simbolo[len("BR2_PACKAGE_"):]
    if resto.startswith("HOST_"):
        return None
    return resto.split("_")[0].lower()


def mapear(antigo, novo, raiz=None):
    """Devolve (pacotes, globais) afetados pela diferença."""
    pacotes = set()
    globais = set()

    for simbolo in set(antigo) | set(novo):
        antes = antigo.get(simbolo)
        depois = novo.get(simbolo)
        if antes == depois:
            continue
        if any(simbolo.startswith(p) for p in GLOBAIS):
            globais.add(simbolo)
            continue
        nome = pacote_do_simbolo(simbolo)
        if nome:
            pacotes.add(nome)
            continue
        for pkg, prefixos in EXTRAS.items():
            if any(simbolo.startswith(p) for p in prefixos):
                pacotes.add(pkg)

    # Um pacote só interessa se existe em package/ (o nome derivado do símbolo
    # pode ser um submenu: BR2_PACKAGE_MESA3D_* -> mesa3d, ok; mas
    # BR2_PACKAGE_PROVIDES_LIBEGL não é pacote).
    if raiz:
        pkgdir = os.path.join(raiz, "package")
        if os.path.isdir(pkgdir):
            existentes = set(os.listdir(pkgdir))
            pacotes = {p for p in pacotes if p in existentes}

    return sorted(pacotes), sorted(globais)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("antigo")
    ap.add_argument("novo")
    ap.add_argument("--packages", action="store_true",
                    help="imprime só os nomes, um por linha")
    ap.add_argument("--buildroot", default=os.environ.get("BUILDROOT_DIR", ""),
                    help="raiz do Buildroot (para filtrar pacotes existentes)")
    args = ap.parse_args()

    for caminho in (args.antigo, args.novo):
        if not os.path.isfile(caminho):
            print("config-delta: %s nao existe" % caminho, file=sys.stderr)
            return 2

    pacotes, globais = mapear(ler(args.antigo), ler(args.novo),
                              args.buildroot or None)

    if args.packages:
        for p in pacotes:
            print(p)
        return 0

    if globais:
        print("config-delta: %d simbolo(s) GLOBAL(is) mudaram" % len(globais),
              file=sys.stderr)
        for s in globais[:10]:
            print("  " + s, file=sys.stderr)
        print("  -> rebuild completo seria o correto", file=sys.stderr)
    if pacotes:
        print("config-delta: %d pacote(s) afetado(s): %s"
              % (len(pacotes), " ".join(pacotes)), file=sys.stderr)
    if not pacotes and not globais:
        print("config-delta: nenhuma mudanca relevante")
    return 0


if __name__ == "__main__":
    sys.exit(main())
