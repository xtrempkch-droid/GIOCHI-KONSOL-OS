"""O app de loja do PC: catalogo, novidades e instalacao no cartao.

O que este teste protege e' a parte que ja' falhou em outros lugares deste
repositorio:

  * `.giochi` invalido aceito calado (ADR-13 proibe x86_64, e um pacote x86 no
    cartao so' falha quando o usuario tenta abrir);
  * relatório "falhou" sem motivo — o `forge store` ja' tinha aprendido que o
    motivo e' o produto;
  * copia para o cartao sem conferir o DESTINO: o `dd` termina com sucesso
    mesmo quando a midia devolve lixo (§3.42), e `shutil.copy` idem.
"""

from __future__ import annotations

import io
import json
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "apps" / "store"))

from giochi_store.catalogo import (  # noqa: E402
    catalogo,
    ler_metadata,
    novidades,
    validar,
    varrer,
)
from giochi_store.destino import Destino, _tem_assinatura  # noqa: E402

META_OK = {
    "id": "jogo-bom",
    "title": "Jogo Bom",
    "version": "1.0.0",
    "arch": "aarch64",
    "runtime": "godot",
    "entrypoint": "bin/jogo",
    "author": "eu",
    "description": "teste",
}


def escrever_pacote(destino: Path, meta: dict | None,
                    ordem: str = "normal") -> Path:
    """Monta um `.giochi` de teste (tar com metadata.json)."""
    destino.parent.mkdir(parents=True, exist_ok=True)
    membros = []
    if meta is not None:
        dados = json.dumps(meta).encode()
        membros.append(("metadata.json", dados))
    membros.append(("capa.png", b"PNG"))
    membros.append(("bin/jogo", b"#!/bin/sh\nexit 0\n"))
    if ordem == "capa_primeiro":
        membros = [membros[1], membros[0]] + membros[2:]

    with tarfile.open(destino, "w") as tar:
        for nome, dados in membros:
            info = tarfile.TarInfo(nome)
            info.size = len(dados)
            tar.addfile(info, io.BytesIO(dados))
    return destino


class ValidacaoTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_pacote_valido(self):
        p = validar(escrever_pacote(self.dir / "bom.giochi", META_OK))

        self.assertTrue(p.ok, p.motivo)
        self.assertEqual(p.id, "jogo-bom")
        self.assertEqual(p.titulo, "Jogo Bom")
        self.assertEqual(len(p.sha256), 64)

    def test_arquitetura_errada_e_recusa_com_motivo(self):
        meta = dict(META_OK, arch="x86_64", id="ruim")
        p = validar(escrever_pacote(self.dir / "ruim.giochi", meta))

        self.assertFalse(p.ok)
        self.assertIn("x86_64", p.motivo)
        self.assertIn("ADR-13", p.motivo)

    def test_runtime_fora_do_enum_e_recusa(self):
        meta = dict(META_OK, runtime="nativo")
        p = validar(escrever_pacote(self.dir / "r.giochi", meta))

        self.assertFalse(p.ok)
        self.assertIn("runtime", p.motivo)

    def test_campo_obrigatorio_ausente_diz_qual(self):
        for campo in ("id", "title", "version", "entrypoint"):
            meta = dict(META_OK)
            del meta[campo]
            p = validar(escrever_pacote(self.dir / f"{campo}.giochi", meta))

            self.assertFalse(p.ok)
            self.assertIn(campo, p.motivo,
                          f"o motivo nao cita o campo que falta: {p.motivo}")

    def test_entrypoint_fora_de_bin_e_recusa(self):
        for entry in ("jogo", "bin/../etc/passwd", "/bin/jogo"):
            meta = dict(META_OK, entrypoint=entry)
            p = validar(escrever_pacote(self.dir / "e.giochi", meta))

            self.assertFalse(p.ok)
            self.assertIn("entrypoint", p.motivo)

    def test_sem_metadata_e_recusa(self):
        p = validar(escrever_pacote(self.dir / "sem.giochi", None))

        self.assertFalse(p.ok)
        self.assertIn("metadata", p.motivo)

    def test_metadata_fora_de_primeiro_lugar_e_recusa(self):
        # ADR-11 exige metadata.json primeiro: o `giochi-extract` do console
        # le em streaming e nao pode varrer o tar inteiro.
        p = validar(escrever_pacote(self.dir / "ordem.giochi", META_OK,
                                    ordem="capa_primeiro"))

        self.assertFalse(p.ok)
        self.assertIn("primeiro membro", p.motivo)

    def test_arquivo_que_nao_e_tar_e_recusa_sem_excecao(self):
        ruim = self.dir / "lixo.giochi"
        ruim.write_bytes(b"isto nao e' um tar")

        p = validar(ruim)
        self.assertFalse(p.ok)
        self.assertIn("ilegivel", p.motivo)

    def test_ler_metadata_devolve_dicionario(self):
        meta, erro = ler_metadata(escrever_pacote(self.dir / "m.giochi", META_OK))

        self.assertEqual(erro, "")
        self.assertEqual(meta["id"], "jogo-bom")


class CatalogoTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        escrever_pacote(self.dir / "bom.giochi", META_OK)
        escrever_pacote(self.dir / "ruim.giochi",
                        dict(META_OK, id="ruim", arch="x86_64"))
        self.pacotes = varrer(self.dir)

    def tearDown(self):
        self.tmp.cleanup()

    def test_so_pacote_valido_entra_no_catalogo(self):
        cat = catalogo(self.pacotes)

        self.assertEqual(cat["total"], 1)
        self.assertEqual(cat["itens"][0]["id"], "jogo-bom")
        self.assertIn("sha256", cat["itens"][0])

    def test_catalogo_tem_versao_de_schema(self):
        cat = catalogo(self.pacotes)

        self.assertEqual(cat["schema_version"], "1.0")
        self.assertIn("gerado_em", cat)

    def test_novidades_tem_o_formato_que_o_launcher_le(self):
        # O Launcher le {"items":[{"title","body"}]} em /user/.cache/news.json
        # (src/launcher/main.cpp, carregar_novidade).
        novo = novidades(self.pacotes)

        self.assertIn("items", novo)
        self.assertEqual(novo["schema_version"], "1.0")
        self.assertEqual(novo["items"][0]["title"], "Jogo Bom")
        # O corpo traz versao, tamanho e runtime numa linha so'.
        self.assertIn("1.0.0", novo["items"][0]["body"])
        self.assertIn("KB", novo["items"][0]["body"])

    def test_pasta_vazia_nao_escreve_arquivo_sem_itens(self):
        novo = novidades([])

        self.assertEqual(len(novo["items"]), 1)
        self.assertEqual(novo["items"][0]["kind"], "dica")

    def test_ordem_do_catalogo_e_por_titulo(self):
        escrever_pacote(self.dir / "aaa.giochi",
                        dict(META_OK, id="aaa", title="AAA Primeiro"))
        cat = catalogo(varrer(self.dir))
        titulos = [i["title"] for i in cat["itens"]]

        self.assertEqual(titulos, sorted(titulos, key=str.lower))


class InstalacaoTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        escrever_pacote(self.dir / "src" / "bom.giochi", META_OK)
        escrever_pacote(self.dir / "src" / "ruim.giochi",
                        dict(META_OK, id="ruim", arch="x86_64"))

    def tearDown(self):
        self.tmp.cleanup()

    def _cli(self):
        sys.path.insert(0, str(ROOT / "apps" / "store"))
        from giochi_store.cli import instalar
        return instalar

    def test_dry_run_nao_escreve_nada(self):
        games = self.dir / "games"
        copiados, falhas = self._cli()(varrer(self.dir / "src"), games, True)

        self.assertFalse(games.exists(), "dry-run criou o diretorio")
        self.assertEqual(copiados, 1)
        self.assertEqual(falhas, 1)

    def test_destino_sem_escrita_falha_com_instrucao(self):
        """Cartão montado pelo udisks é do root: a mensagem tem de dizer o que
        fazer, não despejar PermissionError com traceback."""
        from giochi_store.cli import SemEscrita, instalar

        somente_leitura = self.dir / "ro"
        somente_leitura.mkdir()
        somente_leitura.chmod(0o500)          # sem escrita para o dono
        try:
            with self.assertRaises(SemEscrita) as ctx:
                instalar(varrer(self.dir / "src"), somente_leitura, False)
            texto = str(ctx.exception)
            self.assertIn("nao aceita escrita", texto)
            self.assertIn("MTP", texto, "a mensagem precisa sugerir a saida")
        finally:
            somente_leitura.chmod(0o700)

    def test_copia_conferindo_o_destino(self):
        games = self.dir / "games"
        copiados, falhas = self._cli()(varrer(self.dir / "src"), games, False)

        self.assertEqual((copiados, falhas), (1, 1))
        self.assertTrue((games / "bom.giochi").is_file())
        self.assertFalse((games / "ruim.giochi").exists(),
                         "pacote recusado foi copiado para o cartao")


class DestinoTest(unittest.TestCase):
    """Descoberta do ponto de cópia: cartão no leitor ou MTP pelo cabo.

    O nome do ponto de montagem do MTP muda com bus/device/vendor string, então
    a descoberta é por CONTEÚDO (`games/` ou `.giochi/`), nunca por nome. Um
    caminho errado aqui significa copiar 40 MB para o lugar errado sem erro.
    """

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_assinatura_por_games(self):
        (self.dir / "games").mkdir()
        self.assertTrue(_tem_assinatura(self.dir))

    def test_assinatura_por_giochi(self):
        (self.dir / ".giochi").mkdir()
        self.assertTrue(_tem_assinatura(self.dir))

    def test_diretorio_vazio_nao_e_destino(self):
        self.assertFalse(_tem_assinatura(self.dir))

    def test_arquivo_nao_e_diretorio(self):
        (self.dir / "games").write_text("nao e diretorio")
        self.assertFalse(_tem_assinatura(self.dir))

    def test_caminho_inexistente_nao_explode(self):
        self.assertFalse(_tem_assinatura(self.dir / "nao-existe"))

    def test_destino_expoe_games(self):
        d = Destino(self.dir, "cartao")
        self.assertEqual(str(d), f"cartao: {self.dir}")


if __name__ == "__main__":
    unittest.main()
