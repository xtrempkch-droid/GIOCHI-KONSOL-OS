#!/usr/bin/env python3
"""giochi-ctl — fala com o console por SSH (Modo Desenvolvedor, adendo 4.6.1).

POR QUE EXISTE
--------------
O `giochi-init` escuta em `/run/giochi/control.sock` e esse e' o contrato que um
app de jogos usa (ver docs/APP-SDK.md). Quando a tela nao ajuda — sem serial, sem
teclado, Launcher travado —, a unica forma de perguntar "voce esta vivo?" e'
entrar por SSH e falar com esse socket.

Este script tem tres papeis:

  `preparar`  configura o lado do PC (10.55.0.1/24 em usb0) e espera o console
              aparecer em 10.55.0.2:22;
  `ctl`       manda um comando ao control.sock e mostra a resposta — o MESMO
              protocolo de tests/test_control_protocol.py;
  `echo`      roda um comando de shell no console (para ler logs, `dmesg`,
              conferir o tema ativo, ver se o dropbear subiu).

O console nao tem Python e o `nc` do BusyBox NAO fala soquete de dominio
Unix (`networking/nc.c` do 1.36.1 tem zero ocorrencias de `AF_UNIX`, e nao
existe simbolo `CONFIG_NC_UNIX`). Por isso o console traz o proprio cliente:
`/usr/bin/giochi-ctl` (src/tools/giochi-ctl.c), que usa o mesmo
`giochi_sock_*` do supervisor. Este script manda `giochi-ctl COMANDO` pelo
SSH.

Se a imagem for antiga e nao tiver o binario, o erro e' classificado e dito
com essas palavras — em vez de um "falhou" generico.

USO
    tools/giochi-ctl.py preparar                  # configura usb0 e testa SSH
    tools/giochi-ctl.py ctl PING                  # PING -> PONG
    tools/giochi-ctl.py ctl "USB_MODE MASS"
    tools/giochi-ctl.py echo "cat /usr/share/giochi/device-theme.json"
    tools/giochi-ctl.py echo --tail 40 "cat /boot/giochi-boot.log"
    tools/giochi-ctl.py status                    # resumo de uma vez

VARIAVEIS
    GIOCHI_HOST   IP do console   (default 10.55.0.2)
    GIOCHI_USER   usuario         (default root)
    GIOCHI_IFACE  interface do PC (default: a primeira usb0/enp0s*/enx* que
                                    aparecer depois de plugar o cabo)
    GIOCHI_SSH    comando ssh     (default ssh; use "ssh -o ..." para testar)
"""

from __future__ import annotations

import argparse
import ipaddress
import os
import re
import shutil
import socket
import subprocess
import sys
import time

HOST = os.environ.get("GIOCHI_HOST", "10.55.0.2")
# Caminho do socket DENTRO do console. Existe como variavel para o teste
# automatizado poder apontar para um socket de verdade criado num diretorio
# temporario — sem isso a unica forma de testar o cliente seria no aparelho.
SOCK = os.environ.get("GIOCHI_CTL_SOCK", "/run/giochi/control.sock")
USUARIO = os.environ.get("GIOCHI_USER", "root")
PC_IP = "10.55.0.1"
PORTA = 22

# Comandos do control.sock que este script conhece. A lista e' a mesma de
# tests/test_control_protocol.py; se um entrar sem o outro, o teste avisa.
COMANDOS = {
    "PING": "responde PONG",
    "USB_MODE MTP|MASS": "troca o modo do gadget",
    "DEV_MODE ON|OFF": "liga/desliga o SSH via USB",
    "LAUNCH_APP <caminho>": "abre um app externo",
    "POWEROFF|REBOOT|SUSPEND": "energia (derruba a sessao)",
    "WIFI_UP|WIFI_DOWN|BT_UP|BT_DOWN": "radio",
    "SET_THEME <id>": "NAO implementado na v1.2 (responde ERR)",
}


def log(msg: str) -> None:
    print(msg, flush=True)


def erro(msg: str) -> None:
    print(f"giochi-ctl: {msg}", file=sys.stderr, flush=True)


def roda(cmd: list[str], timeout: float = 20.0, entrada: str | None = None,
         check: bool = False) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout,
                          input=entrada, check=check)


def privilegiado(cmd: list[str]) -> subprocess.CompletedProcess:
    """Roda `cmd` como root, sem senha interativa quando possivel.

    Ordem: direto (se ja' formos root) -> sudo -n -> pkexec. `pkexec` abre o
    dialogo do polkit quando ha' sessao grafica; num SSH de servidor, so' o
    sudo -n funciona, e o erro diz o que fazer.
    """
    if os.geteuid() == 0:
        return roda(cmd)
    if shutil.which("sudo") is not None:
        r = roda(["sudo", "-n"] + cmd)
        if r.returncode == 0:
            return r
    if shutil.which("pkexec") is not None:
        r = roda(["pkexec"] + cmd, timeout=120)
        if r.returncode == 0:
            return r
    erro("precisa de root e nenhum dos caminhos funcionou "
         f"({', '.join(cmd)}). Rode com sudo.")
    sys.exit(3)


# --- descoberta do lado do PC -------------------------------------------

def interfaces_usb() -> list[str]:
    """Interfaces de rede que aparecem com o cabo USB do console.

    `usb0` e' o nome classico; o kernel moderno nomeia por endereco
    (`enp0s20f0u1`, RNDIS/ECM) ou por MAC (`enx...`). Olhar so' `usb0` faz o
    script dizer "nao conectado" com o cabo plugado.
    """
    try:
        saida = roda(["ip", "-br", "link"]).stdout
    except (OSError, subprocess.SubprocessError):
        return []
    nomes = []
    for linha in saida.splitlines():
        nome = linha.split()[0].split("@")[0]
        if nome in ("lo", "docker0") or nome.startswith(("wl", "veth", "br-")):
            continue
        if nome.startswith(("usb", "enp0s", "enx")):
            nomes.append(nome)
    return nomes


def iface_preferida() -> str | None:
    escolhida = os.environ.get("GIOCHI_IFACE")
    if escolhida:
        return escolhida
    candidatas = interfaces_usb()
    # Prefere uma que ainda NAO tenha IP: e' a que acabou de aparecer.
    for nome in candidatas:
        saida = roda(["ip", "-br", "addr", "show", nome]).stdout
        if "10.55." not in saida:
            return nome
    return candidatas[0] if candidatas else None


def nm_gerencia(iface: str) -> bool:
    """O NetworkManager esta' com esta interface?

    Importa porque ele desfaz o IP manual: a interface e' ethernet, o console
    nao tem DHCP e o NM fica em "conectando" apagando o endereco. Sem
    `nmcli` (ou sem NM) devolve False e nada muda.
    """
    if not shutil.which("nmcli"):
        return False
    r = roda(["nmcli", "-t", "-f", "DEVICE,STATE", "device", "status"])
    if r.returncode != 0:
        return False
    for linha in (r.stdout or "").splitlines():
        partes = linha.split(":")
        if len(partes) >= 2 and partes[0] == iface:
            return partes[1] not in ("unmanaged", "unavailable")
    return False


def tem_pc_ip(iface: str) -> bool:
    saida = roda(["ip", "-br", "addr", "show", iface]).stdout
    return PC_IP in saida


def cmd_preparar(args) -> int:
    iface = iface_preferida()

    if iface is None:
        erro("nenhuma interface USB encontrada. O cabo de dados esta' ligado "
             "e o console ligado? (a interface nasce como usb0/enp0s*/enx*)")
        return 2

    log(f"interface do PC: {iface}")

    # Num desktop com NetworkManager o `ip addr add` e' DESFEITO: o NM ve
    # uma interface ethernet sem DHCP, entra em "obtendo configuracao de IP"
    # e limpa o endereco manual alguns segundos depois. O console nao tem
    # DHCP por projeto (§4.6.1), entao o NM nunca vai conseguir nada ali.
    # Medido numa X55: ping funcionando numa execucao e "Connection timed
    # out" na seguinte, com o IP desaparecido da interface.
    if nm_gerencia(iface):
        log(f"{iface} esta' sob NetworkManager; marcando como nao gerenciada")
        r = privilegiado(["nmcli", "device", "set", iface, "managed", "no"])
        if r.returncode != 0:
            erro(f"nao consegui marcar {iface} como nao gerenciada: "
                 f"{(r.stderr or '').strip()}")
            erro("  rode: sudo nmcli device set %s managed no" % iface)
        else:
            # O NM demora um instante para soltar o device.
            time.sleep(1.5)

    if not tem_pc_ip(iface):
        log(f"atribuindo {PC_IP}/24 em {iface} (precisa de root)")
        r = privilegiado(["ip", "addr", "add", f"{PC_IP}/24", "dev", iface])
        if r.returncode != 0:
            if "File exists" not in (r.stderr or ""):
                erro(f"nao consegui configurar {iface}: {r.stderr.strip()}")
                return 3
        privilegiado(["ip", "link", "set", iface, "up"])
    else:
        log(f"{iface} ja tem {PC_IP}/24")

    log(f"esperando {HOST}:{PORTA} aceitar conexao ...")
    limite = time.time() + args.wait
    while time.time() < limite:
        try:
            with socket.create_connection((HOST, PORTA), timeout=1.5) as s:
                banner = s.recv(64)
                log(f"SSH respondeu: {banner.strip().decode(errors='replace')}"
                    if banner else "SSH respondeu (sem banner)")
                return 0
        except OSError:
            time.sleep(1.0)

    erro(f"{HOST}:{PORTA} nao respondeu em {args.wait}s.")
    erro("causas, na ordem em que costumam acontecer:")
    erro("  1. Modo Desenvolvedor DESLIGADO (developer.ssh_usb_enabled=false")
    erro("     em /user/settings.json) — e' o padrao de fabrica (§4.6.1);")
    erro("  2. sem chave e sem senha: o S62sshd nao sobe de proposito;")
    erro("  3. o aparelho nao chegou ao userspace (olhe o log do cartao).")
    return 4


# --- comunicação -------------------------------------------------------

def ssh_base() -> list[str]:
    comando = os.environ.get("GIOCHI_SSH", "ssh")
    return comando.split()


def ssh_exec(remoto: str, timeout: float = 30.0) -> subprocess.CompletedProcess:
    cmd = ssh_base() + [
        "-o", "StrictHostKeyChecking=accept-new",
        "-o", "UserKnownHostsFile=" + os.path.expanduser("~/.ssh/known_hosts"),
        "-o", "ConnectTimeout=8",
        "-o", "BatchMode=no",
        f"{USUARIO}@{HOST}", remoto,
    ]
    try:
        return roda(cmd, timeout=timeout)
    except subprocess.TimeoutExpired:
        erro(f"timeout de {timeout:.0f}s esperando o console")
        return subprocess.CompletedProcess(cmd, 124, "", "timeout")


# Cliente do control.sock: o binario `giochi-ctl` DENTRO do console.
#
# Nao da' para usar `nc -U`: o nc do BusyBox nao tem soquete Unix (zero
# `AF_UNIX` em networking/nc.c do 1.36.1) e `CONFIG_NC_UNIX` nao existe —
# pedi-lo no fragmento nao liga nada. O console traz `/usr/bin/giochi-ctl`
# (src/tools/giochi-ctl.c), compilado com o mesmo giochi_sock_* do
# supervisor, e este script so' o invoca pelo SSH.
CLIENTE_CONSOLE = os.environ.get("GIOCHI_CTL_CONSOLE", "/usr/bin/giochi-ctl")

MARCAS_SEM_CLIENTE = ("not found", "No such file", "Permission denied")


def ctl(comando: str, timeout: float = 20.0) -> subprocess.CompletedProcess:
    """Manda `comando` ao control.sock pelo SSH e devolve a resposta."""
    # Aspas simples no remoto: o comando pode ter argumento com espaco
    # (`LAUNCH_APP /user/ports/x`); o shell do console repassa os args.
    seguro = comando.replace("'", "'\\''")
    remoto = f"{CLIENTE_CONSOLE} '{seguro}'"
    return ssh_exec(remoto, timeout=timeout)


def cmd_ctl(args) -> int:
    comando = args.comando if isinstance(args.comando, str) else " ".join(args.comando)

    r = ctl(comando)
    resposta = r.stdout.strip()

    if r.returncode != 0 and not resposta:
        juntou = f"{r.stderr}".lower()
        if any(marca.lower() in juntou for marca in MARCAS_SEM_CLIENTE):
            erro(f"o console nao tem o cliente do control.sock "
                 f"({CLIENTE_CONSOLE}).")
            erro("  ele vem do pacote giochi-tools (BR2_PACKAGE_GIOCHI_TOOLS);")
            erro("  numa imagem antiga, atualize o cartao.")
            return 5
        erro(f"falha ao falar com o control.sock: {r.stderr.strip()}")
        return r.returncode if r.returncode else 1

    # `nc` do BusyBox sai != 0 quando a conexao fecha; a resposta e' o que vale.
    log(f"{comando}  ->  {resposta or '(sem resposta)'}")
    return 0 if resposta.startswith(("OK", "PONG")) else 1


def cmd_echo(args) -> int:
    comando = " ".join(args.comando)
    if args.tail:
        comando = f"{comando} | tail -n {args.tail}"
    r = ssh_exec(comando, timeout=args.timeout)
    sys.stdout.write(r.stdout)
    if r.stderr:
        sys.stderr.write(r.stderr)
    return r.returncode


def cmd_status(args) -> int:
    """Resumo do que interessa num unico round-trip."""
    script = "; ".join([
        'echo "== uptime"; cat /proc/uptime',
        'echo "== tema"; cat /usr/share/giochi/device-theme.json 2>/dev/null',
        'echo "== settings"; grep -E "theme_id|ssh_usb_enabled" '
        '/user/settings.json 2>/dev/null',
        'echo "== sockets"; ls -l /run/giochi/ 2>/dev/null',
        'echo "== sshd"; pidof dropbear >/dev/null && echo "dropbear ativo" '
        '|| echo "dropbear parado"',
        'echo "== processos"; ps w | head -12',
    ])
    r = ssh_exec(script, timeout=args.timeout)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    return r.returncode


def cmd_listar(args) -> int:
    log("comandos do control.sock (o app de jogos usa estes):")
    for verbo, desc in COMANDOS.items():
        log(f"  {verbo:34s} {desc}")
    log("")
    log("resposta: uma linha, 'OK ...' ou 'ERR <motivo>'.")
    log("detalhes: docs/APP-SDK.md")
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        prog="giochi-ctl",
        description="Fala com o console por SSH: prepara o PC, consulta o "
                    "control.sock e roda comandos.")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("preparar", help="configura usb0 e espera o SSH")
    p.add_argument("--wait", type=int, default=30, help="segundos de espera")
    p.set_defaults(func=cmd_preparar)

    p = sub.add_parser("ctl", help="manda um comando ao control.sock")
    p.add_argument("comando", nargs="+")
    p.set_defaults(func=cmd_ctl)

    p = sub.add_parser("echo", help="roda um comando de shell no console")
    p.add_argument("--tail", type=int, default=0)
    p.add_argument("--timeout", type=float, default=30.0)
    p.add_argument("comando", nargs="+")
    p.set_defaults(func=cmd_echo)

    p = sub.add_parser("status", help="resumo: uptime, tema, sockets, sshd")
    p.add_argument("--timeout", type=float, default=30.0)
    p.set_defaults(func=cmd_status)

    p = sub.add_parser("comandos", help="lista o protocolo do control.sock")
    p.set_defaults(func=cmd_listar)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
