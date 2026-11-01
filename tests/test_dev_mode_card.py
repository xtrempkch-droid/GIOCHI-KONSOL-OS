"""Modo Desenvolvedor pelo lado do PC: `tools/giochi-dev-mode.sh`.

O SSH via USB é **desligado de fábrica** (§4.6.1, AGENTS.md §3.9). Quando a tela
não ajuda — Launcher travado, vídeo que não sobe —, a única entrada é o USB, e
ela está fechada. O script abre a porta mexendo só na partição `/user`.

Este teste roda o script contra um diretório temporário e depois **executa o
`S62sshd` real** com o que ele escreveu, com `dropbear` falso: é a prova de que
o cartão preparado produz um daemon que sobe, e não apenas um JSON bonito.
"""

from __future__ import annotations

import json
import os
import shutil
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "tools" / "giochi-dev-mode.sh"
SSHD = ROOT / "scripts" / "init.d" / "S62sshd"

# Interfaces que existem em qualquer host com loopback: o S62sshd espera
# /sys/class/net/<iface>, e `lo` está sempre lá.
IFACE = "lo"


class DevModeScriptTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)
        self.user = self.base / "user"
        (self.user / "games").mkdir(parents=True)   # marca a partição /user
        self.key = self.base / "testkey"
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "",
                        "-f", str(self.key)], check=True)

    def tearDown(self):
        self.tmp.cleanup()

    def rodar(self, *args: str) -> subprocess.CompletedProcess:
        return subprocess.run(["sh", str(SCRIPT), *args],
                              capture_output=True, text=True, timeout=60)

    # -- o que o script escreve -----------------------------------------

    def test_cria_settings_com_dev_mode_ligado(self):
        r = self.rodar("--key", str(self.key) + ".pub", str(self.user))
        self.assertEqual(r.returncode, 0, r.stderr)

        doc = json.loads((self.user / "settings.json").read_text())
        self.assertTrue(doc["developer"]["ssh_usb_enabled"])
        # O resto do arquivo tem de continuar consistente: o Launcher reescreve
        # inteiro, mas lê este antes.
        self.assertEqual(doc["schema_version"], "1.1")
        self.assertIn("theme_id", doc["appearance"])
        self.assertIn("usb_mode", doc["system"])

    def test_instala_a_chave_publica(self):
        self.rodar("--key", str(self.key) + ".pub", str(self.user))
        chave = (self.key.with_suffix(".pub")).read_text().strip()
        autorizada = (self.user / "ssh" / "authorized_keys").read_text()

        self.assertIn(chave, autorizada)
        modo = stat.S_IMODE((self.user / "ssh" / "authorized_keys").stat().st_mode)
        self.assertEqual(modo, 0o600, "authorized_keys tem de ser 0600")

    def test_preserva_settings_existente_e_faz_backup(self):
        original = {
            "schema_version": "1.1",
            "appearance": {"theme_id": "ios_glass", "accent_color": "#FFFFFF",
                           "font_scale": 1.0, "ui_scale": 1.0},
            "system": {"language": "en", "timezone": "UTC", "brightness": 40,
                       "suspend_timeout_s": 120, "usb_mode": "mass"},
            "sound": {"master_volume": 10, "game_volume": 20, "music_volume": 30,
                      "muted": True},
            "store": {"region": "US", "source": "curated"},
            "network": {"wifi_enabled": True, "wifi_last_ssid": "casa",
                        "wifi_credentials": [], "bluetooth_enabled": False,
                        "bluetooth_paired": []},
            "developer": {"ssh_usb_enabled": False},
        }
        (self.user / "settings.json").write_text(json.dumps(original, indent=2))

        self.rodar("--key", str(self.key) + ".pub", str(self.user))

        doc = json.loads((self.user / "settings.json").read_text())
        self.assertTrue(doc["developer"]["ssh_usb_enabled"])
        # Nada além da flag pode ter mudado: o usuário escolheu esses valores.
        self.assertEqual(doc["appearance"]["theme_id"], "ios_glass")
        self.assertEqual(doc["system"]["usb_mode"], "mass")
        self.assertTrue(doc["sound"]["muted"])
        self.assertTrue((self.user / "settings.json.bak").is_file(),
                        "sem backup, um erro de sed é irreversível")

    def test_desligar_volta_a_false(self):
        self.rodar("--key", str(self.key) + ".pub", str(self.user))
        r = self.rodar("--desligar", str(self.user))
        self.assertEqual(r.returncode, 0, r.stderr)

        doc = json.loads((self.user / "settings.json").read_text())
        self.assertFalse(doc["developer"]["ssh_usb_enabled"])

    def test_recusa_chave_que_nao_e_publica(self):
        ruim = self.base / "nao-e-chave.pub"
        ruim.write_text("isto nao e uma chave\n")
        r = self.rodar("--key", str(ruim), str(self.user))

        self.assertNotEqual(r.returncode, 0)
        self.assertIn("chave", r.stderr.lower())
        self.assertFalse((self.user / "settings.json").exists(),
                         "falhou depois de já ter escrito o settings")

    def test_recusa_diretorio_inexistente(self):
        r = self.rodar("--key", str(self.key) + ".pub",
                       str(self.base / "nao-existe"))
        self.assertNotEqual(r.returncode, 0)

    def test_nao_escreve_chave_privada_no_cartao(self):
        """Só a pública vai: a privada nunca sai de ~/.ssh."""
        self.rodar("--key", str(self.key) + ".pub", str(self.user))
        privada = self.key.read_text()

        for arquivo in self.user.rglob("*"):
            if arquivo.is_file():
                self.assertNotIn(privada, arquivo.read_text(errors="ignore"),
                                 f"chave privada vazou para {arquivo}")


class SshdSobeTest(unittest.TestCase):
    """O `S62sshd` real, com `dropbear` falso, sobre o cartão preparado."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)
        self.user = self.base / "user"
        (self.user / "games").mkdir(parents=True)
        self.bin = self.base / "bin"
        self.bin.mkdir()
        self.calls = self.base / "calls"
        self.key = self.base / "k"
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "",
                        "-f", str(self.key)], check=True)

        # Stubs: nenhum processo de verdade, mas cada chamada é registrada.
        for nome, corpo in (
            ("logger", '#!/bin/sh\necho "logger $*" >> "$CALLS"\n'),
            ("dropbear", '#!/bin/sh\necho "dropbear $*" >> "$CALLS"\n'),
            ("dropbearkey", '#!/bin/sh\necho "dropbearkey $*" >> "$CALLS"\n'),
            ("pidof", "#!/bin/sh\nexit 1\n"),
            ("giochi_wait_for", "#!/bin/sh\nexit 0\n"),
        ):
            p = self.bin / nome
            p.write_text(corpo)
            p.chmod(0o755)

    def tearDown(self):
        self.tmp.cleanup()

    def rodar_sshd(self) -> subprocess.CompletedProcess:
        env = dict(os.environ, CALLS=str(self.calls), PATH=f"{self.bin}:{os.environ['PATH']}",
                   SETTINGS=str(self.user / "settings.json"),
                   USB_SSH_KEYS=str(self.user / ".ssh"),
                   USB_SSH_AUTHKEYS=str(self.user / "ssh" / "authorized_keys"),
                   USB_SSH_AUTHKEYS_DIR=str(self.base / "dropbear"),
                   USB_SSH_PIDFILE=str(self.base / "run" / "dropbear.pid"),
                   USB_NET_IFACE=IFACE)
        return subprocess.run(["sh", str(SSHD), "start"], capture_output=True,
                              text=True, env=env, timeout=60)

    def test_cartao_preparado_sobe_dropbear_so_no_ip_do_console(self):
        subprocess.run(["sh", str(SCRIPT), "--key", str(self.key) + ".pub",
                        str(self.user)], check=True, capture_output=True)
        r = self.rodar_sshd()
        self.assertEqual(r.returncode, 0, r.stderr)

        chamadas = self.calls.read_text() if self.calls.is_file() else ""
        self.assertIn("dropbear -p 10.55.0.2:22", chamadas,
                      f"dropbear nao subiu no IP do console:\n{chamadas}")
        # O contrato do adendo: nunca 0.0.0.0 (SSH aberto em toda interface).
        self.assertNotIn("0.0.0.0", chamadas)
        self.assertIn("authorized_keys instalada", chamadas)

    def test_sem_dev_mode_nao_sobe(self):
        """Padrão de fábrica: SSH desligado, e o script de init diz o motivo."""
        (self.user / "settings.json").write_text(
            json.dumps({"schema_version": "1.1",
                        "developer": {"ssh_usb_enabled": False}}))
        r = self.rodar_sshd()
        self.assertEqual(r.returncode, 0, r.stderr)

        chamadas = self.calls.read_text() if self.calls.is_file() else ""
        self.assertNotIn("dropbear -p", chamadas,
                         "dropbear subiu com o Modo Desenvolvedor desligado")
        self.assertIn("desativado", chamadas.lower())


if __name__ == "__main__":
    unittest.main()
