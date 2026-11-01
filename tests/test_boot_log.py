#!/usr/bin/env python3
"""Guarda do log de boot e do caminho de boot do cartão.

Cada teste aqui corresponde a um defeito REAL que deixaria a tela preta sem
nenhuma pista — ou que faria o log mentir. O objetivo é que a próxima pessoa
não precise redescobrir isso lendo log de serial.
"""
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOARD = os.path.join(ROOT, "buildroot", "board", "giochi")
INITD = os.path.join(ROOT, "scripts", "init.d")
OVERLAY = os.path.join(BOARD, "rootfs_overlay", "etc")


def read(*parts):
    with open(os.path.join(*parts), encoding="utf-8") as fh:
        return fh.read()


def sh_code(text):
    """Remove comentários de um script sh (linhas iniciadas por #)."""
    out = []
    for line in text.splitlines():
        if line.lstrip().startswith("#"):
            continue
        out.append(line.split("#", 1)[0] if " #" in line else line)
    return "\n".join(out)


class BootLogTest(unittest.TestCase):
    def setUp(self):
        self.s00 = read(INITD, "S00bootlog")

    def test_s00_existe_e_e_executavel(self):
        path = os.path.join(INITD, "S00bootlog")
        self.assertTrue(os.path.isfile(path))
        self.assertTrue(os.access(path, os.X_OK), "S00bootlog sem +x")

    def test_s00_e_posix(self):
        # Mesmo critério dos outros scripts: sh puro, sem bashismo.
        for bad in ("[[", "$'", "function ", "local ", "echo -e"):
            self.assertNotIn(bad, sh_code(self.s00),
                             "S00bootlog usa bashismo: %r" % bad)

    def test_s00_nao_escreve_em_etc(self):
        # A raiz é SquashFS somente leitura (AGENTS §3.6).
        for m in re.finditer(r">>?\s*([^\s;|&]+)", self.s00):
            destino = m.group(1).strip('"')
            self.assertFalse(destino.startswith("/etc"),
                             "S00bootlog escreve em /etc: %s" % destino)

    def test_s00_roda_antes_dos_outros(self):
        # O nome tem de ordenar antes de S05update (rcS percorre em ordem).
        nomes = sorted(n for n in os.listdir(INITD) if re.match(r"^S\d", n))
        self.assertEqual(nomes[0], "S00bootlog")

    def test_s00_tem_fallback_de_destino(self):
        # Se o cartão não for gravável o boot NÃO pode parar.
        for marcador in ("/user", "/run/giochi"):
            self.assertIn(marcador, self.s00,
                          "S00bootlog sem fallback para %s" % marcador)
        self.assertIn("exit 0", self.s00)

    def test_s00_publica_o_caminho_do_log(self):
        # rcS e lib.sh dependem deste contrato.
        self.assertIn("/run/giochi/bootlog.path", self.s00)
        self.assertIn("/run/giochi/bootlog.path", read(OVERLAY, "init.d", "rcS"))
        self.assertIn("giochi_bootlog_path", read(OVERLAY, "giochi", "lib.sh"))

    def test_rcc_duplica_a_saida_dos_servicos(self):
        rcs = read(OVERLAY, "init.d", "rcS")
        self.assertIn("tee -a", rcs,
                      "rcS não duplica a saída; o log ficaria sem os serviços")
        self.assertIn("S00bootlog", rcs)
        # O S00 tem de rodar ANTES do laço (ele cria o destino do tee).
        antes = rcs.split("run_scripts", 1)[0]
        self.assertIn("S00bootlog", antes)


class BootDeviceTest(unittest.TestCase):
    """O cartão SD é mmc1 nestes aparelhos (mmc0 é o eMMC)."""

    def setUp(self):
        self.uboot = read(BOARD, "uboot.fragment")

    def test_bootcommand_nao_fixa_mmc0(self):
        m = re.search(r'^CONFIG_BOOTCOMMAND="([^"]*)"', self.uboot, re.M)
        self.assertIsNotNone(m, "uboot.fragment sem CONFIG_BOOTCOMMAND")
        cmd = m.group(1)
        # Um 'load mmc 0:...' literal é o bug: a X55 não tem eMMC soldado,
        # o load falha, o boot.scr nunca roda e a tela fica preta.
        self.assertNotRegex(cmd, r"load\s+mmc\s+0:",
                            "BOOTCOMMAND carrega de mmc 0 (eMMC); use 1 (SD)")
        self.assertIn("mmc ${d}:1", cmd,
                      "BOOTCOMMAND deve sondar o dispositivo (mmc 1 = cartão)")
        self.assertIn("for d in 1 0", cmd)

    def test_env_fica_no_cartao(self):
        m = re.search(r'^CONFIG_ENV_FAT_DEVICE_AND_PART="([^"]*)"',
                      self.uboot, re.M)
        self.assertIsNotNone(m)
        self.assertEqual(m.group(1).split(":")[0], "1",
                         "uboot.env tem de ficar no cartão (mmc 1); "
                         "em mmc 0 o saveenv falha e o rollback A/B não grava")
        self.assertIn("CONFIG_SYS_MMC_ENV_DEV=1", self.uboot)

    def test_boot_cmd_assume_o_cartao(self):
        boot = read(BOARD, "boot.cmd")
        self.assertIn("setenv giochi_mmc 1", boot)


class RootIdentityTest(unittest.TestCase):
    """A raiz tem de ser achada por PARTUUID, nao por nome de dispositivo."""

    def setUp(self):
        self.gen = read(BOARD, "genimage.cfg")

    def test_disk_signature_e_fixa(self):
        # Sem disk-signature o genimage SORTEIA a assinatura do MBR a cada
        # build, e o PARTUUID do root= deixaria de bater.
        self.assertIn("disk-signature", self.gen,
                      "genimage.cfg sem disk-signature: o PARTUUID muda a "
                      "cada build e o root= nao resolve")
        self.assertIn("0x47494f43", self.gen)
        # O cfg sem U-Boot tambem (aparelhos X35S/X35H).
        self.assertIn("disk-signature",
                      read(BOARD, "genimage-no-uboot.cfg"))

    def test_boot_cmd_usa_o_mesmo_partuuid(self):
        boot = sh_code(read(BOARD, "boot.cmd"))
        self.assertIn("PARTUUID=47494f43-02", boot)
        self.assertIn("PARTUUID=47494f43-03", boot)
        # /dev/mmcblk<N>p2 so vale para o cartao: a mesma imagem no eMMC vira
        # mmcblk0p2 e no segundo slot, mmcblk2p2.
        self.assertNotIn("/dev/mmcblk${giochi_mmc}p2", boot)

    def test_extlinux_usa_o_mesmo_partuuid(self):
        post = sh_code(read(BOARD, "post-image.sh"))
        self.assertIn("47494f43", post)
        self.assertNotIn("%PARTUUID%", post)

    def test_instalador_de_emmc_esta_na_imagem(self):
        # Roda no console: e o caminho quando o bootloader de fabrica ganha
        # do cartao.
        pb = read(BOARD, "post-build.sh")
        self.assertIn("install-image-to-emmc", pb)
        self.assertTrue(os.path.isfile(
            os.path.join(ROOT, "tools", "install-image-to-emmc.sh")))

    def test_instalador_nao_aceita_dispositivo_errado(self):
        # Um dd no cartao do sistema destroi a instalacao em uso.
        tool = read(ROOT, "tools", "install-image-to-emmc.sh")
        self.assertIn("fe310000.mmc", tool,
                      "o instalador tem de exigir o eMMC (fe310000.mmc)")
        self.assertIn("APAGAR", tool, "falta confirmacao destrutiva")
        # A guarda de "nao estou gravando no disco em uso" tem de estar no
        # codigo, nao so na explicacao do cabecalho.
        codigo = sh_code(tool)
        # O padrao do case, nao a mencao na mensagem de erro: um assertIn
        # simples passava mesmo com o case trocado por '*)" (testado assim).
        self.assertRegex(codigo, r"\*fe310000\.mmc\*\)",
                         "o instalador nao comparou o sysfs com fe310000.mmc")
        self.assertIn("/proc/mounts", codigo,
                      "o instalador nao confere se a raiz esta no disco alvo")


class SerialToolTest(unittest.TestCase):
    def test_serial_capture_e_ferramenta_de_pc(self):
        # Le o pino de UART pelo PC; nao vai ao rootfs.
        caminho = os.path.join(ROOT, "tools", "serial-capture.py")
        self.assertTrue(os.path.isfile(caminho))
        guarda = read(ROOT, "tools", "check-image-contents.sh")
        self.assertIn("serial-capture.py", guarda,
                      "serial-capture.py nao esta na lista de ferramenta de PC")


class BusyboxTest(unittest.TestCase):
    """Sem estes applets/opções o log some em silêncio."""

    def setUp(self):
        self.frag = read(BOARD, "busybox.fragment")

    def test_applets_do_log(self):
        for sym in ("CONFIG_SYSLOGD=y", "CONFIG_KLOGD=y", "CONFIG_TEE=y",
                    "CONFIG_LOGGER=y"):
            self.assertIn(sym, self.frag, "busybox.fragment sem %s" % sym)

    def test_label_no_fstab(self):
        # fstab usa LABEL=GIOCHI_USER: sem FEATURE_MOUNT_LABEL o busybox trata
        # a string como nome de arquivo, o mount -a falha calado e /user (a
        # única partição gravável) nunca é montada.
        self.assertIn("CONFIG_FEATURE_MOUNT_LABEL=y", self.frag)
        self.assertIn("LABEL=GIOCHI_USER", read(OVERLAY, "fstab"))


class ImagePiecesTest(unittest.TestCase):
    def test_boot_dir_existe_na_imagem(self):
        # A raiz é somente leitura: não dá para mkdir /boot em runtime.
        tabela = read(BOARD, "device_table.txt")
        self.assertRegex(tabela, r"(?m)^/boot\td\t")

    def test_post_build_instala_s00(self):
        pb = read(BOARD, "post-build.sh")
        self.assertIn("S00bootlog", pb)
        # Todo script de init tem de estar na lista obrigatória (um esquecido
        # aqui é uma imagem que não inicia).
        for nome in os.listdir(INITD):
            if not re.match(r"^S\d\d", nome):
                continue
            self.assertIn(nome, pb,
                          "scripts/init.d/%s não está na lista do post-build.sh"
                          % nome)

    def test_fragmento_de_debug_existe(self):
        frag = read(BOARD, "linux-debug.fragment")
        for sym in ("CONFIG_FRAMEBUFFER_CONSOLE=y", "CONFIG_FB=y",
                    "CONFIG_DRM_FBDEV_EMULATION=y"):
            self.assertIn(sym, frag)
        # O padrão continua sem fbcon (linux.fragment declara CONFIG_FB=n).
        self.assertIn("CONFIG_FB=n", read(BOARD, "linux.fragment"))

    def test_boot_scr_e_regerado(self):
        # O Buildroot gera boot.scr dentro do host-uboot-tools, que so e
        # reconstruido quando ELE muda: editar boot.cmd nao invalidava nada e a
        # imagem saia com o boot.scr antigo (medido: 4 dias de diferenca).
        post = read(BOARD, "post-image.sh")
        self.assertIn("boot.cmd", post)
        self.assertIn("mkimage", post,
                      "post-image.sh tem de regerar o boot.scr; sem isso "
                      "mudanca no boot.cmd nao chega na imagem")
        self.assertIn("boot.scr", post)

    def test_dtb_cai_para_board_dtb_quando_o_load_falha(self):
        # BUG REAL (medido no X35H): o fallback era gated em "fdtfile vazio".
        # fdtfile vem do ambiente do U-Boot e quase nunca esta vazio; como o
        # cartao so tem o DTB do aparelho para o qual a imagem foi gerada,
        # carregar dtbs/${fdtfile} falhava, o fallback nao acontecia, fdt_addr_r
        # ficava com lixo e o booti falhava — e o U-Boot bootava o eMMC (o
        # sistema de fabrica). O usuario ve "nao apareceu nada nosso".
        boot = sh_code(read(BOARD, "boot.cmd"))
        self.assertNotIn('if test -z "${fdtfile}"', boot,
                         "o fallback para board.dtb nao pode depender de "
                         "fdtfile estar vazio: ele tem de reagir a FALHA do load")
        self.assertIn("giochi_fdt_ok", boot)
        self.assertIn("board.dtb", boot)

    def test_saveenv_nao_grava_no_emmc_do_fabricante(self):
        # Nosso boot.scr tambem roda sob o U-Boot de FABRICA (que varre o cartao
        # atras de boot.scr). Ali 'saveenv' grava no eMMC do usuario -- efeito
        # colateral inaceitavel em aparelho de terceiro. O discriminador e
        # 'giochi_mmc': o NOSSO BOOTCOMMAND o define antes do 'source'.
        boot = sh_code(read(BOARD, "boot.cmd"))
        self.assertIn("giochi_env_gravavel", boot)
        linhas = boot.splitlines()
        for i, linha in enumerate(linhas):
            if linha.strip() != "saveenv":
                continue
            contexto = "\n".join(linhas[max(0, i - 4):i])
            self.assertIn("giochi_env_gravavel", contexto,
                          "saveenv sem guarda de 'giochi_env_gravavel' na "
                          "linha %d: grava no eMMC do aparelho de fabrica" % (i + 1))

    def test_extlinux_e_regerado_sempre(self):
        # Artefato derivado gravado em BINARIES_DIR sobrevive ao build
        # incremental: com "if [ ! -f ]", mudanca de root= nao chegava na
        # imagem (medido: extlinux.conf com /dev/mmcblk1p2 depois de o boot.cmd
        # ja usar PARTUUID). Mesma armadilha do boot.scr.
        post = sh_code(read(BOARD, "post-image.sh"))
        self.assertNotIn('if [ ! -f "$BINARIES_DIR/extlinux.conf" ]', post,
                         "extlinux.conf nao pode ser gerado so se faltar")
        self.assertIn("> \"$BINARIES_DIR/extlinux.conf\"", post)

    def test_board_dtb_e_regerado_sempre(self):
        """Mesma armadilha do extlinux.conf e do boot.scr (§3.39).

        Medido nesta sessão: o DTB do kernel foi recompilado com o nó do TRNG
        ligado (59416 B, status = "okay") e o `board.dtb` que foi para o cartão
        continuou o antigo (59420 B, status = "disabled"): a correção não
        chegava ao aparelho e o boot seguia lento por falta de entropia sem
        nenhum aviso. Com `if [ ! -f ]`, o artefato da execução anterior
        sobrevive ao build incremental.
        """
        post = sh_code(read(BOARD, "post-image.sh"))

        self.assertNotIn('if [ ! -f "$BINARIES_DIR/board.dtb" ]', post,
                         "board.dtb nao pode ser copiado so se faltar")
        self.assertIn('cp -f "$BINARIES_DIR/dtbs/$DTB_FILE" '
                      '"$BINARIES_DIR/board.dtb"', post)
        # A normalizacao para dtbs/ tinha o MESMO problema (terceira ocorrencia
        # desta armadilha no mesmo arquivo): o DTB velho de dtbs/ sobrevivia e
        # o novo nunca era copiado.
        self.assertNotIn('if [ ! -f "$BINARIES_DIR/dtbs/$DTB_FILE" ] &&', post,
                         "dtbs/ nao pode ser populado so se faltar")
        self.assertIn('cp -f "$BINARIES_DIR/$DTB_FILE" '
                      '"$BINARIES_DIR/dtbs/$DTB_FILE"', post)

    def test_mkbootimg_copia_o_subdiretorio_rockchip(self):
        # O glob "dtbs/*.dtb" nao entra em subdiretorio: sem mmd/mcopy
        # explicitos, dtbs/rockchip/ nao existe no FAT (verificado no .img).
        mk = sh_code(read(BOARD, "mkbootimg.sh"))
        self.assertIn("::dtbs/rockchip", mk)

    def test_dtb_em_subdiretorio_rockchip(self):
        # MEDIDO no console serial do X35H: o U-Boot exporta fdtfile como
        # "rockchip/rk3566-powkiddy-x55.dtb" (organizacao do kernel upstream),
        # mas o cartao so tinha o nome plano -> "Failed to load
        # 'dtbs/rockchip/rk...'". O post-image passa a gravar as duas formas.
        post = read(BOARD, "post-image.sh")
        self.assertIn("dtbs/rockchip", post,
                      "post-image.sh nao grava dtbs/rockchip/: o cargo "
                      "dtbs/${fdtfile} do boot.cmd nao encontra o arquivo")
        boot = sh_code(read(BOARD, "boot.cmd"))
        self.assertIn("dtbs/board.dtb", boot,
                      "falta o fallback por dtbs/board.dtb")

    def test_boot_cmd_tem_marcadores_de_serial(self):
        # Sem fbcon (imagem normal) o serial e a unica visao antes do kernel.
        boot = read(BOARD, "boot.cmd")
        for marcador in ("GIOCHI: boot.scr em execucao",
                         "GIOCHI: Image carregada",
                         "GIOCHI: booti"):
            self.assertIn(marcador, boot)

    def test_scripts_de_pacote_conflitantes_sao_removidos(self):
        # S01syslogd/S02klogd roubam /dev/log (log do cartao para de crescer);
        # S50dropbear sobe SSH em todas as interfaces e anula o adendo 4.6.1.
        # Sem comentarios: o nome aparece na explicacao do bloco e um
        # assertIn no arquivo inteiro passaria mesmo com o 'rm' removido
        # (testado ao contrario — foi assim que esta guarda falhou antes).
        pb = sh_code(read(BOARD, "post-build.sh"))
        for nome in ("S01syslogd", "S02klogd", "S50dropbear"):
            self.assertIn(nome, pb,
                          "post-build.sh nao remove /etc/init.d/%s" % nome)
        self.assertIn("rm -f", pb)

    def test_build_image_tem_modo_debug(self):
        bi = read(ROOT, "tools", "build-image.sh")
        self.assertIn("--debug", bi)
        self.assertIn("linux-debug.fragment", bi)
        # Saída separada: evita gravar o cartão com a imagem de diagnóstico.
        self.assertIn("-debug", bi)



class FragmentoDoBusyBoxSoPedeSimboloQueExiste(unittest.TestCase):
    """Símbolo inventado no fragmento é descartado em silêncio (§3.2, §3.103).

    Aconteceu duas vezes nesta sessão: `CONFIG_NC` sem estar no fragmento
    (default desligado) e `CONFIG_NC_UNIX`, que **não existe** no BusyBox
    1.36.1 (zero `AF_UNIX` em `networking/nc.c`). O kconfig aceita e ignora,
    e o defeito só aparece no aparelho como "sh: nc: not found".

    Percorre o fragmento e cobra que todo `CONFIG_*` pedido exista no fonte
    do BusyBox baixado (quando houver árvore de build) ou na lista de
    conhecidos.
    """

    FRAG = os.path.join(BOARD, "busybox.fragment")

    def simbolos_pedidos(self) -> set[str]:
        import re

        with open(self.FRAG, encoding="utf-8") as fh:
            corpo = fh.read()
        return {m.group(1) for m in re.finditer(r"^(CONFIG_[A-Z0-9_]+)=",
                                                corpo, re.M)}

    def test_nc_esta_ligado(self):
        """`nc` sem `CONFIG_NC` fica desligado: o default do BusyBox é n."""
        self.assertIn("CONFIG_NC", self.simbolos_pedidos(),
                      "sem CONFIG_NC=y o applet nc nao existe na imagem")

    def test_nc_unix_nao_existe_e_nao_pode_ser_pedido(self):
        """`-U` (soquete Unix) não existe no nc do BusyBox, em config nenhuma.

        Quem precisa falar com `/run/giochi/control.sock` é o binário
        `giochi-ctl` do console (src/tools), não o nc.
        """
        self.assertNotIn("CONFIG_NC_UNIX", self.simbolos_pedidos(),
                         "CONFIG_NC_UNIX nao existe no BusyBox 1.36.1: "
                         "pedi-lo nao liga nada e esconde a falta do cliente "
                         "de verdade (src/tools/giochi-ctl.c)")

    def test_todo_simbolo_existe_no_fonte_baixado(self):
        import glob

        fontes = glob.glob(os.path.join(ROOT, "dist", "*", "build", "build",
                                        "busybox-*"))
        fontes = [f for f in fontes if os.path.isdir(f)]
        if not fontes:
            self.skipTest("sem arvore do busybox construida")
        pedidos = self.simbolos_pedidos()
        for arvore in fontes:
            # A definicao de cada simbolo esta em algum Kconfig/Config.in.
            texto = ""
            for raiz, _, arquivos in os.walk(arvore):
                for nome in arquivos:
                    if nome in ("Kconfig", "Config.in"):
                        try:
                            with open(os.path.join(raiz, nome),
                                      encoding="utf-8", errors="replace") as fh:
                                texto += fh.read()
                        except OSError:
                            pass
            inexistentes = sorted(s for s in pedidos
                                  if ("config " + s[7:]) not in texto)
            self.assertEqual(
                [], inexistentes,
                "o fragmento pede simbolo que o BusyBox de %s nao define "
                "(descartado em silencio)" % os.path.basename(arvore))


if __name__ == "__main__":
    unittest.main()
