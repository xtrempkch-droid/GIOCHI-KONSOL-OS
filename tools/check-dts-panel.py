#!/usr/bin/env python3
"""Avisa quando o DTS escolhido NÃO tem nó de painel (imagem sem vídeo).

POR QUE ISSO EXISTE
-------------------
Medido: `GIOCHI_DTS_FALLBACK=rk3566-anbernic-rg353v` para X35S/X35H produz uma
imagem que **não pode** acender a tela, por dois motivos independentes:

  * o DTS do rg353v no mainline não tem nó `panel@`/`dsi@` — não há driver de
    painel nenhum para o kernel ligar;
  * o painel real do X35S/X35H usa `compatible = "rocknix,generic-dsi"`, um
    driver FORA da árvore (ROCKNIX `packages/linux-drivers/generic-dsi`).

O build passava sem uma palavra sobre isso. A pessoa gravava o cartão, via a
tela preta e ia procurar defeito no bootloader — quando o problema estava na
escolha do DTS, feita no build.

O que este script faz: resolve o DTS que a imagem vai usar (seguindo
`#include`, inclusive o caso `DTS_PATH` out-of-tree) e reporta se existe algum
nó de painel reconhecível. Roda no lint e no pré-voo do build.

Uso:
  tools/check-dts-panel.py                     # todos os aparelhos
  tools/check-dts-panel.py x35h                # um aparelho
  tools/check-dts-panel.py --dts caminho.dts   # arquivo avulso
  tools/check-dts-panel.py --strict            # falha em vez de avisar

Saída: uma linha por aparelho, `painel:` ok / AUSENTE.
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEVICES = os.path.join(ROOT, "buildroot", "board", "giochi", "devices")
BOARD_DTS = os.path.join(ROOT, "buildroot", "board", "giochi", "dts")

# Onde procurar o DTS upstream: as árvores de kernel já extraídas em dist/.
# Caminho MONTADO, não os.walk: dist/ tem milhões de arquivos de build e uma
# varredura recursiva levava minutos só para responder isto.
KERNEL_DTS_DIRS = [
    os.path.join(ROOT, "dist", dev, "build", "build", "linux-6.12",
                 "arch", "arm64", "boot", "dts", "rockchip")
    for dev in sorted(os.listdir(os.path.join(ROOT, "dist")))
] if os.path.isdir(os.path.join(ROOT, "dist")) else []

# Nó de painel: `panel: panel@0 {`, `panel {`, `panel@0 {` ou um dsi com
# `panel@`. Também aceita o rótulo `&panel` sendo sobrescrito no delta.
RE_PAINEL = re.compile(r"^\s*(panel(:|\s*@|\s*\{)|.*@[0-9a-f]+\s*\{\s*$)",
                       re.M)
RE_PAINEL_LABEL = re.compile(r"^\s*panel:\s*\w", re.M)
RE_PAINEL_OVERRIDE = re.compile(r"^\s*&panel\s*\{", re.M)
RE_INCLUDE = re.compile(r'^\s*#include\s+"([^"]+)"', re.M)

# O driver que o painel do X35S/X35H exige e que NÃO está no mainline.
FORA_DA_ARVORE = {"rocknix,generic-dsi": "ROCKNIX generic-dsi (fora da árvore)"}


def ler_device(device_id):
    for nome in sorted(os.listdir(DEVICES)):
        if not nome.endswith(".conf"):
            continue
        caminho = os.path.join(DEVICES, nome)
        conf = {}
        with open(caminho, encoding="utf-8") as fh:
            for linha in fh:
                m = re.match(r"^([A-Z_0-9]+)=(.*)$", linha.strip())
                if m:
                    conf[m.group(1)] = m.group(2).strip().strip('"')
        if conf.get("GIOCHI_ID") == device_id:
            return conf
    return None


def resolver_caminho(nome, base):
    """Acha `nome` a partir de `base` e dos diretórios de DTS do kernel."""
    candidatos = [os.path.join(base, nome)]
    for raiz in KERNEL_DTS_DIRS:
        candidatos.append(os.path.join(raiz, nome))
    for c in candidatos:
        if os.path.isfile(c):
            return c
    return None


def coletar(caminho, vistos=None, texto=None, faltando=None):
    """Concatena o DTS e todos os includes (o delta do aparelho vem primeiro)."""
    vistos = vistos if vistos is not None else set()
    texto = texto if texto is not None else []
    faltando = faltando if faltando is not None else []
    if caminho in vistos:
        return texto, faltando
    vistos.add(caminho)
    try:
        with open(caminho, encoding="utf-8") as fh:
            conteudo = fh.read()
    except OSError:
        faltando.append(caminho)
        return texto, faltando
    texto.append(conteudo)
    for nome in RE_INCLUDE.findall(conteudo):
        alvo = resolver_caminho(nome, os.path.dirname(caminho))
        if alvo is None:
            faltando.append(nome)
            continue
        coletar(alvo, vistos, texto, faltando)
    return texto, faltando


def analisar(caminho):
    """Devolve (tem_painel, drivers_fora_da_arvore, includes_faltando)."""
    texto, faltando = coletar(caminho)
    junto = "\n".join(texto)
    tem = bool(RE_PAINEL_LABEL.search(junto) or RE_PAINEL_OVERRIDE.search(junto))
    if not tem:
        # Nó sem rótulo: `panel@0 {` / `panel {`
        tem = bool(re.search(r"^\s*panel(\s|@|:)", junto, re.M)) or \
              bool(re.search(r"^\s*panel@[0-9a-f]+\s*\{", junto, re.M))
    fora = [v for k, v in FORA_DA_ARVORE.items() if k in junto]
    return tem, fora, faltando


def dts_do_aparelho(conf):
    """Caminho do DTS que o build usaria para este aparelho (ou None)."""
    if conf.get("GIOCHI_DTS_UPSTREAM") == "yes":
        nome = (conf.get("GIOCHI_DTS") or "") + ".dts"
        return resolver_caminho(nome, BOARD_DTS)
    # Out-of-tree: buildroot/board/giochi/dts/<nome>.dts
    nome = (conf.get("GIOCHI_DTS") or "") + ".dts"
    local = os.path.join(BOARD_DTS, nome)
    if os.path.isfile(local):
        return local
    return None


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("aparelho", nargs="?")
    ap.add_argument("--dts")
    ap.add_argument("--strict", action="store_true",
                    help="sai com 1 quando o painel estiver ausente")
    ap.add_argument("--buscar", metavar="NOME",
                    help="resolve NOME.dts nas arvores de kernel conhecidas e "
                         "verifica (usado pelo build-image.sh no --fallback-dts)")
    args = ap.parse_args()

    alvos = []
    if args.buscar:
        achado = resolver_caminho(args.buscar + ".dts", BOARD_DTS)
        if achado is None:
            # Arvore do kernel ainda nao extraida (primeiro build): nao e erro.
            print("%-6s painel: ? (DTS %s ainda nao extraido; confira depois)"
                  % (args.buscar, args.buscar))
            return 0
        alvos.append((achado, args.buscar))
    elif args.dts:
        alvos.append((args.dts, os.path.basename(args.dts)))
    else:
        ids = [args.aparelho] if args.aparelho else [
            nome[:-5] for nome in sorted(os.listdir(DEVICES))
            if nome.endswith(".conf")]
        for dev in ids:
            conf = ler_device(dev)
            if conf is None:
                print("check-dts-panel: aparelho desconhecido: %s" % dev,
                      file=sys.stderr)
                return 2
            alvos.append((dts_do_aparelho(conf), dev))

    problemas = 0
    for caminho, rotulo in alvos:
        if caminho is None:
            # Aparelho cujo DTS o build recebe por GIOCHI_DTS_PATH/DTB_PATH:
            # não é possível julgar sem o arquivo. Avisa, não falha.
            print("%-6s painel: ? (DTS externo, informe com --dts)" % rotulo)
            continue
        tem, fora, faltando = analisar(caminho)
        if faltando:
            print("%-6s painel: ? (include não resolvido: %s)"
                  % (rotulo, ", ".join(faltando[:3])))
            continue
        if tem and not fora:
            print("%-6s painel: ok (%s)" % (rotulo, os.path.basename(caminho)))
            continue
        if tem and fora:
            print("%-6s painel: DRIVER FORA DA ÁRVORE -- %s"
                  % (rotulo, "; ".join(fora)))
            print("        o kernel do Buildroot não compila esse driver: a "
                  "tela NÃO acende")
            problemas += 1
            continue
        print("%-6s painel: AUSENTE em %s" % (rotulo,
                                              os.path.basename(caminho)))
        print("        o DTS não tem nó de painel: nenhum driver de vídeo será "
              "ligado e a tela fica preta")
        problemas += 1

    if problemas and args.strict:
        print("\ncheck-dts-panel: %d aparelho(s) sem painel utilizável"
              % problemas, file=sys.stderr)
        return 1
    if problemas:
        print("\ncheck-dts-panel: %d aparelho(s) sem painel utilizável "
              "(aviso; use --strict para falhar)" % problemas)
    return 0


if __name__ == "__main__":
    sys.exit(main())
