#!/usr/bin/env python3
"""O cliente do control.sock que roda NO CONSOLE (src/tools/giochi-ctl.c).

POR QUE ESTE TESTE EXISTE
-------------------------
Este binario nao era exercitado por nada. O `make` nao o compilava (so' o
`make lint` compilava para /tmp a fim de checar sintaxe) e nenhuma suite o
executava. Duas consequencias medidas em 2026-11-02:

1. `giochi-ctl --comandos` nao listava BT_UP/BT_DOWN havia semanas — o
   supervisor implementa os dois, o cliente em Python e o docs/APP-SDK.md os
   listam. Quem consultasse o APARELHO concluiria que Bluetooth nao existe no
   protocolo. A divergencia passou porque nada comparava as quatro fontes.
2. Um console travado (ou morto) fazia `giochi-ctl PING` sair com **0** e
   stdout vazio: o `giochi_sock_read_line` devolve 0 tanto no fim de conexao
   quanto no timeout do `poll`, e o cliente so' tratava `n < 0` como erro. Num
   script, "sem resposta" era indistinguivel de "PONG"; o cliente em Python ja'
   tratava resposta vazia como falha, entao os dois clientes do MESMO protocolo
   discordavam no veredito.

O que e' coberto aqui:
  A. os verbos que o supervisor aceita aparecem nas QUATRO fontes (C, Python,
     docs/APP-SDK.md) — verbos extraidos do fonte do supervisor, nao listados
     a mao, para o teste cobrar comando novo;
  B. o binario de verdade contra um soquete de verdade: PONG -> 0, OK -> 0,
     ERR -> 1, resposta estranha -> 1, nada recebido -> 2, fechou sem
     responder -> 2;
  C. opcao sem valor / valor invalido recusa dizendo QUAL e' o problema.
"""

from __future__ import annotations

import os
import pathlib
import re
import socket
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
CTL_C = ROOT / "src/tools/giochi-ctl.c"
CTL_PY = ROOT / "tools/giochi-ctl.py"
SDK = ROOT / "docs/APP-SDK.md"
SUPERVISOR = ROOT / "src/init/main.c"
BIN = ROOT / "build/host/bin/giochi-ctl"


def read(path):
    with open(path, encoding="utf-8") as fp:
        return fp.read()


def verbos_do_supervisor():
    """O que o giochi-init aceita, lido do codigo — nao de uma lista copiada."""
    fonte = read(SUPERVISOR)
    return set(re.findall(r'strcmp\(verb,\s*"([A-Z][A-Z0-9_]*)"\)', fonte))


def texto_do_cliente_c():
    """So' os literais de `puts()` de lista_comandos.

    NAO use o arquivo inteiro: o comentario de lista_comandos CITA BT_UP e
    BT_DOWN ao explicar por que eles ficaram de fora uma vez. Procurar o verbo
    no texto do arquivo fazia o teste passar com a linha do `puts` removida —
    casar com COMENTARIO em vez de codigo e' a classe de defeito do AGENTS.md
    §3.36/§3.118, e este teste ja' caiu nela uma vez.
    """
    fonte = read(CTL_C)
    ini = fonte.index("static void lista_comandos")
    fim = fonte.index("\n}", ini)
    return " ".join(re.findall(r'puts\("([^"]*)"\)', fonte[ini:fim]))


def texto_do_cliente_py():
    """As CHAVES do dict COMANDOS (o comentario nao entra)."""
    fonte = read(CTL_PY)
    ini = fonte.index("COMANDOS = {")
    fim = fonte.index("\n}", ini)
    return " ".join(re.findall(r'"([^"]*)":', fonte[ini:fim]))


def texto_da_doc():
    """So' as linhas de TABELA do APP-SDK.md (linha que comeca com '| `').

    A prosa do documento cita SET_THEME e SET_DATE em paragrafos explicativos;
    aceitar isso deixaria uma linha de tabela removida passar.
    """
    linhas = [l for l in read(SDK).splitlines() if l.startswith("| `")]
    return "\n".join(linhas)


def faltando(verbos, texto):
    """Verbos ausentes como PALAVRA INTEIRA (SET_DATE nao vale por SET_DATEX)."""
    return [v for v in sorted(verbos)
            if not re.search(rf"(?<![A-Z_]){re.escape(v)}(?![A-Z_])", texto)]


class ProtocoloNasQuatroFontes(unittest.TestCase):
    """A (verbos x fontes). Um comando novo no supervisor tem de aparecer no
    cliente do console, no cliente do PC e na documentacao."""

    @classmethod
    def setUpClass(cls):
        cls.verbos = verbos_do_supervisor()
        if not cls.verbos:
            raise unittest.SkipTest("nao achei verbo nenhum em src/init/main.c")

    def test_achei_o_protocolo_no_supervisor(self):
        # Sanidade: se a extracao falhar, os testes abaixo passariam em vazio.
        for esperado in ("PING", "USB_MODE", "SET_DATE"):
            self.assertIn(esperado, self.verbos)

    def test_cliente_do_console_lista_todos_os_verbos(self):
        ausentes = faltando(self.verbos, texto_do_cliente_c())
        self.assertEqual(
            [], ausentes,
            f"giochi-ctl.c nao lista {ausentes} (o --comandos do console "
            f"mostra menos do que o aparelho aceita)")

    def test_cliente_do_pc_lista_todos_os_verbos(self):
        ausentes = faltando(self.verbos, texto_do_cliente_py())
        self.assertEqual([], ausentes,
                         f"tools/giochi-ctl.py nao lista {ausentes}")

    def test_documentacao_lista_todos_os_verbos(self):
        ausentes = faltando(self.verbos, texto_da_doc())
        self.assertEqual([], ausentes,
                         f"docs/APP-SDK.md nao documenta {ausentes}")


class ComportamentoDoBinario(unittest.TestCase):
    """B e C: roda o binario real contra um soquete Unix real."""

    @classmethod
    def setUpClass(cls):
        if not BIN.is_file():
            raise unittest.SkipTest(f"{BIN} nao compilado (rode 'make')")
        cls.tmp = tempfile.mkdtemp(prefix="giochi-ctl-")
        cls.n = 0

    # --- infraestrutura ----------------------------------------------------
    def _servidor(self, resposta: bytes, fecha_antes: bool = False,
                  demora: float = 0.0):
        """Sobe um soquete que responde (ou nao) e devolve o caminho."""
        type(self).n += 1
        caminho = os.path.join(self.tmp, f"s{self.n}.sock")
        srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        srv.bind(caminho)
        srv.listen(1)

        def atende():
            try:
                con, _ = srv.accept()
                try:
                    if fecha_antes:
                        con.close()
                        return
                    con.recv(1024)
                    if demora:
                        time.sleep(demora)
                    if resposta:
                        con.sendall(resposta)
                finally:
                    con.close()
            finally:
                srv.close()

        threading.Thread(target=atende, daemon=True).start()
        return caminho

    def _roda(self, *args):
        return subprocess.run([str(BIN), *args], capture_output=True,
                              text=True, timeout=60)

    def _fala(self, caminho, *args):
        return self._roda("--socket", caminho, *args)

    # --- respostas validas -------------------------------------------------
    def test_pong_sai_com_zero(self):
        p = self._servidor(b"PONG\n")
        r = self._fala(p, "PING")
        self.assertEqual(0, r.returncode, r.stderr)
        self.assertEqual("PONG\n", r.stdout)

    def test_ok_sai_com_zero(self):
        p = self._servidor(b"OK usb_mode MASS\n")
        r = self._fala(p, "USB_MODE", "MASS")
        self.assertEqual(0, r.returncode, r.stderr)
        self.assertEqual("OK usb_mode MASS\n", r.stdout)

    def test_err_sai_com_um(self):
        p = self._servidor(b"ERR invalid_mode\n")
        r = self._fala(p, "USB_MODE", "XX")
        self.assertEqual(1, r.returncode,
                         "ERR nao pode sair com 0: num script isso esconde a falha")

    def test_resposta_estranha_sai_com_um(self):
        """O protocolo so' tem OK/PONG/ERR. Qualquer outra linha e' defeito de
        um dos lados e nao pode passar como sucesso (o cliente em Python ja'
        recusava)."""
        p = self._servidor(b"talvez\n")
        r = self._fala(p, "PING")
        self.assertEqual(1, r.returncode, r.stdout + r.stderr)
        self.assertIn("inesperada", r.stderr)

    # --- respostas ausentes: o defeito de 2026-11-02 ------------------------
    def test_sem_resposta_sai_com_dois(self):
        """Console travado: nada chega. Antes disto sair com 0 e stdout vazio,
        o que num script e' indistinguivel de um PING respondido."""
        p = self._servidor(b"", demora=1.0)
        r = self._fala(p, "--timeout", "250", "PING")
        self.assertEqual(2, r.returncode,
                         f"sem resposta tem de ser falha, nao sucesso "
                         f"({r.returncode}, out={r.stdout!r})")
        self.assertEqual("", r.stdout)
        self.assertIn("nao respondeu", r.stderr)

    def test_fechou_sem_responder_sai_com_dois(self):
        p = self._servidor(b"", fecha_antes=True)
        r = self._fala(p, "--timeout", "2000", "PING")
        self.assertEqual(2, r.returncode, r.stdout + r.stderr)
        self.assertNotEqual("", r.stderr, "falha sem mensagem nao se diagnostica")

    def test_distingue_travado_de_morto(self):
        """As duas causas pedem acao diferente, entao a mensagem tem de dizer
        qual foi. 'nao respondeu em 2000 ms' mentia quando o par fechava na
        hora."""
        p = self._servidor(b"", fecha_antes=True)
        morto = self._fala(p, "--timeout", "2500", "PING")
        p = self._servidor(b"", demora=1.0)
        travado = self._fala(p, "--timeout", "250", "PING")
        self.assertIn("nao respondeu", travado.stderr)
        self.assertNotEqual(morto.stderr.strip(), travado.stderr.strip(),
                            "as duas causas saem com a mesma mensagem")

    def test_soquete_inexistente_sai_com_dois(self):
        r = self._roda("--socket", os.path.join(self.tmp, "nao-existe.sock"),
                       "PING")
        self.assertEqual(2, r.returncode)
        self.assertIn("nao existe", r.stderr)

    # --- opcoes: recusar com o motivo --------------------------------------
    def test_socket_sem_valor_diz_que_faltou_o_valor(self):
        r = self._roda("--socket")
        self.assertEqual(2, r.returncode)
        self.assertIn("exige um caminho", r.stderr)
        self.assertNotIn("desconhecida", r.stderr,
                         "dizer 'opcao desconhecida' manda procurar uma opcao "
                         "que existe e esta' escrita certo")

    def test_timeout_sem_valor_diz_que_faltou_o_valor(self):
        r = self._roda("--timeout")
        self.assertEqual(2, r.returncode)
        self.assertIn("exige milissegundos", r.stderr)

    def test_timeout_invalido_recusa(self):
        for valor in ("abc", "0", "-5", "9e999", "999999999999"):
            with self.subTest(valor=valor):
                r = self._roda("--socket", self.tmp, "--timeout", valor, "PING")
                self.assertEqual(2, r.returncode,
                                 f"--timeout {valor} foi aceito")
                self.assertIn("invalido", r.stderr)

    # --- --comandos --------------------------------------------------------
    def test_comandos_do_binario_cobrem_o_supervisor(self):
        verbos = verbos_do_supervisor()
        if not verbos:
            self.skipTest("nao achei verbo nenhum em src/init/main.c")
        r = self._roda("--comandos")
        self.assertEqual(0, r.returncode)
        ausentes = faltando(verbos, r.stdout)
        self.assertEqual([], ausentes,
                         f"o --comandos do CONSOLE nao mostra {ausentes}")


class CompilaNoMake(unittest.TestCase):
    """O binario tem de sair do `make`: enquanto ele nao saia, nenhum teste
    podia executa-lo, e foi assim que a divergencia de verbos passou."""

    def test_esta_nos_alvos_do_make(self):
        fonte = read(ROOT / "Makefile")
        alvos = fonte[fonte.index("TARGETS :="):]
        alvos = alvos[:alvos.index("\n\n")]
        self.assertIn("giochi-ctl", alvos,
                      "giochi-ctl fora de TARGETS: `make` nao o compila")

    def test_tem_regra_de_compilacao_para_src_tools(self):
        fonte = read(ROOT / "Makefile")
        self.assertIn("$(OBJ)/tools/giochi-ctl.o: $(CTL_SRC)", fonte,
                      "sem regra explicita, o objeto de src/tools so' sai por "
                      "coincidencia do padrao generico")


if __name__ == "__main__":
    unittest.main(verbosity=2)
