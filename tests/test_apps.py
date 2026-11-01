#!/usr/bin/env python3
"""Apps do console em /apps: PPSSPP e PortMaster.

POR QUE ESTE TESTE EXISTE
-------------------------
Os programas do projeto moram em /apps (convencao do repositorio: apps/). No
aparelho, /apps -> /user/apps (symlink criado no post-build). Os pacotes
giochi-ppsspp/giochi-portmaster estagiam em BINARIES_DIR/apps-stage — FORA do
TARGET_DIR, porque /user montado ESCONDE o que estiver sob ele no SquashFS
(§3.46) — e o post-image.sh injeta tudo no user.ext4 com debugfs.

O que e' coberto:
1. as receitas dos pacotes (o que instalam, o que removem do bundle);
2. a injecao de verdade: roda o bloco do post-image.sh sobre um ext4 de
   teste e confere o resultado com debugfs (arquivos, bits de execucao,
   symlinks) — sem root, sem aparelho.
"""

from __future__ import annotations

import os
import pathlib
import shutil
import stat
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
POST_IMAGE = ROOT / "buildroot/board/giochi/post-image.sh"
PM_MK = ROOT / "buildroot/package/giochi-portmaster/giochi-portmaster.mk"
PM_WRAPPER = ROOT / "buildroot/package/giochi-portmaster/portmaster-wrapper"
PM_DEVICE = ROOT / "buildroot/package/giochi-portmaster/device_info.txt"
PM_MOD = ROOT / "buildroot/package/giochi-portmaster/mod_giochi.txt"
PP_MK = ROOT / "buildroot/package/giochi-ppsspp/giochi-ppsspp.mk"
PP_WRAPPER = ROOT / "buildroot/package/giochi-ppsspp/ppsspp-wrapper"
BUSYBOX_FRAGMENT = ROOT / "buildroot/board/giochi/busybox.fragment"

DEBUGFS = shutil.which("debugfs")
MKFS_EXT4 = shutil.which("mkfs.ext4")


def read(path):
    with open(path, encoding="utf-8") as fp:
        return fp.read()


class PacotePortMaster(unittest.TestCase):
    """A receita transforma o bundle muOS no PortMaster do Giochi."""

    def test_extract_e_idempotente_sem_perguntar(self):
        # Medido: o diretorio de build sobrevive a um build que falha, e o
        # unzip INTERATIVO pergunta "replace ...?" com o stdin fechado —
        # trata como "[N]one" e morre. O EXTRACT tem de limpar antes e
        # sobrescrever sem perguntar (-o).
        corpo = read(PM_MK)
        self.assertIn("rm -rf $(@D)", corpo,
                      "sem limpar, o segundo build pergunta 'replace?' e morre")
        self.assertIn("unzip -q -o", corpo,
                      "sem -o, o unzip interage com o stdin fechado")

    def test_hash_do_bundle_fixado(self):
        # Integridade do download: o hash fixa a versao 2026.11.01-1314.
        caminho = ROOT / "buildroot/package/giochi-portmaster" \
            / "giochi-portmaster.hash"
        self.assertTrue(caminho.is_file(), "sem hash, um bundle adulterado "
                                           "passaria calado")
        corpo = read(caminho)
        self.assertIn("muos.portmaster.zip", corpo)
        self.assertRegex(corpo, r"sha256\s+[0-9a-f]{64}", "formato sha256")

    def test_instala_no_binaries_dir_nao_no_target(self):
        corpo = read(PM_MK)
        self.assertIn("$(BINARIES_DIR)/apps-stage", corpo,
                      "o PortMaster tem de estagiar FORA do TARGET_DIR: /user "
                      "no SquashFS fica escondido pelo user.ext4 (§3.46)")
        self.assertNotIn("$(TARGET_DIR)/apps", corpo)

    def test_controlfolder_aponta_para_apps(self):
        corpo = read(PM_MK)
        self.assertIn('controlfolder="/apps/PortMaster"', corpo,
                      "sem o ajuste, o PortMaster procura /mnt/mmc/MUOS/...")

    def test_remove_o_image_smash_do_muos(self):
        corpo = read(PM_MK)
        self.assertIn("image_smash", corpo,
                      "a chamada muOS-only tem de ser removida da receita")
        # E a remocao nao pode ser um no-op: tem de operar sobre o script.
        self.assertIn("PortMaster.sh", corpo)

    def test_descarta_firmwares_alheios_e_binarios_errados(self):
        corpo = read(PM_MK)
        for firma in ("batocera", "knulli", "trimui", "retrodeck", "miyoo"):
            self.assertIn(firma, corpo,
                          f"a pasta {firma} nao e' a nossa")
        self.assertIn("armhf", corpo, "binarios de outra arquitetura saem")
        self.assertIn("x86_64", corpo)

    def test_symlinks_de_arquitetura_sao_criados(self):
        corpo = read(PM_MK)
        for alvo in ("7zzs.aarch64 7zzs", "sdl2imgshow.aarch64 sdl2imgshow",
                     "libinterpose.aarch64.so libinterpose.so"):
            self.assertIn("ln -sf " + alvo, corpo,
                          f"os ports chamam a ferramenta sem sufixo: {alvo}")

    def test_device_info_declara_o_giochi(self):
        corpo = read(PM_DEVICE)
        self.assertIn('CFW_NAME="giochi"', corpo)
        self.assertIn('DEVICE_HAS_AARCH64="Y"', corpo)
        self.assertIn('DEVICE_ARCH="aarch64"', corpo)
        # musl: sem CFW_GLIBC — o PortMaster usa esse valor para box64/box86.
        self.assertIn('CFW_GLIBC=""', corpo)

    def test_mod_giochi_sem_pipewire_sway(self):
        # O comentario do arquivo CITA sway/pipewire; o que vale e' o codigo.
        linhas = [l for l in read(PM_MOD).splitlines()
                  if l.strip() and not l.lstrip().startswith("#")]
        corpo = "\n".join(linhas)
        self.assertIn("GODOT_OPTS", corpo,
                      "os ports Godot precisam do GODOT_OPTS")
        for ausente in ("sway", "pipewire"):
            self.assertNotIn(ausente, corpo,
                             f"{ausente} nao existe no Giochi (no codigo)")

    def test_wrapper_faz_a_troca_do_fbcon(self):
        corpo = read(PM_WRAPPER)
        self.assertIn("vtconsole", corpo,
                      "o PortMaster e' UI em bash: precisa do fbcon de volta "
                      "(o S90graphics o desvincula para a UI SDL)")
        self.assertIn("bash PortMaster.sh", corpo)
        self.assertIn('exec bash', corpo)


class PacotePPSSPP(unittest.TestCase):
    """A receita constroi o SDL (nao Qt) e o headless (o CLI)."""

    def test_estagia_fora_do_target(self):
        corpo = read(PP_MK)
        self.assertIn("$(BINARIES_DIR)/apps-stage", corpo)
        self.assertNotIn("$(TARGET_DIR)/apps", corpo)

    def test_pasta_maiuscula_e_wrapper_separado(self):
        # O APP:ppsspp resolve /apps/ppsspp, que tem de ser EXECUTAVEL: pasta
        # e wrapper com o MESMO nome nao podem coexistir. Arranjo igual ao
        # PortMaster: /apps/PPSSPP (pasta) + /apps/ppsspp (wrapper).
        # Medido: a receita ja estagiou em `ppsspp/` minusculo enquanto o
        # wrapper fazia cd /apps/PPSSPP — inconsistencia calada, porque
        # nenhum teste conferia o NOME da pasta.
        corpo = read(PP_MK)
        self.assertIn("apps-stage/PPSSPP/assets", corpo,
                      "a pasta do app e' /apps/PPSSPP (maiuscula, como o "
                      "binario PPSSPPSDL)")
        self.assertIn("$(BINARIES_DIR)/apps-stage/ppsspp\n", corpo,
                      "o wrapper execuvel e' /apps/ppsspp (minusculo)")

    def test_sem_qt_ffmpeg_discord(self):
        corpo = read(PP_MK)
        self.assertIn("-DUSING_QT_UI=OFF", corpo,
                      "a variante Qt e' a do desktop (Alpine); aqui e' SDL")
        self.assertIn("-DUSE_FFMPEG=OFF", corpo,
                      "sem ffmpeg os movies nao tocam; jogos nao precisam")
        self.assertIn("-DUSE_DISCORD=OFF", corpo)

    def test_gles2_sem_o_bloco_egl_com_x11(self):
        # Medido no build real: o bloco EGL do PPSSPP chama XOpenDisplay sem
        # guarda no ramo nao-fbdev e morre em alvo sem libX11. O contexto
        # GLES2 vem do SDL2; a config e' a MESMA da ROCKNIX (ppsspp-sa,
        # ramo OPENGLES_SUPPORT).
        corpo = read(PP_MK)
        self.assertIn("-DUSING_GLES2=ON", corpo)
        self.assertIn("-DUSING_EGL=OFF", corpo,
                      "com USING_EGL=ON o bloco EGL do PPSSPP compila e "
                      "chama X11 direto (XCloseDisplay undeclared no alvo)")
        self.assertIn("-DUSING_FBDEV=ON", corpo,
                      "sem FBDEV, o ramo #else assume X11 disponivel")

    def test_sdl2_ttf_pelo_pkgconfig_nao_pelo_config_quebrado(self):
        # Medido no build real: o sdl2_ttf-config.cmake gerado pelo autotools
        # do Buildroot nao e' relocavel (procura /usr/lib do HOST), seta
        # SDL2_ttf_FOUND=TRUE sem criar o alvo, e o generate do PPSSPP morre
        # em "SDL2_ttf::SDL2_ttf not found". Com o find_package desligado, o
        # fallback pkg_check_modules do proprio PPSSPP usa o .pc do staging.
        corpo = read(PP_MK)
        self.assertIn("-DCMAKE_DISABLE_FIND_PACKAGE_SDL2_ttf=TRUE", corpo,
                      "sem desligar o find_package, o config nao-relocavel do "
                      "autotools vence e o alvo SDL2_ttf::SDL2_ttf nao nasce")

    def test_gera_o_headless_o_cli(self):
        corpo = read(PP_MK)
        self.assertIn("-DHEADLESS=ON", corpo,
                      "o 'PPSSPP CLI' e' o PPSSPPHeadless")
        self.assertIn("PPSSPPHeadless", corpo)

    def test_assets_viajam_junto(self):
        corpo = read(PP_MK)
        for asset in ("flash0", "lang", "font_atlas.zim"):
            self.assertIn(asset, corpo,
                          f"sem {asset} o menu sai sem fonte e jogos PSP "
                          "ficam sem texto")

    def test_wrapper_cd_antes_de_exec(self):
        corpo = read(PP_WRAPPER)
        self.assertIn("cd /apps/PPSSPP", corpo,
                      "o PPSSPP resolve os assets relativos ao cwd")
        self.assertIn("exec ./PPSSPPSDL", corpo)


class InjecaoNoUserExt4(unittest.TestCase):
    """Roda o bloco de injecao do post-image.sh sobre um ext4 de teste.

    Confere com debugfs o que o bloco escreveu: arquivos, bits de execucao e
    symlinks — exatamente o que o primeiro boot do aparelho vai encontrar.
    """

    @classmethod
    def setUpClass(cls):
        if not DEBUGFS or not MKFS_EXT4:
            raise unittest.SkipTest("e2fsprogs ausente no host")

    def _bloco_injecao(self):
        fonte = read(POST_IMAGE)
        ini = fonte.index('    if [ -d "$BINARIES_DIR/apps-stage" ]')
        fim = fonte.index('    fi\n', fonte.index('rm -f "$cmds"')) + len('    fi\n')
        return "#!/bin/sh\nset -eu\n" + fonte[ini:fim]

    def _debugfs(self, img, comando):
        r = subprocess.run([DEBUGFS, "-R", comando, img],
                           capture_output=True, text=True, timeout=30)
        return r.stdout

    def _montar_estagio(self, base):
        stage = pathlib.Path(base) / "apps-stage"
        (stage / "PPSSPP" / "assets").mkdir(parents=True)
        (stage / "PortMaster").mkdir()
        alvo = stage / "PPSSPP" / "PPSSPPSDL"
        alvo.write_text("x")
        alvo.chmod(0o755)
        (stage / "PPSSPP" / "assets" / "font_atlas.zim").write_text("z")
        wrapper = stage / "portmaster"
        wrapper.write_text("#!/bin/sh\necho oi\n")
        wrapper.chmod(0o755)
        # o wrapper do PPSSPP tambem e' arquivo executavel na raiz do stage —
        # no build real de 01/11 ele saiu CORROMPIDO na imagem (o write do
        # debugfs falhou calado por cima do diretorio da receita antiga e o
        # set_inode_field transformou o inode do DIRETORIO em "arquivo").
        wrapper_ppsspp = stage / "ppsspp"
        wrapper_ppsspp.write_text("#!/bin/sh\necho ppsspp\n")
        wrapper_ppsspp.chmod(0o755)
        # symlink como os que a receita do PortMaster cria
        (stage / "PortMaster" / "7zzs").symlink_to("7zzs.aarch64")
        return stage

    def test_injeta_arquivos_executaveis_e_symlinks(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = pathlib.Path(tmp)
            self._montar_estagio(base)
            img = base / "user.ext4"
            with open(img, "wb") as fh:
                fh.truncate(64 * 1024 * 1024)
            subprocess.run([MKFS_EXT4, "-q", "-F", str(img)], check=True)

            script = base / "inj.sh"
            script.write_text(self._bloco_injecao())
            # O bloco espera $HOST_DIR/sbin/debugfs (o host-e2fsprogs do
            # Buildroot); aqui um HOST_DIR falso aponta para o debugfs do
            # sistema, sem depender de dist/ existir.
            host = base / "host"
            (host / "sbin").mkdir(parents=True)
            (host / "sbin" / "debugfs").symlink_to(DEBUGFS)
            r = subprocess.run(
                ["sh", str(script)],
                env=dict(os.environ, BINARIES_DIR=str(base),
                         HOST_DIR=str(host), USER_SIZE_MB="64",
                         USER_LABEL="GIOCHI_TESTE"),
                capture_output=True, text=True, timeout=120)
            self.assertEqual(0, r.returncode,
                             f"injecao falhou: {r.stdout} {r.stderr}")

            ls = self._debugfs(str(img), "ls -l /apps")
            for esperado in ("PPSSPP", "PortMaster", "portmaster", "ppsspp"):
                self.assertIn(esperado, ls, f"/apps/{esperado} nao foi criado")

            stat_ppsspp = self._debugfs(str(img), "stat /apps/PPSSPP/PPSSPPSDL")
            self.assertIn("0755", stat_ppsspp,
                          "o binario tem de nascer executavel")
            stat_wrapper = self._debugfs(str(img), "stat /apps/portmaster")
            self.assertIn("0755", stat_wrapper)

            # o wrapper /apps/ppsspp tem de ser ARQUIVO com o texto do stage
            # (no build real de 01/11 ele saiu como "arquivo" de 4096 B com
            # bloco de diretorio da receita antiga dentro)
            stat_pp = self._debugfs(str(img), "stat /apps/ppsspp")
            self.assertIn("regular", stat_pp,
                          "/apps/ppsspp nao e' arquivo regular")
            self.assertIn("0755", stat_pp)
            cat_pp = self._debugfs(str(img), "cat /apps/ppsspp")
            self.assertEqual("#!/bin/sh\necho ppsspp\n", cat_pp,
                             "conteudo do wrapper /apps/ppsspp errado")

            ls_pm = self._debugfs(str(img), "ls -l /apps/PortMaster")
            self.assertIn("7zzs", ls_pm)

            # assets em profundidade
            stat_font = self._debugfs(str(img),
                                      "stat /apps/PPSSPP/assets/font_atlas.zim")
            self.assertIn("regular", stat_font)

    def test_injecao_e_idempotente(self):
        """Rodar duas vezes nao pode duplicar nem falhar: o user.ext4 e'
        recriado do zero a cada execucao do bloco, e o post-image roda em
        todo build incremental."""
        with tempfile.TemporaryDirectory() as tmp:
            base = pathlib.Path(tmp)
            self._montar_estagio(base)
            img = base / "user.ext4"
            with open(img, "wb") as fh:
                fh.truncate(64 * 1024 * 1024)
            subprocess.run([MKFS_EXT4, "-q", "-F", str(img)], check=True)
            script = base / "inj.sh"
            script.write_text(self._bloco_injecao())
            host = base / "host"
            (host / "sbin").mkdir(parents=True)
            (host / "sbin" / "debugfs").symlink_to(DEBUGFS)
            env = dict(os.environ, BINARIES_DIR=str(base),
                       HOST_DIR=str(host), USER_SIZE_MB="64",
                       USER_LABEL="GIOCHI_TESTE")
            for _ in range(2):
                r = subprocess.run(["sh", str(script)], env=env,
                                   capture_output=True, text=True, timeout=120)
                self.assertEqual(0, r.returncode, r.stdout + r.stderr)
            ls = self._debugfs(str(img), "ls -l /apps")
            self.assertEqual(1, ls.count("portmaster"),
                             "rodar duas vezes duplicou o wrapper")

    def test_injecao_conserta_arvore_de_receita_antiga(self):
        """O cenario que corrompeu o build real de 01/11: o user.ext4 guarda
        a arvore da receita ANTIGA (ppsspp era diretorio, havia ppsspp-exec
        e um arquivo que nao existe mais). O bloco tem de recriar o fs e
        entregar exatamente o conteudo do stage — sem lixo nenhum."""
        with tempfile.TemporaryDirectory() as tmp:
            base = pathlib.Path(tmp)
            self._montar_estagio(base)
            img = base / "user.ext4"
            with open(img, "wb") as fh:
                fh.truncate(64 * 1024 * 1024)
            subprocess.run([MKFS_EXT4, "-q", "-F", str(img)], check=True)

            # arvore velha: ppsspp como DIRETORIO, symlink de receita antiga
            # e um arquivo que nao existe mais no stage
            lixo = base / "lixo"
            lixo.write_text("receita velha\n")
            for cmd in ("mkdir /apps", "mkdir /apps/ppsspp",
                        "symlink /apps/ppsspp-exec ppsspp/run.sh",
                        f"write {lixo} /apps/lixo"):
                subprocess.run([DEBUGFS, "-w", "-R", cmd, str(img)],
                               check=True, capture_output=True, text=True,
                               timeout=30)

            script = base / "inj.sh"
            script.write_text(self._bloco_injecao())
            host = base / "host"
            (host / "sbin").mkdir(parents=True)
            (host / "sbin" / "debugfs").symlink_to(DEBUGFS)
            r = subprocess.run(
                ["sh", str(script)],
                env=dict(os.environ, BINARIES_DIR=str(base),
                         HOST_DIR=str(host), USER_SIZE_MB="64",
                         USER_LABEL="GIOCHI_TESTE"),
                capture_output=True, text=True, timeout=120)
            self.assertEqual(0, r.returncode,
                             f"injecao falhou: {r.stdout} {r.stderr}")

            ls = self._debugfs(str(img), "ls -l /apps")
            for esperado in ("PPSSPP", "PortMaster", "portmaster", "ppsspp"):
                self.assertIn(esperado, ls)
            for lixo_esperado in ("ppsspp-exec", "lixo"):
                self.assertNotIn(lixo_esperado, ls,
                                 f"resquicio da receita antiga: {lixo_esperado}")

            stat_pp = self._debugfs(str(img), "stat /apps/ppsspp")
            self.assertIn("regular", stat_pp,
                          "/apps/ppsspp continuou diretorio corrompido")
            cat_pp = self._debugfs(str(img), "cat /apps/ppsspp")
            self.assertEqual("#!/bin/sh\necho ppsspp\n", cat_pp)

    def test_bloco_nao_usa_rm_de_debugfs(self):
        """O `rm` do debugfs 1.47 NAO aceita flags: `rm -rf` imprime usage,
        nao remove nada, e o `debugfs -f` devolve 0 mesmo assim (foi assim
        que a arvore antiga sobreviveu no build real). O bloco nao pode
        depender de rm: ele recria o fs."""
        fonte = read(POST_IMAGE)
        ini = fonte.index('    if [ -d "$BINARIES_DIR/apps-stage" ]')
        fim = fonte.index('    fi\n', fonte.index('rm -f "$cmds"')) + len('    fi\n')
        bloco = fonte[ini:fim]
        self.assertNotIn("rm -rf /apps", bloco,
                         "rm do debugfs nao aceita flags; o bloco recria o fs")


class DefconfigEImagem(unittest.TestCase):
    def test_ambos_os_pacotes_estao_nos_defconfigs(self):
        for base in ("base-rk3566.defconfig", "base-rk3326.defconfig"):
            corpo = read(ROOT / "buildroot/board/giochi/defconfig" / base)
            self.assertIn("BR2_PACKAGE_GIOCHI_PORTMASTER=y", corpo, base)
            self.assertIn("BR2_PACKAGE_GIOCHI_PPSSPP=y", corpo, base)

    def test_portmaster_pede_o_zip_de_verdade(self):
        # O BusyBox 1.36.1 NAO tem o applet zip (so' unzip); o PortMaster
        # usa zip em runtime, entao o pacote tem de pedir o Info-ZIP.
        corpo = read(ROOT / "buildroot/package/giochi-portmaster/Config.in")
        self.assertIn("BR2_PACKAGE_ZIP", corpo,
                      "sem o Info-ZIP o PortMaster quebra ao empacotar saves")
        # So' as linhas de CODIGO: o comentario do fragmento cita CONFIG_ZIP
        # (para explicar que ele nao existe) — casar com o texto inteiro
        # passaria mesmo com o simbolo invalido de volta (classe §3.36).
        linhas = [l.strip() for l in read(BUSYBOX_FRAGMENT).splitlines()
                  if l.strip() and not l.lstrip().startswith("#")]
        self.assertNotIn("CONFIG_ZIP", "\n".join(linhas),
                         "CONFIG_ZIP nao existe no BusyBox 1.36.1; o validador "
                         "do fragmento reprova simbolo inexistente")

    def test_wrappers_estao_versionados(self):
        # A armadilha §3.1: arquivo de pacote que o .gitignore engole so'
        # aparece no CI. Os dois wrappers tem de estar no git.
        for caminho in (PM_WRAPPER, PP_WRAPPER, PM_DEVICE, PM_MOD):
            self.assertTrue(caminho.is_file(), f"{caminho} nao existe")


if __name__ == "__main__":
    unittest.main(verbosity=2)
