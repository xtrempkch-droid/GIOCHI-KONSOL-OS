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


FRAGMENTOS_KERNEL = [
    os.path.join(ROOT, "buildroot", "board", "giochi", "linux.fragment"),
    os.path.join(ROOT, "buildroot", "board", "giochi",
                 "linux-powkiddy.fragment"),
]


class GadgetLegadoReservaOFunctionFS(unittest.TestCase):
    """`CONFIG_USB_FUNCTIONFS=y` derruba o MTP do configfs (AGENTS.md §3.94).

    O nome engana: `USB_FUNCTIONFS` é o gadget **legado** `g_ffs`, não o
    FunctionFS. No boot ele chama `ffs_single_dev()` e marca o único
    dispositivo FunctionFS do sistema como reservado; a partir daí
    `_ffs_alloc_dev()` devolve `-EBUSY` em qualquer `mkdir functions/ffs.*`.

    Medido na X55 (01/11): `.config` com `USB_F_FS=y` e
    `USB_CONFIGFS_F_FS=y`, e mesmo assim

        giochi-usb: aviso: configfs recusou ffs.mtp (CONFIG_USB_CONFIGFS_F_FS?)

    A mensagem da época apontava para o símbolo errado. O que existe para
    provar é o par: `CONFIG_USB_F_FS=y` (a função) **sem**
    `CONFIG_USB_FUNCTIONFS=y` (o gadget legado).
    """

    LEGADO = "CONFIG_USB_FUNCTIONFS"

    def test_fragmentos_nao_pedem_o_gadget_legado(self):
        for caminho in FRAGMENTOS_KERNEL:
            body = read(caminho)
            with self.subTest(fragmento=os.path.basename(caminho)):
                # Comentário citando o símbolo é o que DOCUMENTA o motivo;
                # o que não pode existir é a linha de configuração.
                for linha in body.splitlines():
                    if self.LEGADO in linha:
                        self.assertTrue(
                            linha.lstrip().startswith("#"),
                            "%s habilita %s=y: o g_ffs reserva o FunctionFS "
                            "e o configfs recusa `mkdir functions/ffs.*`, "
                            "que e' o MTP. Ver AGENTS.md §3.94."
                            % (os.path.basename(caminho), self.LEGADO))

    def test_fragmentos_pedem_a_funcao_e_o_configfs(self):
        for caminho in FRAGMENTOS_KERNEL:
            body = read(caminho)
            with self.subTest(fragmento=os.path.basename(caminho)):
                self.assertIn("CONFIG_USB_F_FS=y", body,
                              "sem a FUNCAO f_fs o MTP nao existe")
                self.assertIn("CONFIG_USB_CONFIGFS_F_FS=y", body,
                              "sem expor f_fs no configfs nao ha "
                              "`mkdir functions/ffs.mtp`")

    def test_fragmentos_nao_pedem_simbolo_de_mtp_que_nao_existe(self):
        """`USB_CONFIGFS_F_MTP`/`USB_F_MTP` são do BSP, não do mainline.

        O kconfig do kernel descarta os dois em silêncio (mesma classe do
        §3.2, agora no kernel). Pedir "MTP no kernel" dava a impressão de que
        o MTP estava ligado; quem responde MTP é o `umtprd` sobre `f_fs`.
        """
        inexistentes = ("CONFIG_USB_CONFIGFS_F_MTP", "CONFIG_USB_F_MTP")
        for caminho in FRAGMENTOS_KERNEL:
            body = read(caminho)
            for linha in body.splitlines():
                for simbolo in inexistentes:
                    if simbolo in linha:
                        self.assertTrue(
                            linha.lstrip().startswith("#"),
                            "%s pede %s, que nao existe no mainline: o "
                            "kconfig descarta calado. O MTP e' o umtprd "
                            "sobre CONFIG_USB_F_FS."
                            % (os.path.basename(caminho), simbolo))

    def test_kernel_construido_nao_tem_o_legado(self):
        """.config real do build, quando existir: mede o passado, não a intenção.

        Árvore de build mais velha que o fragmento não é evidência de erro de
        configuração — é build pendente. Comparar as datas separa as duas
        coisas; sem isso a suíte fica vermelha para quem só ainda não
        recompilou, e um teste que fica vermelho por motivo errado ensina a
        ignorá-lo.
        """
        import glob

        padrao = os.path.join(ROOT, "dist", "*", "build", "build",
                              "linux-*", ".config")
        configs = glob.glob(padrao)
        if not configs:
            self.skipTest("sem arvore de kernel construida")

        mais_novo_fragmento = max(os.path.getmtime(f)
                                  for f in FRAGMENTOS_KERNEL)
        for cfg in configs:
            if os.path.getmtime(cfg) < mais_novo_fragmento:
                continue
            with open(cfg, encoding="utf-8", errors="replace") as fh:
                linhas = fh.read().splitlines()
            with self.subTest(config=cfg):
                self.assertIn("CONFIG_USB_F_FS=y", linhas)
                self.assertIn("CONFIG_USB_CONFIGFS_F_FS=y", linhas)
                # `assertIn(x, linhas)` despeja o .config inteiro (~650 KB);
                # o booleano mantem a mensagem curta e util.
                self.assertFalse(
                    "CONFIG_USB_FUNCTIONFS=y" in linhas,
                    "o kernel desta arvore reserva o FunctionFS com o g_ffs "
                    "legado: o MTP do configfs vai falhar com -EBUSY (%s). "
                    "Rebuild: rm build/linux-*/.stamp_*" % cfg)


class FallbackDeMtpNaoDesmontaOUser(unittest.TestCase):
    """MTP quebrado não pode virar `mass` automaticamente.

    O modo `mass` desmonta `/user` (necessário: dois donos do mesmo ext4
    corrompem) e derruba junto as chaves do dropbear (`/etc/dropbear` é
    symlink para `/user/.ssh`), a biblioteca e a música. Medido na X55:
    com a queda automática o boot terminava em

        S62sshd: aviso: falha ao gerar chave ed25519
        S62sshd: aviso: falha ao gerar chave rsa
        S62sshd: Modo Desenvolvedor ativo, mas sem chave e sem senha

    mesmo com o Modo Desenvolvedor ligado e o `usb0` com IP. Mass Storage é
    escolha do usuário, não recuperação de MTP.
    """

    HELPER = os.path.join(ROOT, "scripts", "giochi-usb-mode")

    def test_helper_nao_troca_modo_por_falha_do_ffs(self):
        body = read(self.HELPER)
        # Os dois caminhos de falha do ffs não podem mais escrever MODE=mass.
        trecho = body[body.index("MTP_OK=0"):]
        trecho = trecho[:trecho.index("if [ \"$MODE\" = \"mass\" ]")]
        self.assertNotIn("MODE=mass", trecho,
                         "o helper ainda cai para mass quando o ffs falha: "
                         "isso desmonta /user, derruba o dropbear e a "
                         "biblioteca. AGENTS.md §3.95.")

    def test_helper_sobe_o_mtp_antes_de_anexar_o_udc(self):
        """FunctionFS sem descritores devolve -19 no bind (AGENTS.md §3.96).

        `ffs_func_bind` → `ffs_do_functionfs_bind()`:

            ret = ffs_opts->dev->desc_ready ? 0 : -ENODEV;

        e `desc_ready` só fica verdadeiro depois que o daemon do userspace
        escreve os descritores no ep0. Subir o umtprd DEPOIS do `echo > UDC`
        dá exatamente:

            udc fcc00000.usb: failed to start giochi: -19
            UDC core: giochi: couldn't find an available UDC or it's busy

        A segunda linha é enganosa: o UDC estava livre.
        """
        body = read(self.HELPER)
        # `echo "" > UDC` no começo é a parada limpa, não o bind.
        self.assertLess(body.index('"$MTPSVC" start'),
                        body.index('echo "$udc" > UDC'),
                        "o umtprd tem de subir ANTES do bind no UDC: o "
                        "FunctionFS recusa bind sem descritores (-19).")

    def test_falha_do_ffs_diz_o_motivo_e_mantem_a_rede(self):
        import subprocess

        with tempfile.TemporaryDirectory() as tmp:
            gadget = os.path.join(tmp, "gadget")
            os.makedirs(os.path.join(gadget, "configs", "c.1"), exist_ok=True)
            os.makedirs(os.path.join(gadget, "functions"), exist_ok=True)
            # configfs falso que RECUSA o mkdir de ffs.*: um ARQUIVO com o
            # nome do diretorio faz `mkdir -p` falhar com EEXIST/ENOTDIR, que
            # e' o efeito visivel do -EBUSY do _ffs_alloc_dev quando o g_ffs
            # reservou o dispositivo.
            with open(os.path.join(gadget, "functions", "ffs.mtp"), "w",
                      encoding="utf-8") as fh:
                fh.write("reservado pelo g_ffs\n")
            # configfs falso que RECUSA qualquer mkdir de ffs.*: é o efeito
            # do -EBUSY do _ffs_alloc_dev quando o g_ffs reservou o device.
            binfalso = os.path.join(tmp, "bin")
            os.makedirs(binfalso, exist_ok=True)
            logger = os.path.join(binfalso, "logger")
            with open(logger, "w", encoding="utf-8") as fh:
                fh.write("#!/bin/sh\nshift 2\necho \"$*\"\n")
            os.chmod(logger, 0o755)

            env = dict(os.environ,
                       PATH=binfalso + os.pathsep + os.environ.get("PATH", ""),
                       GIOCHI_DRY_RUN="1", GADGET=gadget,
                       GIOCHI_LIB=os.path.join(tmp, "lib-inexistente.sh"))
            proc = subprocess.run(["sh", self.HELPER, "mtp"], env=env,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, timeout=60)
            saida = (proc.stdout + proc.stderr).decode("utf-8", "replace")
            self.assertIn("segue so' com a rede", saida,
                          "a falha do ffs tem de dizer que segue so' com a "
                          "rede: %r" % saida)
            self.assertNotIn("usando Mass Storage", saida,
                             "nao pode anunciar queda para Mass Storage: %r"
                             % saida)


if __name__ == "__main__":
    unittest.main()
