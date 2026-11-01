#!/usr/bin/env python3
"""Guarda dos patches locais de kernel.

Um patch local que não é aplicado é o pior tipo de defeito deste projeto: o
build termina com sucesso, a imagem é gerada, o cartão é gravado e o defeito
volta no aparelho — sem nenhuma mensagem, porque o patch simplesmente não
entrou. Foi assim que a X55 ficou com a tela apagada (`-22` no bind do DSI,
AGENTS.md §3.48).

O que esta suíte cobra:

  1. Todo `.patch` em `buildroot/board/giochi/patches/linux/` é um diff
     unificado bem formado, com caminhos `a/` e `b/` (é o que o
     `apply-patches.sh` do Buildroot sabe aplicar com `-p1`).
  2. Os dois templates de defconfig apontam `BR2_LINUX_KERNEL_PATCH` para
     **esse** patch, e o caminho existe.
  3. Os defconfigs GERADOS carregam a mesma linha (é o que o build usa de
     verdade; editar o template e esquecer `make defconfigs` passaria calado).
  4. O patch ainda aplica na árvore de kernel que existir em `dist/`, quando
     houver alguma. Sem árvore o teste se auto-pula: a checagem de conteúdo
     continua valendo, a de aplicação não.
"""
import glob
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOARD = os.path.join(ROOT, "buildroot", "board", "giochi")
PATCH_DIR = os.path.join(BOARD, "patches", "linux")
CONFIGS = os.path.join(ROOT, "buildroot", "configs")
TEMPLATES = (
    os.path.join(BOARD, "defconfig", "base-rk3326.defconfig"),
    os.path.join(BOARD, "defconfig", "base-rk3566.defconfig"),
)

# O patch que fecha o §3.48. Se ele for renomeado, esta lista muda junto — não
# é para "consertar o teste" apagando o nome.
PATCH_DSI = "0001-drm-rockchip-dw-mipi-dsi-adia-component-add.patch"


def read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def patch_files():
    return sorted(glob.glob(os.path.join(PATCH_DIR, "*.patch")))


def kernel_trees():
    """Árvores de kernel já extraídas por algum build local (pode não haver).

    O glob tem de excluir `linux-firmware-*`, `linux-headers-*` e afins, que
    também começam por "linux-" em build/build/ e não têm os arquivos do kernel.
    """
    arvores = []
    for caminho in sorted(glob.glob(os.path.join(ROOT, "dist", "*", "build",
                                                 "build", "linux-*"))):
        nome = os.path.basename(caminho)
        if nome.startswith(("linux-firmware", "linux-headers")):
            continue
        if not os.path.isdir(os.path.join(caminho, "drivers")):
            continue
        arvores.append(caminho)
    return arvores


class FormatoDoPatch(unittest.TestCase):
    def test_existe_pelo_menos_um_patch(self):
        self.assertTrue(patch_files(),
                        "nenhum patch em %s; o bind do DSI volta a falhar"
                        % PATCH_DIR)

    def test_patch_do_dsi_existe(self):
        caminho = os.path.join(PATCH_DIR, PATCH_DSI)
        self.assertTrue(os.path.isfile(caminho), "patch do DSI ausente: %s"
                        % caminho)

    def test_diffs_sao_unificados_com_prefixo_a_b(self):
        for caminho in patch_files():
            texto = read(caminho)
            nome = os.path.basename(caminho)
            with self.subTest(patch=nome):
                self.assertIn("--- a/", texto, "%s sem '--- a/'" % nome)
                self.assertIn("+++ b/", texto, "%s sem '+++ b/'" % nome)
                self.assertIn("\n@@", texto, "%s sem hunk '@@'" % nome)

    def test_sem_crlf_e_com_newline_final(self):
        for caminho in patch_files():
            with open(caminho, "rb") as fh:
                dados = fh.read()
            nome = os.path.basename(caminho)
            with self.subTest(patch=nome):
                # O 'patch' aplica CRLF, mas o resultado fica com \r no fonte e
                # o erro aparece só na compilação.
                self.assertNotIn(b"\r\n", dados, "%s tem CRLF" % nome)
                self.assertTrue(dados.endswith(b"\n"),
                                "%s não termina em newline" % nome)

    def test_patch_do_dsi_mantem_a_correcao(self):
        texto = read(os.path.join(PATCH_DIR, PATCH_DSI))
        # Os três pedaços que fazem a correção funcionar: o campo de estado, a
        # guarda no host_attach e o registro no fim do probe.
        self.assertIn("component_pending", texto,
                      "o patch perdeu o estado de componente pendente")
        self.assertIn("dw_mipi_dsi_rockchip_component_add", texto,
                      "o patch perdeu o helper de registro do componente")
        self.assertIn("if (!dsi->dmd)", texto,
                      "o patch perdeu a guarda de dmd nulo")

    def test_patch_do_trng_liga_o_no_do_rk3566(self):
        """Sem o TRNG o crng demora 11 s (ou 1024 s) e getrandom() bloqueia.

        Medido na X55: 8,8 s de silêncio entre "buttons.map carregada" e o
        primeiro MESA-LOADER, terminando exatamente em "crng init done". O SDL
        com KMSDRM chega ao EGL, o EGL pede entropia, e só há entropia quando o
        crng fica pronto.
        """
        nome = ("0002-arm64-dts-rockchip-liga-o-trng-do-rk3566-na-x55"
                ".patch")
        caminho = os.path.join(PATCH_DIR, nome)
        self.assertTrue(os.path.isfile(caminho), "patch do TRNG ausente: %s"
                        % caminho)
        texto = read(caminho)
        self.assertIn("rk3566-powkiddy-x55.dts", texto)
        self.assertIn("&rng", texto)
        self.assertIn('status = "okay"', texto)
        # O numero do patch importa: precisa vir depois do 0001 e o nome tem de
        # ordenar (o Buildroot aplica em ordem de nome).
        self.assertTrue(nome.startswith("0002-"),
                        "o patch do TRNG tem de ordenar depois do 0001")

    def test_patch_do_usb_poe_a_porta_em_modo_dispositivo(self):
        """OTG sem extcon = nenhum UDC = nenhum gadget (AGENTS.md §3.88).

        O `usb_host0_xhci` do rk356x.dtsi vem `dr_mode = "otg"`, e nem o
        dtsi nem o DTS da X55 declaram `extcon` ou `usb-role-switch`. Sem
        isso o dwc3 fica na maquina OTG esperando um evento do pino ID que
        esta placa nao tem, `/sys/class/udc` fica vazio, e o
        `giochi-usb-mode` sai com "nenhum UDC disponivel" — sem MTP, sem
        rede USB e sem SSH, com o PC nao vendo dispositivo nenhum.
        """
        nome = ("0003-arm64-dts-rockchip-x55-usb-c-em-modo-dispositivo"
                ".patch")
        caminho = os.path.join(PATCH_DIR, nome)
        self.assertTrue(os.path.isfile(caminho),
                        "patch do USB-C ausente: %s" % caminho)
        texto = read(caminho)
        self.assertIn("rk3566-powkiddy-x55.dts", texto)
        self.assertIn("&usb_host0_xhci", texto)
        self.assertIn('dr_mode = "peripheral"', texto,
                      "o patch nao poe a porta em modo dispositivo")
        # O nome tem de ordenar depois dos outros dois: o Buildroot aplica
        # em ordem de nome e todos tocam o mesmo DTS.
        self.assertTrue(nome.startswith("0003-"),
                        "o patch do USB tem de ordenar depois do TRNG")

    def test_dts_usado_nao_declara_extcon_nem_role_switch(self):
        """A premissa do patch: sem extcon, OTG nao escolhe papel sozinho.

        Se um dia o DTS (ou o dtsi) ganhar `extcon`/`usb-role-switch`, o
        patch deixa de ser necessario — e este teste avisa em vez de deixar
        um `dr_mode` forcado sem motivo.
        """
        arvores = kernel_trees()
        if not arvores:
            self.skipTest("nenhuma arvore de kernel em dist/")
        dts = os.path.join(arvores[0], "arch/arm64/boot/dts/rockchip",
                           "rk3566-powkiddy-x55.dts")
        dtsi = os.path.join(arvores[0], "arch/arm64/boot/dts/rockchip",
                            "rk356x.dtsi")
        if not (os.path.isfile(dts) and os.path.isfile(dtsi)):
            self.skipTest("DTS da X55 nao encontrado na arvore")

        for caminho in (dtsi, dts):
            corpo = read(caminho)
            nome = os.path.basename(caminho)
            with self.subTest(arquivo=nome):
                # O patch pode ter sido revertido pela propria suite acima,
                # entao procuramos so' as propriedades de deteccao de papel.
                self.assertNotIn("extcon", corpo,
                                 "%s passou a declarar extcon: o patch do "
                                 "USB pode ter ficado desnecessario" % nome)
                self.assertNotIn("usb-role-switch", corpo,
                                 "%s passou a declarar usb-role-switch: o "
                                 "patch do USB pode ter ficado "
                                 "desnecessario" % nome)

    def test_build_rearma_o_kernel_quando_os_patches_mudam(self):
        """Mudar o conjunto de patches NAO invalida o .stamp_patched.

        Medido: um patch novo no diretório não era aplicado, o build ficava
        verde e o DTB saía como antes. Mesma classe do §3.53.
        """
        bi = read(os.path.join(ROOT, "tools", "build-image.sh"))
        self.assertIn(".stamp_patched", bi,
                      "build-image.sh nao re-arma o kernel")
        self.assertRegex(bi, r"patches.*\.sig|patch_state",
                         "nao guarda assinatura dos patches aplicados")

class DefconfigApontaParaOPatch(unittest.TestCase):
    def linha_patch(self, caminho):
        for linha in read(caminho).splitlines():
            if linha.startswith("BR2_LINUX_KERNEL_PATCH="):
                return linha.strip()
        return ""

    def test_templates_apontam_para_o_diretorio(self):
        """O diretório e' melhor que listar arquivo por arquivo.

        O Buildroot, quando o valor e' um diretório, aplica todos os *.patch
        dele em ordem de nome (`apply-patches.sh <dir> <dir> \*.patch`). Listar
        arquivo por arquivo faz um patch novo ser ignorado em silêncio — o
        mesmo tipo de defeito silencioso que esta suíte existe para pegar.
        """
        esperado = ('BR2_LINUX_KERNEL_PATCH="$(BR2_EXTERNAL_GIOCHI_PATH)'
                    '/board/giochi/patches/linux"')
        for caminho in TEMPLATES:
            with self.subTest(template=os.path.basename(caminho)):
                self.assertEqual(self.linha_patch(caminho), esperado)

    def test_defconfigs_gerados_carregam_a_linha(self):
        gerados = sorted(glob.glob(os.path.join(CONFIGS, "*_defconfig")))
        self.assertTrue(gerados, "nenhum defconfig gerado em %s" % CONFIGS)
        for caminho in gerados:
            nome = os.path.basename(caminho)
            with self.subTest(defconfig=nome):
                # Sintoma de 'esqueci o make defconfigs': o template tem a
                # linha e o arquivo que o build usa não tem.
                self.assertIn("BR2_LINUX_KERNEL_PATCH=", read(caminho),
                              "%s sem BR2_LINUX_KERNEL_PATCH; rode "
                              "'make defconfigs'" % nome)

    def test_caminho_do_patch_existe(self):
        for caminho in TEMPLATES:
            linha = self.linha_patch(caminho)
            alvo = linha.split('"')[1]
            alvo = alvo.replace("$(BR2_EXTERNAL_GIOCHI_PATH)",
                                os.path.join(ROOT, "buildroot"))
            with self.subTest(template=os.path.basename(caminho)):
                self.assertTrue(os.path.isdir(alvo),
                                "o defconfig aponta para %s, que não existe"
                                % alvo)
                self.assertTrue(glob.glob(os.path.join(alvo, "*.patch")),
                                "%s não tem nenhum .patch" % alvo)


class PatchAplicaNaArvore(unittest.TestCase):
    def test_aplica_limpo(self):
        arvores = kernel_trees()
        if not arvores:
            self.skipTest("nenhuma árvore de kernel em dist/ para testar")
        patch_bin = shutil.which("patch")
        if not patch_bin:
            self.skipTest("'patch' não instalado")

        for arvore in arvores:
            for caminho in patch_files():
                nome = os.path.basename(caminho)
                with self.subTest(arvore=os.path.basename(arvore), patch=nome):
                    self._aplica(arvore, caminho, patch_bin)

    def _aplica(self, arvore, patch_file, patch_bin):
        # A árvore do build já tem o patch aplicado (é o estado normal depois
        # de um build). Por isso o teste usa uma cópia do arquivo original:
        # se o patch reverter limpo, ele também aplica limpo.
        texto = read(patch_file)
        alvos = [ln.split("+++ b/", 1)[1].strip()
                 for ln in texto.splitlines() if ln.startswith("+++ b/")]
        self.assertTrue(alvos, "%s não declara arquivo alvo"
                        % os.path.basename(patch_file))

        tmp = tempfile.mkdtemp(prefix="giochi-patch-")
        try:
            for alvo in alvos:
                destino = os.path.join(tmp, alvo)
                os.makedirs(os.path.dirname(destino), exist_ok=True)
                shutil.copyfile(os.path.join(arvore, alvo), destino)

            # A árvore de um build já traz o patch aplicado (é o estado normal
            # depois de compilar). Reverter as cópias devolve o estado em que
            # o patch tem de aplicar.
            reverso = subprocess.run([patch_bin, "-p1", "-R", "-s",
                                      "--dry-run"], cwd=tmp, input=texto,
                                     capture_output=True, text=True)
            if reverso.returncode == 0:
                subprocess.run([patch_bin, "-p1", "-R", "-s"], cwd=tmp,
                               input=texto, capture_output=True, text=True)

            r = subprocess.run([patch_bin, "-p1", "-s", "--dry-run"], cwd=tmp,
                               input=texto, capture_output=True, text=True)
            self.assertEqual(r.returncode, 0,
                             "o patch não aplica na árvore %s: %s"
                             % (arvore, (r.stdout + r.stderr).strip()))
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
