#!/usr/bin/env python3
"""Testa tools/verify-card.py com arquivos regulares (sem hardware).

Por que existe: a ferramenta é a única forma de distinguir "a gravação do
cartão não terminou" de "o cartão tem setor ruim" — duas causas com ações
opostas. Se ela mentir, a pessoa regrava um cartão defeituoso para sempre (ou
troca um cartão bom).

O "cartão" aqui é um arquivo regular. O caso de erro de leitura não é
simulável sem hardware, então ele é verificado no código (a ferramenta tem de
tratar OSError sem abortar).
"""
import lzma
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, "tools", "verify-card.py")


def rodar(*args):
    """Executa a ferramenta de verdade e devolve (rc, stdout+stderr)."""
    p = subprocess.run([sys.executable, TOOL] + list(args),
                       capture_output=True, text=True, timeout=300)
    return p.returncode, p.stdout + p.stderr


class VerifyCardTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.mkdtemp()
        # Determinístico de propósito: teste tem de ser reproduzível.
        cls.dados = bytes((i * 37 + 11) & 0xFF for i in range(3 * 1024 * 1024))
        cls.img = os.path.join(cls.dir, "img.img")
        with open(cls.img, "wb") as fh:
            fh.write(cls.dados)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.dir, ignore_errors=True)

    def arquivo(self, nome, conteudo):
        caminho = os.path.join(self.dir, nome)
        with open(caminho, "wb") as fh:
            fh.write(conteudo)
        return caminho

    def test_arquivo_identico_passa(self):
        disco = self.arquivo("ok.img", self.dados)
        rc, saida = rodar(self.img, disco)
        self.assertEqual(rc, 0, saida)
        self.assertIn("OK: o cartao e' identico", saida)

    def test_dados_diferentes_aponta_offset_e_setor(self):
        # Reproduz o caso medido: imagem boa, cartão devolve outro byte.
        sujo = bytearray(self.dados)
        sujo[1000] ^= 0xFF
        disco = self.arquivo("sujo.img", bytes(sujo))
        rc, saida = rodar(self.img, disco)
        self.assertEqual(rc, 1, saida)
        self.assertIn("DADOS DIFERENTES", saida)
        self.assertIn("setor 1", saida)     # 1000 // 512 = 1
        self.assertIn("gravacao NAO terminou", saida)

    def test_cartao_truncado_e_reportado(self):
        # dd sem conv=fsync + remoção cedo deixa o cartão mais curto.
        disco = self.arquivo("curto.img", self.dados[:1024 * 1024])
        rc, saida = rodar(self.img, disco)
        self.assertEqual(rc, 1, saida)
        self.assertIn("tamanhos diferentes", saida)

    def test_imagem_xz(self):
        xz = os.path.join(self.dir, "img.img.xz")
        with lzma.open(xz, "wb") as fh:
            fh.write(self.dados)
        disco = self.arquivo("ok2.img", self.dados)
        rc, saida = rodar(xz, disco)
        self.assertEqual(rc, 0, saida)

    def test_imagem_ausente_falha_com_mensagem(self):
        rc, saida = rodar(os.path.join(self.dir, "nao-existe.img"), self.img)
        self.assertEqual(rc, 2, saida)
        self.assertIn("imagem nao encontrada", saida)

    def test_recusa_dispositivo_montado(self):
        # Confundir o disco do sistema com o cartão é o pior acidente
        # possível aqui: a guarda tem de existir e ser consultada no início.
        with open(TOOL, encoding="utf-8") as fh:
            fonte = fh.read()
        self.assertIn("/proc/mounts", fonte)
        self.assertIn("desmonte antes", fonte)

    def test_nunca_abre_o_dispositivo_para_escrita(self):
        with open(TOOL, encoding="utf-8") as fh:
            fonte = fh.read()
        for proibido in ('args.dispositivo, "wb"', 'args.dispositivo, "r+"',
                         'args.dispositivo, "a"'):
            self.assertNotIn(proibido, fonte,
                             "verify-card.py abre o dispositivo para escrita")

    def test_trata_erro_de_leitura_sem_abortar(self):
        # Setor ruim não pode virar traceback: é diagnosticamente diferente de
        # "dados errados" e a mensagem tem de dizer isso.
        with open(TOOL, encoding="utf-8") as fh:
            fonte = fh.read()
        self.assertIn("except OSError", fonte)
        self.assertIn("ERRO(S) DE LEITURA", fonte)
        self.assertIn("setor defeituoso", fonte)


if __name__ == "__main__":
    unittest.main()
