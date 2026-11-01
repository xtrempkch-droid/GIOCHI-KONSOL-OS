"""Contrato do `control.sock` — a comunicação que um app de jogos consome.

POR QUE ESTE TESTE EXISTE
-------------------------
Um app externo (jogo, Loja, PortMaster) não conversa com o kernel: conversa com
o `giochi-init` por `/run/giochi/control.sock`. Se o formato da resposta mudar,
o app quebra **em silêncio** — ele recebe uma string que não esperava e segue.
O `DEV_MODE` do Launcher, por exemplo, testa `reply == "OK dev_mode ON"`.

Este teste é o contrato, e roda o binário REAL (`giochi-init --no-spawn`) com
`GIOCHI_ROOT` num diretório temporário — sem root, sem aparelho, sem rede.

Protocolo (medido em src/init/control.c e src/init/main.c):

  * socket de stream UNIX, uma conexão por comando;
  * o cliente manda UMA linha (terminada em '\\n', limite de 1 s para chegar);
  * a resposta é uma linha, com '\\n' no fim, e a conexão é fechada;
  * sucesso começa com "OK ", falha com "ERR <motivo>". NUNCA texto livre:
    o motivo é um identificador estável, não uma frase traduzível.

O que **não** está coberto aqui, de propósito: `POWEROFF`/`REBOOT` derrubam o
processo (por desenho), e `WIFI_UP`/`BT_UP` dependem de rádio — esses são
verificados com a resposta de ERRO quando não há adaptador, que é o que o app
tem de tratar.
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
BIN = ROOT / "build" / "host" / "bin" / "giochi-init"

TIMEOUT_S = 2.0


class InitHarness(unittest.TestCase):
    """Sobe um `giochi-init` de verdade e conversa com ele.

    Base das duas classes: uma sobe o processo uma vez por classe (o init e'
    caro de subir) e cada teste abre a propria conexao.
    """

    proc: subprocess.Popen | None = None
    tmp: tempfile.TemporaryDirectory | None = None
    sock_path: Path

    @classmethod
    def setUpClass(cls):
        if not BIN.is_file():
            raise unittest.SkipTest("giochi-init nao compilado (make all)")

        cls.tmp = tempfile.TemporaryDirectory()
        base = Path(cls.tmp.name)

        for sub in ("run/giochi", "user", "usr", "etc/giochi", "var/log"):
            (base / sub).mkdir(parents=True, exist_ok=True)

        profile = base / "profile.json"
        profile.write_text(
            '{"device":"host","soc":"host","os_version":"1.2",'
            '"kernel_version":"0.0","ram_mb":1024,"vram_mb":64}',
            encoding="utf-8")

        # --no-spawn: sem Launcher. Os scripts de init que ele chamaria nao
        # existem no host, e o teste nao quer processos de verdade.
        env = dict(os.environ, GIOCHI_ROOT=str(base), GIOCHI_LOG_LEVEL="error",
                   GIOCHI_DRY_RUN="1")
        cls.proc = subprocess.Popen(
            [str(BIN), "--profile", str(profile), "--no-spawn"],
            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        cls.sock_path = base / "run" / "giochi" / "control.sock"
        limite = time.time() + 10.0
        while time.time() < limite:
            if cls.sock_path.exists():
                # O arquivo existe antes do listen(): conectar nesse intervalo
                # recebe ECONNREFUSED (§3.41). Sondar com PING resolve.
                try:
                    cls.command("PING")
                    return
                except OSError:
                    pass
            if cls.proc.poll() is not None:
                raise unittest.SkipTest("giochi-init saiu antes de criar o socket")
            time.sleep(0.05)
        raise unittest.SkipTest("control.sock nao apareceu")

    @classmethod
    def tearDownClass(cls):
        if cls.proc is not None and cls.proc.poll() is None:
            cls.proc.terminate()
            try:
                cls.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                cls.proc.kill()
        if cls.tmp is not None:
            cls.tmp.cleanup()

    @classmethod
    def command(cls, texto: str) -> str:
        """Manda uma linha e devolve a resposta, sem o '\\n' final.

        Reconecta em ECONNREFUSED: o socket existe antes do `listen()`, e um
        cliente correto tenta de novo (§3.41).
        """
        limite = time.time() + 3.0
        ultimo: OSError | None = None
        while time.time() < limite:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.settimeout(TIMEOUT_S)
            try:
                s.connect(str(cls.sock_path))
                s.sendall(texto.encode() + b"\n")
                dados = b""
                while not dados.endswith(b"\n"):
                    parte = s.recv(4096)
                    if not parte:
                        break
                    dados += parte
                return dados.decode().rstrip("\n")
            except (ConnectionRefusedError, FileNotFoundError) as exc:
                ultimo = exc
                time.sleep(0.05)
            finally:
                s.close()
        raise OSError(f"control.sock inacessivel: {ultimo}")

class ControlSocketTest(InitHarness):
    """Contrato: verbo -> resposta, forma exata."""

    def test_ping_responde_pong(self):
        self.assertEqual(self.command("PING"), "PONG")

    def test_espaco_antes_do_verbo_e_ignorado(self):
        # O handler faz trim a' esquerda; cliente que manda "  PING" e' aceito.
        self.assertEqual(self.command("   PING"), "PONG")

    def test_resposta_termina_com_uma_linha(self):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(TIMEOUT_S)
        try:
            s.connect(str(self.sock_path))
            s.sendall(b"PING\n")
            dados = s.recv(4096)
        finally:
            s.close()
        self.assertTrue(dados.endswith(b"\n"), f"sem '\\n' final: {dados!r}")
        self.assertEqual(dados.count(b"\n"), 1, "resposta com mais de uma linha")

    def test_comando_desconhecido_tem_motivo_estavel(self):
        self.assertEqual(self.command("NAO_EXISTE"), "ERR unknown_command")

    def test_usb_mode_alterna_e_valida(self):
        self.assertEqual(self.command("USB_MODE MTP"), "OK usb_mode MTP")
        self.assertEqual(self.command("USB_MODE MASS"), "OK usb_mode MASS")
        self.assertEqual(self.command("USB_MODE"), "ERR missing_mode")
        self.assertEqual(self.command("USB_MODE ZIP"), "ERR invalid_mode")

    def test_dev_mode_exige_on_ou_off(self):
        # O valor e' ecoado como veio: o Launcher compara com "OK dev_mode ON".
        self.assertEqual(self.command("DEV_MODE ON"), "OK dev_mode ON")
        self.assertEqual(self.command("DEV_MODE OFF"), "OK dev_mode OFF")
        self.assertEqual(self.command("DEV_MODE TALVEZ"), "ERR invalid_mode")
        self.assertEqual(self.command("DEV_MODE"), "ERR invalid_mode")

    def test_set_theme_admite_que_nao_esta_implementado(self):
        """Antes respondia "OK theme <id>" sem fazer NADA.

        O canal existente e' Launcher -> init; trocar o tema e' o caminho
        inverso, e nao ha' evento init -> Launcher na v1.2. Um app que recebe
        "OK" e o tema nao muda fica impossivel de depurar — a resposta tem de
        dizer que nao foi feito (§3.78, promessa sem entrega).
        """
        r = self.command("SET_THEME neon_rose")
        self.assertTrue(r.startswith("ERR "), f"respondeu '{r}' sem fazer nada")
        self.assertIn("not_implemented", r)
        self.assertEqual(self.command("SET_THEME"), "ERR missing_theme")

    def test_launch_app_de_caminho_inexistente_falha_com_motivo(self):
        # App ausente NAO pode virar sucesso silencioso: o Launcher trataria
        # como aberto e nao haveria nada na tela.
        r = self.command("LAUNCH_APP /nao/existe/app")
        self.assertTrue(r.startswith("ERR "), r)
        self.assertEqual(self.command("LAUNCH_APP"), "ERR missing_app")

    def test_radio_sem_adaptador_responde_erro_tratavel(self):
        for cmd, nome in (("WIFI_UP", "wifi_up"), ("BT_UP", "bt_up")):
            r = self.command(cmd)
            self.assertTrue(
                r in ("OK " + nome, "ERR no_adapter", f"ERR {nome}_failed"),
                f"{cmd} devolveu '{r}', que o app nao sabe tratar")

    def test_supervisor_sobrevive_a_cliente_que_nao_le(self):
        """SIGPIPE (§3.40): cliente que fecha sem ler nao pode matar o init."""
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            s.connect(str(self.sock_path))
            s.sendall(b"PING\n")
        finally:
            s.close()  # fecha sem recv
        time.sleep(0.2)
        self.assertIsNone(self.proc.poll(), "giochi-init morreu (SIGPIPE)")
        self.assertEqual(self.command("PING"), "PONG")

    def test_muitos_comandos_seguidos(self):
        """O app pode falar em rajada; o servidor atende uma conexão por vez."""
        for _ in range(40):
            self.assertEqual(self.command("PING"), "PONG")


class FramingTest(InitHarness):
    """Enquadramento que um cliente em C/Godot precisa respeitar."""

    def test_linha_sem_quebra_nao_derruba_o_servidor(self):
        """O servidor espera '\n'. Mandar bytes sem ele nao pode matar o init.

        `giochi_sock_read_line` espera ate' 1000 ms pelo terminador; 4 KB sem
        quebra fazem a conexao fechar sem resposta. O que o app precisa saber:
        o servidor continua vivo, mas aquele comando estava perdido. Sempre
        termine a linha.
        """
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(TIMEOUT_S)
        try:
            s.connect(str(self.sock_path))
            s.sendall(b"A" * 4000)   # sem quebra de linha
            try:
                s.recv(64)
            except OSError:
                pass
        finally:
            s.close()

        self.assertIsNone(self.proc.poll(), "init morreu com linha sem terminador")
        self.assertEqual(self.command("PING"), "PONG")

    def test_uma_conexao_atende_um_comando(self):
        """Contrato: uma conexao = um comando. O resto do buffer e' descartado.

        Um cliente que reusa a conexao recebe so' a primeira resposta. Melhor
        descobrir aqui do que no aparelho.
        """
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(TIMEOUT_S)
        try:
            s.connect(str(self.sock_path))
            s.sendall(b"PING\nUSB_MODE MASS\n")
            dados = b""
            while not dados.endswith(b"\n"):
                parte = s.recv(4096)
                if not parte:
                    break
                dados += parte
        finally:
            s.close()
        self.assertEqual(dados.decode().rstrip("\n"), "PONG")

    def test_verbo_e_case_sensitive(self):
        """'ping' NAO e' 'PING'. App escrito em outra linguagem tende a
        normalizar caixa; o protocolo nao aceita."""
        self.assertEqual(self.command("ping"), "ERR unknown_command")


if __name__ == "__main__":
    unittest.main()
