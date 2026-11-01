"""Invariantes do primeiro boot sadio — cada uma veio de um defeito medido.

Todos os itens abaixo apareceram no log serial de um boot real da X55 (imagem
de 2026-10-31) e nenhum deles impedia o sistema de subir: o kernel entrava, o
`giochi-init` subia, o Launcher desenhava — e o usuário via a tela apagada, sem
biblioteca, sem música e com o mapa de botões errado. Por isso viram teste.

O que este módulo NÃO cobre: se o DRM faz bind do painel (isso é kernel, ver
AGENTS.md §3.44) e se o cartão grava bem (tests/test_verify_card.py).
"""

import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

RCS = os.path.join(ROOT, "buildroot/board/giochi/rootfs_overlay/etc/init.d/rcS")
POST_BUILD = os.path.join(ROOT, "buildroot/board/giochi/post-build.sh")
S10STORAGE = os.path.join(ROOT, "scripts/init.d/S10storage")
LAUNCHER_MAIN = os.path.join(ROOT, "src/launcher/main.cpp")
DEVICECONF_C = os.path.join(ROOT, "src/common/giochi_deviceconf.c")
BUILD_IMAGE = os.path.join(ROOT, "tools/build-image.sh")
S05MODULES = os.path.join(ROOT, "scripts/init.d/S05modules")
MDEV_CONF = os.path.join(ROOT, "buildroot/board/giochi/mdev.conf")
BUSYBOX_FRAGMENT = os.path.join(ROOT, "buildroot/board/giochi/busybox.fragment")
LINUX_POWKIDDY = os.path.join(ROOT, "buildroot/board/giochi/linux-powkiddy.fragment")
S20GRAPHICS = os.path.join(ROOT, "scripts/init.d/S20graphics")


def read(path):
    with open(path, encoding="utf-8") as fp:
        return fp.read()


class PrimeiroBootTest(unittest.TestCase):
    def test_rcs_nao_chama_servico_duas_vezes(self):
        """'$script' start || '$script' rodava o serviço sem argumento.

        Foi assim que o boot da X55 imprimiu
        "Usage: /etc/init.d/S10mdev {start|stop|restart}": o `start` do
        S10mdev saía != 0 (não há modprobe no rootfs) e o fallback chamava o
        script de novo, sem argumento.
        """
        body = read(RCS)

        self.assertNotRegex(
            body,
            r'"?\$script"?\s+start\s*\|\|',
            "rcS voltou a chamar o serviço duas vezes (start || sem argumento)",
        )
        self.assertIn('"$script" start', body)
        self.assertIn("if ! \"$script\" start; then", body)

    def test_s10mdev_nao_entra_na_imagem(self):
        """Sem modprobe, o S10mdev só gera erro — quem carrega módulo é o hotplug."""
        body = read(POST_BUILD)
        match = re.search(r"for conflito in ([^;]+); do", body)

        self.assertIsNotNone(match, "post-build.sh sem a lista de conflitos")
        conflitos = match.group(1).split()
        for nome in ("S01syslogd", "S02klogd", "S50dropbear", "S10mdev"):
            self.assertIn(nome, conflitos, f"{nome} deveria sair da imagem")

    def test_arvore_de_user_e_criada(self):
        """/user é o ponto de montagem de um ext4 VAZIO.

        Os diretórios criados no SquashFS ficam escondidos depois do mount, e
        o Launcher registrou "'/user/games' inacessivel" → 0 pacotes.
        """
        body = read(S10STORAGE)

        for sub in ("music", "games", "ports", "saves"):
            self.assertIn(f'"$_u/{sub}"', body)
        self.assertIn("seed_user_tree", body)
        # A chamada tem de existir, não só a definição.
        self.assertGreaterEqual(body.count("seed_user_tree"), 2)
        # Prova de escrita de verdade: mkdir -p num diretório que já existe no
        # SquashFS devolve 0 mesmo com a partição fora do ar.
        self.assertIn("boot-write-test", body)

    def test_launcher_le_device_conf_pela_chave_inteira(self):
        """A chave tem 19 caracteres; o código pulava 20 e lia "efault"."""
        body = read(LAUNCHER_MAIN)

        self.assertIn("giochi_deviceconf_get", body)
        self.assertNotIn('.find("GIOCHI_BUTTONS_MAP="', body)
        self.assertNotRegex(
            body,
            r"pos\s*\+\s*20",
            "voltou a pular 20 caracteres depois da chave de 19",
        )

    def test_deviceconf_recusa_truncar_valor(self):
        """Valor maior que o buffer devolve fallback, não uma string cortada."""
        body = read(DEVICECONF_C)

        self.assertIn("strlen(value) + 1 > out_size", body)
        self.assertIn("line[key_len] != '='", body)

    def test_build_ressincroniza_pacote_local(self):
        """Pacote local com .stamp_rsynced NÃO trazia código novo para a imagem.

        Sintoma medido: as correções do commit 39d17b1 estavam no repositório,
        `make` terminou sem erro, e o aparelho continuou registrando
        `(device=rk3326 soc=rk3326)` e `buttons.map: secao [efault]` — porque o
        Buildroot só roda o rsync de `SITE_METHOD = local` quando reconstrói o
        pacote, e a versão (1.2) não muda.

        Este teste falha se alguém remover o `rm` dos stamps.
        """
        body = read(BUILD_IMAGE)

        self.assertIn("build/build/giochi-*", body,
                      "build-image.sh não limpa os pacotes locais")
        self.assertRegex(body, r'rm -f "\$pkg"/\.stamp_\*',
                         "build-image.sh não apaga os stamps do pacote local")

    def test_coldplug_de_modulos_no_boot(self):
        """Sem coldplug no espaço de usuário, nenhum .ko é carregado.

        Medido no boot #3 da X55: `/lib/modules/6.12.0/` cheio e
        `dw-mipi-dsi-rockchip: failed to get mipi dphy: -517`, ALSA sem placa,
        nenhum `rtw88`. O kernel só chama `request_module()` depois que o rootfs
        existe, e os dispositivos de plataforma já nasceram antes disso.
        """
        script = read(S05MODULES)
        mdev = read(MDEV_CONF)

        self.assertIn("mdev -s", script, "S05modules não faz o coldplug")
        self.assertIn("S05modules", read(POST_BUILD),
                      "post-build.sh não instala o S05modules")
        # A regra do modalias é o que transforma o evento em modprobe.
        self.assertRegex(mdev, r'(?m)^\$MODALIAS=\.\*\s+root:root\s+\S+\s+'
                               r'@modprobe "?\$MODALIAS"?\s*$',
                         "mdev.conf sem a regra $MODALIAS -> modprobe")

        # 'modprobe -b' (blacklist) exige CONFIG_FEATURE_MODPROBE_BLACKLIST, que
        # NAO esta ligado no BusyBox desta imagem: o modprobe recusaria a opcao
        # e nada seria carregado — o mesmo defeito, com um erro a mais.
        bb = read(BUSYBOX_FRAGMENT)
        if re.search(r"modprobe\s+-b\b", mdev):
            self.assertIn("CONFIG_FEATURE_MODPROBE_BLACKLIST=y", bb,
                          "mdev.conf usa 'modprobe -b' sem a feature no BusyBox")

        # Sem FEATURE_MDEV_CONF o mdev ignora o mdev.conf por completo.
        self.assertIn("CONFIG_FEATURE_MDEV_CONF=y", bb,
                      "sem FEATURE_MDEV_CONF o mdev.conf e' ignorado")
        self.assertIn("CONFIG_MDEV=y", bb, "sem MDEV nao ha coldplug")

    def test_phy_do_dsi_e_embutido(self):
        """PHY DSI como módulo deixava o DSI esperando (-517) para sempre.

        O painel não acende sem /dev/dri/card0, e o card0 só existe quando o
        agregado do DRM completa — o que depende do PHY do DSI estar pronto.
        """
        body = read(LINUX_POWKIDDY)

        self.assertIn("CONFIG_PHY_ROCKCHIP_INNO_DSIDPHY=y", body,
                      "PHY do DSI voltou a ser carregável (=m)")

    def test_s20graphics_le_soc_do_device_conf(self):
        """O `compatible` do RK3566 começa por 'powkiddy' (fabricante).

        Ler o primeiro campo dava "SoC 'powkiddy' nao reconhecido" e o Mesa
        ficava sem `MESA_LOADER_DRIVER_OVERRIDE`.
        """
        body = read(S20GRAPHICS)

        self.assertIn("GIOCHI_SOC", body)
        self.assertIn("DEVICE_CONF", body)
        self.assertNotRegex(
            body,
            r"SOC=\$\(tr -d '\\0' </proc/device-tree/compatible[^)]*\| head -n1\)",
            "voltou a pegar o primeiro campo do compatible como SoC",
        )

    def test_build_nao_anuncia_imagem_quando_make_falha(self):
        """'if ! build_device ...' desliga o 'set -e' dentro da função.

        Medido: o kernel falhou no passo de patch (patch duplicado), o script
        seguiu, achou o `sdcard.img` VELHO em images/ e imprimiu
        "imagens geradas" com rc=0. Ou seja: falha de build anunciada como
        sucesso, com uma imagem de antes entregue ao aparelho.
        """
        body = read(BUILD_IMAGE)

        # Toda invocacao de make precisa de tratamento de erro explicito.
        linhas = body.splitlines()
        janelas = []
        for i, linha in enumerate(linhas):
            if 'make -C "$buildroot"' not in linha:
                continue
            janela = [linha]
            j = i
            while j < len(linhas) and \
                    linhas[j].rstrip().endswith("\\"):
                j += 1
                janela.append(linhas[j])
            if j + 1 < len(linhas):
                janela.append(linhas[j + 1])
            janelas.append("\n".join(janela))

        self.assertGreaterEqual(len(janelas), 2,
                                "esperava 'make defconfig' e 'make' no script")
        for trecho in janelas:
            with self.subTest(make=trecho.splitlines()[0][:60]):
                self.assertIn("||", trecho,
                              "make sem tratamento de erro: falha vira sucesso")


if __name__ == "__main__":
    unittest.main()
