"""Testes da fonte GitHub Releases (suporte ao formato do GithubLauncher).

O que estes testes protegem:

1. o `apps.json` do GithubLauncher e' lido como esta' -- e' o que permite ao
   nosso app "dar suporte" aquele projeto sem rodar o binario .NET dele;
2. nenhum asset de arquitetura errada e' escolhido: o aparelho e' aarch64, e
   `armv7`/`armhf`/`x86` tem de ser recusados, com motivo;
3. formato extraivel ganha de pacote de distro (nao temos dpkg/rpm);
4. o ELF e' conferido no arquivo, nao no nome (nome mente).

Tudo com dados sinteticos: nenhum teste toca a rede.
"""

from __future__ import annotations

import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "apps" / "store"))

from giochi_store import fonte  # noqa: E402


def _asset(nome: str, tamanho: int = 1024) -> dict:
    return {"name": nome, "size": tamanho,
            "browser_download_url": f"https://example.invalid/{nome}"}


def _release(tag: str, nomes: list[str]) -> dict:
    return {"tag_name": tag, "assets": [_asset(n) for n in nomes]}


class AppsJsonTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def _escrever(self, doc) -> Path:
        caminho = self.dir / "apps.json"
        caminho.write_text(json.dumps(doc), encoding="utf-8")
        return caminho

    def test_formato_do_githublauncher(self):
        caminho = self._escrever({"apps": [
            {"name": "TIC-80", "repository": "nesbox/TIC-80",
             "folderName": "tic80", "appIconUrl": None},
            {"name": "Godot", "repository": "godotengine/godot",
             "folderName": "godot", "appIconUrl": "https://x/y.png"},
        ]})
        entradas = fonte.ler_apps_json(caminho)
        self.assertEqual(len(entradas), 2)
        self.assertEqual(entradas[0].nome, "TIC-80")
        self.assertEqual(entradas[0].repositorio, "nesbox/TIC-80")
        self.assertEqual(entradas[0].pasta, "tic80")
        self.assertIsNone(entradas[0].icone)
        self.assertEqual(entradas[1].icone, "https://x/y.png")

    def test_campo_extra_nao_quebra(self):
        # O launcher pode acrescentar campos; ler o que interessa e guardar o
        # resto e' mais util do que recusar a lista inteira.
        caminho = self._escrever({"apps": [
            {"name": "A", "repository": "a/b", "categoria": "nova"}]})
        entradas = fonte.ler_apps_json(caminho)
        self.assertEqual(entradas[0].bruto["categoria"], "nova")

    def test_lista_simples_de_repositorios(self):
        caminho = self._escrever(["nesbox/TIC-80",
                                  "https://github.com/dhewm/dhewm3.git"])
        entradas = fonte.ler_apps_json(caminho)
        self.assertEqual([e.repositorio for e in entradas],
                         ["nesbox/TIC-80", "dhewm/dhewm3"])

    def test_entrada_sem_repositorio_e_erro(self):
        caminho = self._escrever({"apps": [{"name": "sem repo"}]})
        with self.assertRaises(fonte.FonteError):
            fonte.ler_apps_json(caminho)

    def test_json_quebrado_e_erro(self):
        caminho = self.dir / "apps.json"
        caminho.write_text("{nao e json", encoding="utf-8")
        with self.assertRaises(fonte.FonteError):
            fonte.ler_apps_json(caminho)

    def test_normaliza_url_e_owner_repo(self):
        self.assertEqual(fonte.normalizar_repositorio("a/b"), "a/b")
        self.assertEqual(fonte.normalizar_repositorio("https://github.com/a/b"),
                         "a/b")
        self.assertEqual(fonte.normalizar_repositorio("https://github.com/a/b.git"),
                         "a/b")
        with self.assertRaises(fonte.FonteError):
            fonte.normalizar_repositorio("nao-e-repositorio")


class EscolherAssetTest(unittest.TestCase):
    def test_escolhe_arm64_entre_varios(self):
        asset, motivo = fonte.escolher_asset([
            _asset("App-1.0-Windows.zip"),
            _asset("App-1.0-Linux-X64.tar.gz"),
            _asset("App-1.0-Linux-ARM64.tar.gz"),
        ])
        self.assertIsNotNone(asset)
        self.assertEqual(asset["name"], "App-1.0-Linux-ARM64.tar.gz")
        self.assertIn("ARM64", motivo)

    def test_recusa_arm_de_32_bits(self):
        for nome in ("App-armv7-linux.tar.gz", "App-armhf-linux.tar.gz",
                     "App-linux-arm32.tar.gz"):
            asset, motivo = fonte.escolher_asset([_asset(nome)])
            self.assertIsNone(asset, nome)
            self.assertTrue("32 bits" in motivo or "arm64" in motivo, motivo)

    def test_arm_64_com_hifen_nao_e_confundido_com_arm32(self):
        # "arm-" esta' no marcador de 32 bits e casaria dentro de "arm-64":
        # foi um bug de verdade, e este teste existe para nao voltar.
        asset, _ = fonte.escolher_asset([_asset("App-arm-64-linux.tar.gz")])
        self.assertIsNotNone(asset)

    def test_recusa_x86(self):
        asset, motivo = fonte.escolher_asset([_asset("App-linux-x86_64.tar.gz")])
        self.assertIsNone(asset)
        self.assertIn("x86", motivo)

    def test_recusa_outras_plataformas(self):
        for nome in ("App-win64.zip", "App-macos.tar.gz", "App.apk"):
            asset, _ = fonte.escolher_asset([_asset(nome)])
            self.assertIsNone(asset, nome)

    def test_extraivel_ganha_de_pacote_de_distro(self):
        # Medido no teste real: a primeira versao pegou o .deb do TIC-80 so'
        # porque vinha antes no alfabeto. Nao temos dpkg no aparelho.
        asset, _ = fonte.escolher_asset([
            _asset("tic80-linux-arm64.deb"),
            _asset("tic80-linux-arm64.zip"),
        ])
        self.assertEqual(asset["name"], "tic80-linux-arm64.zip")

    def test_aarch64_explicito_ganha_de_arm64(self):
        asset, _ = fonte.escolher_asset([
            _asset("app-arm64-linux.tar.gz"),
            _asset("app-aarch64-linux.tar.gz"),
        ])
        self.assertEqual(asset["name"], "app-aarch64-linux.tar.gz")

    def test_release_sem_asset_util_explica(self):
        asset, motivo = fonte.escolher_asset([_asset("app-win64.zip")])
        self.assertIsNone(asset)
        self.assertTrue(motivo)

    def test_asset_sem_url_e_ignorado(self):
        asset, motivo = fonte.escolher_asset([
            {"name": "app-linux-arm64.tar.gz"}])
        self.assertIsNone(asset)
        self.assertIn("sem assets", motivo)


class VerificarElfTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def _elf(self, machine: int, classe: int = 2) -> Path:
        cabecalho = bytearray(64)
        cabecalho[0:4] = b"\x7fELF"
        cabecalho[4] = classe
        struct.pack_into("<H", cabecalho, 18, machine)
        caminho = self.dir / f"bin-{machine}-{classe}"
        caminho.write_bytes(bytes(cabecalho))
        return caminho

    def test_aarch64_passa(self):
        ok, detalhe = fonte.verificar_elf(self._elf(0xB7))
        self.assertTrue(ok)
        self.assertIn("AArch64", detalhe)

    def test_x86_64_falha(self):
        ok, detalhe = fonte.verificar_elf(self._elf(0x3E))
        self.assertFalse(ok)
        self.assertIn("0x3E", detalhe)

    def test_elf32_falha(self):
        ok, detalhe = fonte.verificar_elf(self._elf(0xB7, classe=1))
        self.assertFalse(ok)
        self.assertIn("32 bits", detalhe)

    def test_arquivo_que_nao_e_elf(self):
        caminho = self.dir / "pacote.zip"
        caminho.write_bytes(b"PK\x03\x04" + b"\x00" * 60)
        ok, detalhe = fonte.verificar_elf(caminho)
        self.assertFalse(ok)
        self.assertIn("extraia", detalhe)


class ResolverTest(unittest.TestCase):
    def test_resolve_com_cliente_falso(self):
        entradas = [fonte.AppEntry(nome="A", repositorio="dono/a"),
                    fonte.AppEntry(nome="B", repositorio="dono/b")]

        def cliente(repo: str) -> dict:
            if repo == "dono/a":
                return _release("v1", ["a-linux-arm64.tar.gz"])
            return _release("v2", ["b-win64.zip"])

        resultados = fonte.resolver(entradas, cliente=cliente)
        self.assertTrue(resultados[0].ok)
        self.assertEqual(resultados[0].tag, "v1")
        self.assertFalse(resultados[1].ok)
        self.assertTrue(resultados[1].motivo)

    def test_erro_de_api_vira_linha_do_relatorio(self):
        entradas = [fonte.AppEntry(nome="X", repositorio="dono/x")]

        def cliente(repo: str) -> dict:
            raise fonte.FonteError("sem releases")

        resultados = fonte.resolver(entradas, cliente=cliente)
        self.assertFalse(resultados[0].ok)
        self.assertIn("sem releases", resultados[0].motivo)

    def test_relatorio_conta(self):
        entradas = [fonte.AppEntry(nome="A", repositorio="dono/a")]
        resultados = fonte.resolver(
            entradas, cliente=lambda r: _release("v1", ["a-linux-arm64.zip"]))
        texto = fonte.relatorio(resultados)
        self.assertIn("1 com build aarch64", texto)


if __name__ == "__main__":
    unittest.main()
