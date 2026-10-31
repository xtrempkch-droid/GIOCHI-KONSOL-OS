#!/usr/bin/env python3
"""Captura o console serial do console (UART2 / ttyS2) num arquivo.

POR QUE ESTA FERRAMENTA EXISTE
------------------------------
Antes do kernel existir (BootROM, TPL/SPL, U-Boot) NAO ha para onde gravar um
log: nenhum sistema de arquivos esta montado e a RAM ainda nao e gerenciada.
A unica saida e o pino de UART. Este script le esse pino pelo PC e grava tudo,
com carimbo de tempo, para que "nao aparece nada" vire uma linha do tempo.

USO
    tools/serial-capture.py                     # acha o adaptador, 1500000 8N1
    tools/serial-capture.py -b 115200           # outra velocidade
    tools/serial-capture.py -d /dev/ttyUSB0 -o boot.log
    tools/serial-capture.py --list              # mostra os candidatos

POR QUE NAO USA pyserial
    O repositorio nao tem dependencia Python externa, e a velocidade 1500000
    nao e uma constante padrao do termios: quem a aceita e o `stty` do sistema
    (termios2/TCSETS2). Entao a configuracao fica com o `stty` e a leitura aqui.

LIMITE FISICO QUE IMPORTA
    1500000 baud e o limite pratico de varios adaptadores CH340/PL2303
    (clones). Se o texto sair truncado/corrompido, o adaptador pode ser o
    culpado, nao a placa: teste com 115200 (ver docs/BOOT-DEBUG.md §3).
"""

import argparse
import glob
import os
import select
import signal
import subprocess
import sys
import time

PORTS = ["/dev/ttyUSB*", "/dev/ttyACM*"]
BAUD = 1500000
LOG = "serial.log"

# Adaptadores conhecidos. Só para a mensagem: nao decide nada.
CONHECIDOS = {
    "1a86": "CH340/CH341 (limite pratico perto de 1,5 Mbps)",
    "10c4": "CP210x (aceita 2 Mbps)",
    "0403": "FT232 (aceita 3 Mbps)",
    "067b": "PL2303 (limitado; 1,5 Mbps costuma falhar)",
}


def candidatos():
    saida = []
    for padrao in PORTS:
        saida.extend(sorted(glob.glob(padrao)))
    return saida


def descreve(porta):
    """Devolve o idVendor do adaptador dono desta porta, se der para saber."""
    try:
        alvo = os.path.realpath(porta)
        nome = os.path.basename(alvo)
        for base in glob.glob("/sys/class/tty/%s/device/../.." % nome):
            for _ in range(4):
                caminho = os.path.join(base, "idVendor")
                if os.path.exists(caminho):
                    with open(caminho, encoding="utf-8") as fh:
                        return fh.read().strip()
                base = os.path.dirname(base)
    except OSError:
        pass
    return None


def imprimir_lista():
    achados = candidatos()
    if not achados:
        print("nenhum adaptador USB-serial encontrado (/dev/ttyUSB*, /dev/ttyACM*)")
        print("carregue o driver (ch341, cp210x, ftdi_sio) e conecte o adaptador")
        return 1
    for porta in achados:
        vid = descreve(porta)
        extra = ""
        if vid and vid in CONHECIDOS:
            extra = " -> " + CONHECIDOS[vid]
        print("%-16s %s" % (porta, extra or "(adaptador nao identificado)"))
    return 0


def configurar(porta, baud):
    """stty aceita velocidades que o termios do Python nao tem constante."""
    cmd = [
        "stty", "-F", porta, str(baud), "raw", "-echo",
        "-echoe", "-echok", "-echoctl", "-echoke",
        "-ixon", "-ixoff", "-crtscts", "-opost", "min", "1", "time", "0",
    ]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        # Mensagem tipica: "invalid argument" para velocidade nao suportada.
        return False, (r.stderr or r.stdout).strip()
    return True, ""


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("-d", "--device", help="porta serial (default: autodetecta)")
    ap.add_argument("-b", "--baud", type=int, default=BAUD)
    ap.add_argument("-o", "--out", default=LOG)
    ap.add_argument("--list", action="store_true", help="lista e sai")
    ap.add_argument("--quiet", action="store_true", help="nao ecoa no terminal")
    args = ap.parse_args()

    if args.list:
        return imprimir_lista()

    porta = args.device
    if not porta:
        achados = candidatos()
        if not achados:
            print("serial-capture: nenhum adaptador encontrado; use --list",
                  file=sys.stderr)
            return 2
        if len(achados) > 1:
            print("serial-capture: mais de um adaptador; escolha com -d:",
                  file=sys.stderr)
            imprimir_lista()
            return 2
        porta = achados[0]

    if not os.path.exists(porta):
        print("serial-capture: %s nao existe" % porta, file=sys.stderr)
        return 2

    ok, erro = configurar(porta, args.baud)
    if not ok:
        print("serial-capture: stty recusou %d 8N1 em %s" % (args.baud, porta),
              file=sys.stderr)
        if erro:
            print("  stty: %s" % erro, file=sys.stderr)
        print("  o adaptador pode nao suportar essa velocidade; tente -b 115200",
              file=sys.stderr)
        return 3

    vid = descreve(porta)
    dica = CONHECIDOS.get(vid or "", "")
    print("serial-capture: %s @ %d 8N1 -> %s" % (porta, args.baud, args.out))
    if dica and args.baud > 1000000:
        print("serial-capture: atencao: %s" % dica)
    print("serial-capture: ligue/religue o aparelho. Ctrl-C para parar.")

    try:
        fd = os.open(porta, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    except OSError as exc:
        print("serial-capture: nao consegui abrir %s: %s" % (porta, exc),
              file=sys.stderr)
        return 2

    # 'a' e nao 'w': dois boots seguidos ficam no mesmo arquivo, em ordem.
    log = open(args.out, "ab", buffering=0)
    inicio = time.time()
    log.write(("\n=== serial-capture %s @ %d (%.3f) ===\n"
               % (porta, args.baud, inicio)).encode())

    parar = {"sim": False}

    def _sinal(_sig, _frm):
        parar["sim"] = True

    signal.signal(signal.SIGINT, _sinal)
    signal.signal(signal.SIGTERM, _sinal)

    pendente = b""
    try:
        while not parar["sim"]:
            try:
                pronto, _, _ = select.select([fd], [], [], 0.5)
            except InterruptedError:
                continue
            if not pronto:
                continue
            try:
                bloco = os.read(fd, 4096)
            except BlockingIOError:
                continue
            except OSError:
                break
            if not bloco:
                continue
            log.write(bloco)

            # Carimbo por LINHA: sem isso nao da para saber se o aparelho parou
            # ou apenas ficou quieto. A saida no terminal e o que o usuario ve.
            pendente += bloco
            while b"\n" in pendente:
                linha, pendente = pendente.split(b"\n", 1)
                texto = linha.decode("utf-8", "replace").rstrip("\r")
                marca = time.time() - inicio
                if not args.quiet:
                    print("[%8.3f] %s" % (marca, texto), flush=True)
    finally:
        if pendente:
            log.write(pendente)
            if not args.quiet:
                print("[%8.3f] %s" % (time.time() - inicio,
                                      pendente.decode("utf-8", "replace")),
                      flush=True)
        log.close()
        os.close(fd)
        print("serial-capture: gravado em %s" % args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
