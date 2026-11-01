"""`tools/giochi-ctl.py` — o cliente que fala com o console por SSH.

O que este teste prova, sem aparelho e sem servidor SSH:

  * o cliente monta o comando remoto certo e **lê a resposta** do control.sock;
  * `PONG` e `OK ...` viram sucesso (código 0), `ERR ...` vira falha (1);
  * `preparar` não mente quando não há interface USB;
  * `comandos` lista o mesmo conjunto que o teste de protocolo exercita.

O truque: um `ssh` falso no PATH que só executa o último argumento com `sh -c`.
O control.sock é o do **giochi-init real**, num `GIOCHI_ROOT` temporário. Ou
seja, o caminho é o de produção — menos o SSH, que é substituído.
"""

from __future__ import annotations

import os
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CTL = ROOT / "tools" / "giochi-ctl.py"
INIT = ROOT / "build" / "host" / "bin" / "giochi-init"

SSH_FALSO = """#!/bin/sh
# Descarta as opcoes (-o k=v, usuario@host) e roda o comando remoto local.
for ultimo; do :; done
exec sh -c "$ultimo"
"""

# O console tem `/usr/bin/giochi-ctl` (src/tools/giochi-ctl.c), que fala o
# protocolo de verdade com o control.sock do giochi-init. Aqui ele e' um
# script que faz o mesmo com o `nc` do HOST — o que se testa e' o cliente
# Python (montagem do comando remoto, leitura da resposta, codigo de saida),
# nao o binario C, que tem compilacao propria na imagem.
CTL_CONSOLE_FALSO = """#!/bin/sh
# $1 = comando (pode ter argumento com espaco)
printf '%s\\n' "$1" | nc -U "$GIOCHI_CTL_SOCK"
rc=$?
[ $rc -eq 0 ] || exit $rc
exit 0
"""

# `ip` falso: a maquina de desenvolvimento TEM interface de gadget quando o
# console esta' plugado (foi assim que este teste comecou a falhar — ele
# assumia "nenhuma interface USB no host"). A deteccao continua sendo a real;
# o que e' fixo e' o ambiente: sem nenhuma usb*/enp0s*/enx* na lista.
IP_FALSO = """#!/bin/sh
cat <<'FIM'
lo               UNKNOWN        127.0.0.1/8
wlp6s0           UP             192.168.1.4/24
FIM
"""


class CtlTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not INIT.is_file():
            raise unittest.SkipTest("giochi-init nao compilado (make all)")
        if subprocess.run(["sh", "-c", "command -v nc"], capture_output=True
                          ).returncode != 0:
            raise unittest.SkipTest("nc ausente no host")

        cls.tmp = tempfile.TemporaryDirectory()
        base = Path(cls.tmp.name)
        for sub in ("run/giochi", "user", "usr", "etc/giochi", "bin"):
            (base / sub).mkdir(parents=True, exist_ok=True)

        cls.ssh = base / "bin" / "ssh"
        cls.ssh.write_text(SSH_FALSO)
        cls.ssh.chmod(0o755)

        cls.ip = base / "bin" / "ip"
        cls.ip.write_text(IP_FALSO)
        cls.ip.chmod(0o755)

        cls.ctl_console = base / "bin" / "giochi-ctl"
        cls.ctl_console.write_text(CTL_CONSOLE_FALSO)
        cls.ctl_console.chmod(0o755)

        profile = base / "profile.json"
        profile.write_text(
            '{"device":"host","soc":"host","os_version":"1.2",'
            '"kernel_version":"0.0","ram_mb":1024,"vram_mb":64}')

        env = dict(os.environ, GIOCHI_ROOT=str(base), GIOCHI_LOG_LEVEL="error",
                   GIOCHI_DRY_RUN="1")
        cls.proc = subprocess.Popen(
            [str(INIT), "--profile", str(profile), "--no-spawn"],
            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        cls.sock = base / "run" / "giochi" / "control.sock"
        limite = time.time() + 10.0
        while time.time() < limite and not cls.sock.exists():
            if cls.proc.poll() is not None:
                raise unittest.SkipTest("giochi-init saiu antes do socket")
            time.sleep(0.05)
        cls.base = base

    @classmethod
    def tearDownClass(cls):
        if getattr(cls, "proc", None) is not None and cls.proc.poll() is None:
            cls.proc.terminate()
            try:
                cls.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                cls.proc.kill()
        if getattr(cls, "tmp", None) is not None:
            cls.tmp.cleanup()

    def rodar(self, *args: str) -> subprocess.CompletedProcess:
        env = dict(os.environ,
                   PATH=f"{self.base / 'bin'}:{os.environ['PATH']}",
                   GIOCHI_SSH="ssh",
                   GIOCHI_CTL_SOCK=str(self.sock),
                   GIOCHI_CTL_CONSOLE=str(self.ctl_console),
                   GIOCHI_HOST="127.0.0.1")
        return subprocess.run(["python3", str(CTL), *args],
                              capture_output=True, text=True, env=env,
                              timeout=60)

    # -- caminho feliz ---------------------------------------------------

    def test_ping_por_ctl(self):
        r = self.rodar("ctl", "PING")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("PONG", r.stdout)

    def test_comando_com_argumento(self):
        r = self.rodar("ctl", "USB_MODE", "MASS")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("OK usb_mode MASS", r.stdout)

    def test_erro_do_console_vira_codigo_de_saida_1(self):
        """`ERR` não pode sair com 0: num script, isso esconde a falha."""
        r = self.rodar("ctl", "USB_MODE", "ZIP")
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("ERR invalid_mode", r.stdout)

    def test_set_theme_reporta_nao_implementado(self):
        r = self.rodar("ctl", "SET_THEME", "neon_rose")
        self.assertEqual(r.returncode, 1)
        self.assertIn("not_implemented", r.stdout)

    # -- casos de borda --------------------------------------------------

    def test_sem_interface_usb_preparar_falha_com_dica(self):
        r = self.rodar("preparar", "--wait", "1")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("interface USB", r.stderr)


    def test_preparar_solta_a_interface_do_networkmanager(self):
        """O NM apaga o IP manual: a rede USB fica intermitente.

        Medido numa X55, com o gadget funcionando: `ping` respondendo numa
        execucao e "Connection timed out" na seguinte, com `ip addr` mostrando
        que o 10.55.0.1/24 tinha DESAPARECIDO. O NetworkManager via uma
        ethernet sem DHCP (o console nao tem DHCP por projeto, §4.6.1), ficava
        em "conectando (obtendo configuracao de IP)" e limpava o endereco.
        """
        import pathlib

        fonte = pathlib.Path(CTL).read_text()
        self.assertIn("nmcli", fonte,
                      "preparar tem de saber do NetworkManager")
        self.assertIn('"device", "set", iface, "managed", "no"', fonte,
                      "e' isso que impede o NM de desfazer o IP")
        # A chamada tem de acontecer ANTES de atribuir o endereco.
        self.assertLess(fonte.index("nm_gerencia(iface)"),
                        fonte.index('"addr", "add"'),
                        "soltar o NM depois de atribuir o IP nao resolve: o "
                        "NM ja' apagou")


    def test_o_cliente_do_console_e_o_binario_e_nao_nc_U(self):
        """O nc do BusyBox não fala soquete Unix — `nc -U` nunca funcionou.

        `networking/nc.c` do BusyBox 1.36.1 tem ZERO ocorrências de `AF_UNIX`,
        e `CONFIG_NC_UNIX` não existe (pedir no fragmento é descartado em
        silêncio). Medido na X55: `sh: nc: not found` mesmo com
        `CONFIG_NC=y` ligado.

        O console agora traz `/usr/bin/giochi-ctl`, compilado com o mesmo
        `giochi_sock_*` do supervisor.
        """
        import pathlib

        fonte = pathlib.Path(CTL).read_text()
        # So' as LINHAS DE CODIGO: o comentario que explica por que o nc nao
        # serve cita `nc -U` de proposito.
        codigo = [l for l in fonte.splitlines()
                  if l.strip() and not l.lstrip().startswith("#")]
        self.assertFalse(any("nc -U" in l for l in codigo),
                         "o cliente do PC nao pode chamar `nc -U`: o nc do "
                         "BusyBox nao suporta soquete Unix")
        self.assertIn("giochi-ctl", fonte,
                      "o cliente tem de invocar o binario do console")

        # E o binario tem de estar no build do alvo, senao o erro vira
        # "not found" no aparelho, que foi o sintoma original.
        mk = (pathlib.Path(ROOT) / "buildroot/package/giochi-tools"
              / "giochi-tools.mk").read_text()
        self.assertIn("src/tools/giochi-ctl.c", mk,
                      "o giochi-ctl nao e' compilado para o alvo")
        bloco = mk.split("INSTALL_TARGET_CMDS")[1].split("endef")[0]
        self.assertIn("giochi-ctl", bloco,
                      "o giochi-ctl nao e' instalado no rootfs")


    def test_receita_do_alvo_compila_de_verdade(self):
        """Compila EXATAMENTE os arquivos que o `.mk` lista.

        Divergir entre "o que eu compilei para testar" e "o que a receita
        compila" e' o mesmo padrao do §3.88b (patch gerado contra a arvore
        errada): o teste passa e o build quebra. Aconteceu aqui — o `.mk`
        nao listava `giochi_util.c`, e o link falhou em `giochi_strlcpy`.
        """
        import pathlib
        import shutil
        import subprocess
        import tempfile

        if not shutil.which("gcc"):
            self.skipTest("sem gcc no host")
        mk = (pathlib.Path(ROOT) / "buildroot/package/giochi-tools"
              / "giochi-tools.mk").read_text()
        bloco = mk.split("GIOCHI_TOOLS_CTL_SRC = ")[1].split("\n\n")[0]
        # $(@D) e' o diretorio do pacote = raiz do repo (SITE local).
        fontes = [t.replace("$(@D)/", "").replace("\\", "").strip()
                  for t in bloco.split()]
        fontes = [f for f in fontes if f.endswith(".c")]
        self.assertTrue(fontes, "a receita nao lista fonte nenhum: %r" % bloco)

        with tempfile.TemporaryDirectory() as tmp:
            saida = pathlib.Path(tmp) / "giochi-ctl"
            r = subprocess.run(
                ["gcc", "-std=c11", "-D_GNU_SOURCE", "-Wall", "-Wextra",
                 "-Werror", "-Isrc/common", "-o", str(saida), *fontes],
                cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(r.returncode, 0,
                             "a receita do giochi-ctl nao compila:\n%s"
                             % r.stderr[-2000:])
            self.assertTrue(saida.is_file())

    def test_comandos_lista_o_protocolo(self):
        r = self.rodar("comandos")
        self.assertEqual(r.returncode, 0, r.stderr)
        for verbo in ("PING", "USB_MODE", "DEV_MODE", "LAUNCH_APP"):
            self.assertIn(verbo, r.stdout)
        # A lista tem de ser honesta sobre o SET_THEME.
        self.assertIn("NAO implementado", r.stdout)


if __name__ == "__main__":
    unittest.main()
