#!/usr/bin/env python3
"""Guarda do tools/check-dts-panel.py.

Motivo: uma imagem cujo DTS não tem painel utilizável NUNCA acende a tela, e o
build termina com sucesso. Foi assim que um ciclo de flash foi perdido
(`GIOCHI_DTS_FALLBACK=rk3566-anbernic-rg353v` — que por sorte TEM painel via
`#include` do rg353p; outros não têm).

Cada teste aqui é um caso real de decisão: tem painel mainline? tem painel que
exige driver fora da árvore? não tem painel nenhum?
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, "tools", "check-dts-panel.py")


def rodar(*args):
    p = subprocess.run([sys.executable, TOOL] + list(args),
                       capture_output=True, text=True, timeout=120)
    return p.returncode, p.stdout + p.stderr


class DtsPanelTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.mkdtemp()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.dir, ignore_errors=True)

    def dts(self, nome, conteudo):
        caminho = os.path.join(self.dir, nome + ".dts")
        with open(caminho, "w", encoding="utf-8") as fh:
            fh.write(conteudo)
        return caminho

    def test_painel_mainline_passa(self):
        caminho = self.dts("ok", '/dts-v1/;\n/ {\n'
                                 '\tpanel: panel@0 {\n'
                                 '\t\tcompatible = "himax,hx8394";\n'
                                 '\t};\n};\n')
        rc, saida = rodar("--dts", caminho, "--strict")
        self.assertEqual(rc, 0, saida)
        self.assertIn("painel: ok", saida)

    def test_sem_painel_falha_em_strict(self):
        caminho = self.dts("sem", '/dts-v1/;\n/ {\n\tmodel = "x";\n};\n')
        rc, saida = rodar("--dts", caminho, "--strict")
        self.assertEqual(rc, 1, saida)
        self.assertIn("AUSENTE", saida)
        self.assertIn("tela fica preta", saida)

    def test_sem_painel_avisa_sem_strict(self):
        caminho = self.dts("sem2", '/dts-v1/;\n/ {\n\tmodel = "x";\n};\n')
        rc, saida = rodar("--dts", caminho)
        self.assertEqual(rc, 0, saida)
        self.assertIn("aviso", saida)

    def test_driver_fora_da_arvore_e_reportado(self):
        # rocknix,generic-dsi NAO existe no mainline: e o painel real do
        # X35S/X35H, e por isso esses aparelhos ainda nao podem usa-lo.
        caminho = self.dts("fora", '/dts-v1/;\n/ {\n'
                                    '\tpanel: panel@0 {\n'
                                    '\t\tcompatible = "rocknix,generic-dsi";\n'
                                    '\t};\n};\n')
        rc, saida = rodar("--dts", caminho, "--strict")
        self.assertEqual(rc, 1, saida)
        self.assertIn("FORA DA ÁRVORE", saida)

    def test_segue_include(self):
        # O painel pode estar no DTS base, nao no delta do aparelho.
        base = os.path.join(self.dir, "base.dts")
        with open(base, "w", encoding="utf-8") as fh:
            fh.write('/dts-v1/;\n/ {\n'
                     '\tpanel: panel@0 {\n'
                     '\t\tcompatible = "newvision,nv3051d";\n'
                     '\t};\n};\n')
        delta = os.path.join(self.dir, "delta.dts")
        with open(delta, "w", encoding="utf-8") as fh:
            fh.write('#include "base.dts"\n/ {\n\tmodel = "delta";\n};\n')
        rc, saida = rodar("--dts", delta, "--strict")
        self.assertEqual(rc, 0, saida)
        self.assertIn("painel: ok", saida)

    def test_include_nao_resolvido_nao_mente(self):
        caminho = self.dts("inc", '#include "nao-existe.dtsi"\n/ {\n};\n')
        rc, saida = rodar("--dts", caminho, "--strict")
        # '?' e nao 'ok': não dá para afirmar que tem painel.
        self.assertIn("não resolvido", saida)
        self.assertNotIn("painel: ok", saida)

    def test_todos_os_aparelhos_executam(self):
        rc, saida = rodar()
        self.assertEqual(rc, 0, saida)
        for dev in ("x55", "x35h", "x35s"):
            self.assertIn(dev, saida)


if __name__ == "__main__":
    unittest.main()
