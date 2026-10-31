#!/usr/bin/env python3
"""Confere se o cartão foi gravado exatamente como a imagem, byte a byte.

POR QUE ISSO EXISTE
-------------------
Um cartão pode "gravar sem erro" e ainda assim entregar dados errados ao
aparelho. Medido neste projeto: o kernel montou o SquashFS, mas falhou ao ler
blocos de dados com EIO, e o boot parou em "No working init found" com a tela
preta. A imagem gerada estava íntegra (unsquashfs extraiu tudo) e tinha dados
válidos nos setores reclamados — ou seja, o problema estava entre o PC e o
cartão, e nenhum dos dois avisou.

Esta ferramenta separa as duas causas possíveis, que exigem ações diferentes:

  * DADOS DIFERENTES no cartão  -> a gravação não terminou (falta conv=fsync,
    cartão removido cedo, cabo/leitor instável). Regravar resolve.
  * ERRO DE LEITURA do cartão   -> setor defeituoso na mídia. Regravar pode
    passar por cima (a controladora realoca), mas se repetir troque o cartão.

USO
    sudo tools/verify-card.py dist/x55/giochi-x55-sdcard.img /dev/sdX
    sudo tools/verify-card.py -j 8 imagem.img.xz /dev/sdX

É SOMENTE LEITURA no dispositivo. Como o de-referência de /dev/sdX pode ser o
seu disco do sistema, o script recusa dispositivo que tenha partição montada.
"""

import argparse
import hashlib
import lzma
import os
import sys

BLOCO = 4 * 1024 * 1024          # 4 MiB: bom tamanho para leitura sequencial
SETOR = 512


def abrir_imagem(caminho):
    """Abre .img (cru) ou .img.xz, sempre como stream."""
    if caminho.endswith(".xz"):
        return lzma.open(caminho, "rb"), True
    return open(caminho, "rb", buffering=0), False


def montado(dev):
    """True se alguma partição do dispositivo está montada."""
    base = os.path.basename(dev)
    try:
        with open("/proc/mounts", encoding="utf-8") as fh:
            for linha in fh:
                origem = linha.split()[0]
                if os.path.basename(origem).startswith(base):
                    return origem
    except OSError:
        pass
    return None


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("imagem")
    ap.add_argument("dispositivo")
    ap.add_argument("--limite", type=int, default=20,
                    help="maximo de divergencias listadas (default 20)")
    args = ap.parse_args()

    if not os.path.exists(args.imagem):
        print("verify-card: imagem nao encontrada: %s" % args.imagem,
              file=sys.stderr)
        return 2
    if not os.path.exists(args.dispositivo):
        print("verify-card: dispositivo nao encontrado: %s" % args.dispositivo,
              file=sys.stderr)
        return 2

    m = montado(args.dispositivo)
    if m:
        print("verify-card: %s tem '%s' montado; desmonte antes "
              "(o script so le, mas ler com ele montado bagunca o cache)"
              % (args.dispositivo, m), file=sys.stderr)
        return 2

    # Tamanho de referencia: o da imagem descomprimida.
    if args.imagem.endswith(".xz"):
        with lzma.open(args.imagem, "rb") as fh:
            fh.seek(0, os.SEEK_END)
            total = fh.tell()
    else:
        total = os.path.getsize(args.imagem)

    print("verify-card: imagem %s (%d MiB) x %s"
          % (args.imagem, total // 1024 // 1024, args.dispositivo))
    print("verify-card: isso le o cartao inteiro; Ctrl-C interrompe sem risco")

    img, _ = abrir_imagem(args.imagem)
    try:
        dev = open(args.dispositivo, "rb", buffering=0)
    except PermissionError:
        print("verify-card: sem permissao de leitura em %s; use sudo"
              % args.dispositivo, file=sys.stderr)
        return 2

    divergentes = []
    erros_io = []
    lidos = 0
    h_img = hashlib.sha256()
    h_dev = hashlib.sha256()

    try:
        while lidos < total:
            quer = min(BLOCO, total - lidos)
            a = img.read(quer)
            if not a:
                break
            try:
                b = dev.read(quer)
            except OSError as exc:
                erros_io.append((lidos, str(exc)))
                lidos += quer
                continue
            h_img.update(a)
            h_dev.update(b)
            if a != b:
                # Primeiro byte que difere dentro deste bloco.
                for i in range(min(len(a), len(b))):
                    if a[i] != b[i]:
                        divergentes.append((lidos + i, a[i], b[i]))
                        break
                else:
                    divergentes.append((lidos, None, None))
            lidos += len(a)
    finally:
        img.close()
        dev.close()

    print()
    print("lido: %d MiB de %d MiB" % (lidos // 1024 // 1024,
                                      total // 1024 // 1024))
    print("sha256 da imagem : %s" % h_img.hexdigest())
    print("sha256 do cartao : %s" % h_dev.hexdigest())

    if not divergentes and not erros_io:
        print("\nOK: o cartao e' identico a imagem, byte a byte.")
        return 0

    if divergentes:
        print("\n%d BLOCO(S) COM DADOS DIFERENTES -- a gravacao NAO terminou."
              % len(divergentes))
        print("   (falta 'conv=fsync' no dd, cartao removido cedo, ou leitor")
        print("    instavel). Regrave e verifique de novo.")
        for off, esperado, achado in divergentes[:args.limite]:
            if esperado is None:
                print("   offset %d (setor %d): tamanhos diferentes"
                      % (off, off // SETOR))
            else:
                print("   offset %d (setor %d): esperado 0x%02x, achado 0x%02x"
                      % (off, off // SETOR, esperado, achado))
        if len(divergentes) > args.limite:
            print("   ... e mais %d" % (len(divergentes) - args.limite))

    if erros_io:
        print("\n%d ERRO(S) DE LEITURA -- setor defeituoso na midia."
              % len(erros_io))
        print("   (o cartao nao consegue devolver esses bytes nem para o PC)")
        for off, msg in erros_io[:args.limite]:
            print("   offset %d (setor %d): %s" % (off, off // SETOR, msg))
        print("   Regravar costuma resolver (a controladora realoca o setor).")
        print("   Se repetir no MESMO setor, troque o cartao.")

    return 1


if __name__ == "__main__":
    sys.exit(main())
