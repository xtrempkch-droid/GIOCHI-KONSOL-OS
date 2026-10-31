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


if __name__ == "__main__":
    unittest.main()
