#!/usr/bin/env python3
"""Confere se os símbolos do defconfig realmente entraram no .config.

POR QUE ISSO EXISTE
-------------------
O Kconfig do Buildroot **descarta em silêncio** um símbolo cuja dependência não
está satisfeita. Não há aviso, não há erro de build, e o pacote simplesmente não
entra (ou entra sem a feature). Neste projeto isso já custou:

  * `BR2_PACKAGE_LIBMPG123` (não existe) -> imagem sem MP3;
  * `BR2_PACKAGE_RFKILL` (virou legacy) -> build morto em Makefile.legacy;
  * `BR2_PACKAGE_SDL2_KMSDRM=y` sem `SDL2_OPENGLES=y` -> SDL2 compilado **sem
    nenhum driver de vídeo**. O aparelho tinha `/dev/dri/card0` funcionando, o
    DSI bindado e a tela acesa, e o Launcher ainda dizia
    "render: SDL2 indisponivel" (AGENTS.md §3.56). Depois de 3 horas de build e
    um ciclo de flash, o diagnóstico foi `grep SDL2 build/.config`.

Uso (o `.config` existe depois do `make defconfig`):

    tools/check-config-applied.py --defconfig buildroot/configs/giochi_x55_defconfig \\
        --dotconfig dist/x55/build/.config

Sai com 1 se algum `SYM=y` do defconfig não estiver no `.config` com o mesmo
valor. Símbolos `is not set` no defconfig são comparados do mesmo jeito.
"""

import argparse
import os
import sys


def ler_atribuicoes(caminho):
    """Devolve {símbolo: 'y'|'n'|'m'|'valor'} do arquivo de config."""
    valores = {}
    with open(caminho, encoding="utf-8", errors="replace") as fp:
        for linha in fp:
            linha = linha.strip()
            if not linha or linha.startswith("#"):
                # '# CONFIG_X is not set' também é informação de valor.
                if linha.startswith("# CONFIG_") and linha.endswith(" is not set"):
                    simbolo = linha[len("# "):-len(" is not set")]
                    valores[simbolo] = "n"
                continue
            if "=" not in linha:
                continue
            simbolo, valor = linha.split("=", 1)
            valores[simbolo] = valor
    return valores


def esperado(valor):
    """Normaliza o valor para comparar ('y', 'n', 'm' ou a string crua)."""
    if valor in ("y", "n", "m"):
        return valor
    return valor


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--defconfig", required=True,
                    help="defconfig gerado (o que o build pede)")
    ap.add_argument("--dotconfig", required=True,
                    help=".config resultante do Kconfig")
    ap.add_argument("--allow", action="append", default=[],
                    help="símbolo a ignorar (pode repetir)")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.dotconfig):
        print("check-config-applied: %s nao existe (rode 'make defconfig')"
              % args.dotconfig, file=sys.stderr)
        return 2

    pedido = ler_atribuicoes(args.defconfig)
    obtido = ler_atribuicoes(args.dotconfig)
    ignorar = set(args.allow)

    perdidos = []
    for simbolo, valor in sorted(pedido.items()):
        if simbolo in ignorar:
            continue
        # Só faz sentido cobrar símbolos booleanos/tristate: string e int em
        # defconfig são literais e o Kconfig só os reescreve com aspas.
        if valor not in ("y", "n", "m"):
            continue
        aplicado = obtido.get(simbolo)
        # Pedir '=n' e o símbolo não aparecer no .config é o estado normal
        # (o Kconfig omite o que está desligado sem 'default y'). O que importa
        # é que ele NÃO esteja ligado.
        if valor == "n" and (aplicado is None or aplicado == "n"):
            continue
        if aplicado != esperado(valor):
            perdidos.append((simbolo, valor, aplicado))

    if perdidos:
        print("check-config-applied: %d simbolo(s) pedido(s) e NAO aplicado(s)"
              % len(perdidos), file=sys.stderr)
        for simbolo, valor, aplicado in perdidos:
            print("  PEDIDO %s=%s  ->  %s"
                  % (simbolo, valor,
                     "AUSENTE" if aplicado is None else aplicado),
                  file=sys.stderr)
        print("  causa provavel: dependencia nao satisfeita (o Kconfig "
              "descarta sem avisar)", file=sys.stderr)
        print("  veja o 'depends on' do simbolo em package/*/Config.in",
              file=sys.stderr)
        return 1

    if args.verbose:
        print("check-config-applied: %d simbolo(s) conferido(s)"
              % sum(1 for v in pedido.values() if v in ("y", "n", "m")))
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
