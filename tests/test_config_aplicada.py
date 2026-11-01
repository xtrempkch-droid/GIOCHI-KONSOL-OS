#!/usr/bin/env python3
"""Guarda do "símbolo pedido e descartado em silêncio" e do SDL2 KMSDRM.

O Kconfig do Buildroot descarta sem aviso um símbolo cuja dependência não está
satisfeita. O caso real deste projeto (AGENTS.md §3.56): `SDL2_KMSDRM=y` sem
`SDL2_OPENGLES=y` → SDL2 compilado **sem nenhum driver de vídeo**. O aparelho
tinha `/dev/dri/card0`, o DSI bindado e a tela acesa, e o Launcher caía para o
backend nulo dizendo apenas "SDL2 indisponivel".

Esta suíte cobra duas coisas:
  1. o defconfig da X55 pede `SDL2_OPENGLES` **e** `SDL2_KMSDRM` (a ordem não
     importa no Kconfig, mas os dois são obrigatórios);
  2. `tools/check-config-applied.py` existe, é executado pelo build e sabe
     detectar o caso (testado com um par defconfig/.config sintético).
"""
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIGS = os.path.join(ROOT, "buildroot", "configs")
TEMPLATES = [os.path.join(ROOT, "buildroot", "board", "giochi", "defconfig",
                          "base-rk3566.defconfig"),
             os.path.join(ROOT, "buildroot", "board", "giochi", "defconfig",
                          "base-rk3326.defconfig")]
CHECKER = os.path.join(ROOT, "tools", "check-config-applied.py")
BUILD_IMAGE = os.path.join(ROOT, "tools", "build-image.sh")


def read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


class SdlKmsdrmPedido(unittest.TestCase):
    def test_templates_pedem_opengles_e_kmsdrm(self):
        for caminho in TEMPLATES:
            body = read(caminho)
            with self.subTest(template=os.path.basename(caminho)):
                self.assertIn("BR2_PACKAGE_SDL2_OPENGLES=y", body,
                              "sem SDL2_OPENGLES o Kconfig descarta KMSDRM")
                self.assertIn("BR2_PACKAGE_SDL2_KMSDRM=y", body,
                              "sem KMSDRM o SDL2 nao tem driver de video DRM")

    def test_defconfigs_gerados_tambem(self):
        for nome in ("giochi_x55_defconfig", "giochi_rk3326_defconfig"):
            caminho = os.path.join(CONFIGS, nome)
            if not os.path.isfile(caminho):
                self.skipTest("defconfig nao gerado")
            body = read(caminho)
            with self.subTest(defconfig=nome):
                self.assertIn("BR2_PACKAGE_SDL2_OPENGLES=y", body,
                              "rode 'make defconfigs'")

    def test_kmod_tools_nao_e_descartado(self):
        """KMOD_TOOLS depende de BUSYBOX_SHOW_OTHERS.

        Sem ele sobra o modprobe do BusyBox. Funciona, mas o modprobe do kmod
        resolve modules.dep.bin/softdeps/blacklist — e carregar módulo virou
        caminho crítico de boot com o coldplug (S05modules).
        """
        for caminho in TEMPLATES:
            body = read(caminho)
            if "BR2_PACKAGE_KMOD_TOOLS=y" not in body:
                continue
            with self.subTest(template=os.path.basename(caminho)):
                self.assertIn("BR2_PACKAGE_BUSYBOX_SHOW_OTHERS=y", body,
                              "KMOD_TOOLS seria descartado pelo Kconfig")


class FerramentaDeConferencia(unittest.TestCase):
    def test_build_executa_o_checker(self):
        body = read(BUILD_IMAGE)
        self.assertIn("check-config-applied.py", body,
                      "build-image.sh nao valida os simbolos aplicados")

    def test_detecta_simbolo_descartado(self):
        if not os.path.isfile(CHECKER):
            self.fail("tools/check-config-applied.py ausente")
        tmp = tempfile.mkdtemp(prefix="giochi-cfg-")
        try:
            pedido = os.path.join(tmp, "defconfig")
            obtido = os.path.join(tmp, ".config")
            with open(pedido, "w", encoding="utf-8") as fh:
                fh.write("BR2_PACKAGE_SDL2=y\n"
                         "BR2_PACKAGE_SDL2_KMSDRM=y\n"
                         "BR2_ENABLE_LTO=n\n")
            # O .config real: KMSDRM descartado, LTO ausente (normal).
            with open(obtido, "w", encoding="utf-8") as fh:
                fh.write("BR2_PACKAGE_SDL2=y\n")

            r = subprocess.run([sys.executable, CHECKER,
                                "--defconfig", pedido,
                                "--dotconfig", obtido],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 1,
                             "o checker nao acusou o SDL2_KMSDRM descartado")
            self.assertIn("BR2_PACKAGE_SDL2_KMSDRM", r.stderr)
            # Pedir '=n' e o símbolo não aparecer é o estado normal.
            self.assertNotIn("BR2_ENABLE_LTO", r.stderr)

            # Agora com o símbolo presente: tem de passar.
            with open(obtido, "a", encoding="utf-8") as fh:
                fh.write("BR2_PACKAGE_SDL2_KMSDRM=y\n")
            r = subprocess.run([sys.executable, CHECKER,
                                "--defconfig", pedido,
                                "--dotconfig", obtido],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
        finally:
            import shutil
            shutil.rmtree(tmp, ignore_errors=True)


class RebuildDePacoteAfetado(unittest.TestCase):
    def test_build_reconstroi_pacote_quando_a_config_muda(self):
        """O Buildroot NAO invalida o pacote quando a config dele muda.

        Medido: `SDL2_OPENGLES=y` entrou, o build rodou inteiro, e o SDL2 da
        imagem continuava sem driver de video (stamps de 6 h antes). Sem esta
        invalidacao, "build verde + imagem velha" e' o estado normal.
        """
        body = read(BUILD_IMAGE)
        self.assertIn("config-delta.py", body,
                      "build-image.sh nao compara o .config com o anterior")
        self.assertIn(".config.aplicado", body,
                      "build-image.sh nao guarda o .config aplicado")

    def test_config_delta_mapeia_simbolo_para_pacote(self):
        sys.path.insert(0, os.path.join(ROOT, "tools"))
        try:
            import importlib.util
            spec = importlib.util.spec_from_file_location(
                "config_delta", os.path.join(ROOT, "tools", "config-delta.py"))
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
        finally:
            sys.path.pop(0)

        casos = {
            "BR2_PACKAGE_SDL2_KMSDRM": "sdl2",
            "BR2_PACKAGE_MESA3D_OPENGL_ES": "mesa3d",
            "BR2_PACKAGE_LIBDRM": "libdrm",
            "BR2_PACKAGE_BUSYBOX": "busybox",
        }
        for simbolo, esperado in casos.items():
            with self.subTest(simbolo=simbolo):
                self.assertEqual(mod.pacote_do_simbolo(simbolo), esperado)

        antigo = {"BR2_PACKAGE_SDL2": "y"}
        novo = {"BR2_PACKAGE_SDL2": "y", "BR2_PACKAGE_SDL2_KMSDRM": "y"}
        pacotes, _ = mod.mapear(antigo, novo)
        self.assertEqual(pacotes, ["sdl2"])


if __name__ == "__main__":
    unittest.main()
