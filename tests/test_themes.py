"""Catalogo de temas: schema, contraste, paridade e efeito real no layout.

POR QUE ISSO EXISTE
-------------------
As cinco propostas de layout do mantenedor (docs/TEMAS.md) sao DADO, e dado
errado nao da erro: dá tela feia. Tres classes de defeito ja' aconteceram neste
repositorio e todas passariam por "o JSON e' valido":

  * token que ninguem le — o tema declara `tile_border` e o render ignora, e o
    autor do tema jura que mudou algo;
  * cor ilegivel — texto secundario quase igual ao fundo passa em qualquer
    revisao visual de imagem pequena, e falha no painel;
  * densidade decorativa — se dois valores de `density` produzem a MESMA grade,
    a opcao de layout nao existe, so' o nome muda.

Os testes abaixo cobram os tres, e cobram contra o binario real
(build/host/bin/dump-geometry), nao contra uma copia da regra.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEMAS = ROOT / "assets" / "themes"
SCHEMA = ROOT / "schemas" / "theme.schema.json"
DUMP = ROOT / "build" / "host" / "bin" / "dump-geometry"

CORES = ("background", "surface", "surface_elevated", "text_primary",
         "text_secondary", "hint_bar_bg", "divider", "accent")


def carregar(nome: str) -> dict:
    return json.loads((TEMAS / nome).read_text(encoding="utf-8"))


def catalogo() -> dict:
    return carregar("index.json")


def luminancia(hexcor: str) -> float:
    """Luminancia relativa (WCAG 2.1, §1.4.3)."""
    r, g, b = (int(hexcor[i:i + 2], 16) / 255.0 for i in (1, 3, 5))
    lin = [c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
           for c in (r, g, b)]
    return 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2]


def contraste(a: str, b: str) -> float:
    la, lb = luminancia(a), luminancia(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


def geometria(theme_id: str, size: str = "1280x720", **extra) -> dict:
    if not DUMP.is_file():
        raise unittest.SkipTest("dump-geometry nao compilado (make all)")
    cmd = [str(DUMP), "--size", size, "--tab", "1", "--items", "15",
           "--theme", theme_id, "--assets", str(ROOT / "assets")]
    for chave, valor in extra.items():
        cmd += [f"--{chave.replace('_', '-')}", str(valor)]
    return json.loads(subprocess.check_output(cmd, text=True))


class SchemaTest(unittest.TestCase):
    def test_schema_existe_e_cobre_as_metricas(self):
        doc = json.loads(SCHEMA.read_text(encoding="utf-8"))
        campos = doc["properties"]["metrics"]["properties"]

        for token in ("tile_corner_radius", "tile_border", "focus_ring",
                      "density"):
            self.assertIn(token, campos,
                          f"token '{token}' nao esta no schema do tema")

    def test_todos_os_temas_tem_os_campos_obrigatorios(self):
        obrigatorios = set(json.loads(SCHEMA.read_text())["required"])

        for item in catalogo()["themes"]:
            tema = carregar(item["file"])
            self.assertTrue(obrigatorios <= set(tema),
                            f"{item['file']}: faltam "
                            f"{sorted(obrigatorios - set(tema))}")

    def test_cores_em_hex_de_seis_digitos(self):
        padrao = re.compile(r"^#[0-9A-Fa-f]{6}$")

        for item in catalogo()["themes"]:
            tema = carregar(item["file"])
            for nome in CORES:
                self.assertRegex(tema["colors"][nome], padrao,
                                 f"{item['file']}: cor '{nome}'")

    def test_nenhum_token_de_efeito_inexistente(self):
        """Token que o desenho nao honra faz o autor do tema acreditar que mudou
        algo. Os dois que existiam e nao faziam nada:
          title_scale_pct — o atlas tem 12/14/18/21 px e a face mais proxima
                            devolve o mesmo desenho;
          cover_pct       — a aba Musica ainda desenha painel de mensagem, sem
                            arte de album.
        Se um deles voltar, este teste avisa."""
        for item in catalogo()["themes"]:
            m = carregar(item["file"])["metrics"]
            for morto in ("title_scale_pct", "cover_pct"):
                self.assertNotIn(morto, m,
                                 f"{item['file']}: '{morto}' nao tem efeito")

    def test_metricas_dentro_da_faixa_do_schema(self):
        faixas = {
            "tile_corner_radius": (0, 24),
            "tile_border": (0, 4),
            "focus_ring": (0, 6),
        }

        for item in catalogo()["themes"]:
            m = carregar(item["file"])["metrics"]
            for campo, (lo, hi) in faixas.items():
                valor = m[campo]
                self.assertTrue(lo <= valor <= hi,
                                f"{item['file']}: {campo}={valor} fora de "
                                f"[{lo}, {hi}]")

    def test_densidade_do_enum(self):
        for item in catalogo()["themes"]:
            dens = carregar(item["file"])["metrics"]["density"]
            self.assertIn(dens, ("compacta", "normal", "espacosa"),
                          f"{item['file']}: densidade '{dens}'")


class CatalogoTest(unittest.TestCase):
    def test_indice_e_temas_concordam(self):
        cat = catalogo()
        ids = [t["theme_id"] for t in cat["themes"]]

        self.assertEqual(len(ids), len(set(ids)), "theme_id repetido")
        self.assertIn(cat["default"], ids,
                      "o padrao do indice nao esta na lista")

        arquivos = {p.stem for p in TEMAS.glob("*.json")} - {"index"}
        self.assertEqual(set(ids), arquivos,
                         "ha tema no disco fora do indice (ou o contrario)")

    def test_nome_unico_e_legivel(self):
        nomes = [carregar(t["file"])["name"] for t in catalogo()["themes"]]

        self.assertEqual(len(nomes), len(set(nomes)), "nome de tema repetido")
        for nome in nomes:
            self.assertGreaterEqual(len(nome), 3)
            self.assertLessEqual(len(nome), 48)

    def test_id_de_cada_arquivo_bate_com_o_indice(self):
        for item in catalogo()["themes"]:
            tema = carregar(item["file"])
            self.assertEqual(tema["theme_id"], item["theme_id"],
                             f"{item['file']}: theme_id divergente")

    def test_galeria_existe_para_cada_tema(self):
        for item in catalogo()["themes"]:
            for tam in ("1280x720", "640x480"):
                img = ROOT / "docs" / "temas" / f"{item['theme_id']}-{tam}.png"
                self.assertTrue(img.is_file(),
                                f"falta a imagem {img.relative_to(ROOT)} "
                                f"(rode 'make tema-galeria')")


class ContrasteTest(unittest.TestCase):
    """Piso de legibilidade medido, nao "parece ok"."""

    # 3:1 e' o minimo WCAG para texto grande / elemento de UI; 4,5:1 para
    # corpo de texto. O texto primario aqui e' rotulo de 18-21 px num painel
    # de 5 polegadas, entao 4,5:1 e' o alvo e o secundario fica em 3:1.
    MIN_PRIMARIO = 4.5
    MIN_SECUNDARIO = 3.0

    def test_texto_primario_sobre_as_superficies(self):
        for item in catalogo()["themes"]:
            c = carregar(item["file"])["colors"]
            for fundo in ("background", "surface", "surface_elevated"):
                r = contraste(c["text_primary"], c[fundo])
                self.assertGreaterEqual(
                    r, self.MIN_PRIMARIO,
                    f"{item['theme_id']}: text_primary sobre {fundo} = "
                    f"{r:.2f}:1 (minimo {self.MIN_PRIMARIO})")

    def test_texto_secundario_sobre_as_superficies(self):
        for item in catalogo()["themes"]:
            c = carregar(item["file"])["colors"]
            for fundo in ("background", "surface", "surface_elevated"):
                r = contraste(c["text_secondary"], c[fundo])
                self.assertGreaterEqual(
                    r, self.MIN_SECUNDARIO,
                    f"{item['theme_id']}: text_secondary sobre {fundo} = "
                    f"{r:.2f}:1 (minimo {self.MIN_SECUNDARIO})")

    def test_acento_visivel_sobre_o_fundo_e_sobre_a_superficie(self):
        # O acento desenha o sublinhado da aba ativa e o anel de foco: se ele
        # sumir no fundo, o usuario perde a referencia de onde esta'.
        for item in catalogo()["themes"]:
            c = carregar(item["file"])["colors"]
            for fundo in ("background", "surface"):
                r = contraste(c["accent"], c[fundo])
                self.assertGreaterEqual(
                    r, 1.35,
                    f"{item['theme_id']}: accent sobre {fundo} = {r:.2f}:1")

    def test_hint_bar_distingue_do_conteudo(self):
        # A barra de dicas tem de ser um bloco visivel, nao a mesma cor do
        # fundo: sem isso o "chip" do botao flutua no vazio.
        for item in catalogo()["themes"]:
            c = carregar(item["file"])["colors"]
            self.assertNotEqual(c["hint_bar_bg"], c["background"],
                                f"{item['theme_id']}: hint_bar_bg == background")


class EfeitoNoLayoutTest(unittest.TestCase):
    """O tema e' aplicado pelo BINARIO, e o efeito tem de ser visivel."""

    def test_tema_troca_cor_e_metrica(self):
        a = geometria("dark_default")
        b = geometria("neon_rose")

        self.assertEqual(a["theme"]["colors"]["accent"], "#E50914")
        self.assertEqual(b["theme"]["colors"]["accent"], "#F43F5E")
        self.assertNotEqual(a["tokens"]["corner"], b["tokens"]["corner"],
                            "raio do canto nao mudou entre os temas")

    def test_densidade_muda_a_grade_de_verdade(self):
        """Tres densidades, tres grades. Se duas coincidirem, a opcao e' falsa."""
        contagem = {}
        for tid in ("mono_minimal", "dark_default", "ember_dark"):
            g = geometria(tid)
            contagem[tid] = (g["tokens"]["columns"], g["tokens"]["rows"])
        valores = list(contagem.values())

        self.assertEqual(len(set(valores)), 3,
                         f"densidades colapsaram na mesma grade: {contagem}")
        # Compacta cabe mais que espacosa nos dois eixos.
        compacta = contagem["mono_minimal"]
        espacosa = contagem["ember_dark"]
        self.assertGreater(compacta[0], espacosa[0])
        self.assertGreaterEqual(compacta[1], espacosa[1])

    def test_densidade_muda_tambem_no_painel_pequeno(self):
        contagem = {}
        for tid in ("mono_minimal", "dark_default", "ember_dark"):
            g = geometria(tid, "640x480")
            contagem[tid] = (g["tokens"]["columns"], g["tokens"]["rows"])

        self.assertEqual(len(set(contagem.values())), 3,
                         f"em 640x480 as densidades coincidem: {contagem}")

    def test_borda_do_tema_aparece_no_tile_comum(self):
        com = geometria("neon_rose")      # tile_border 1
        sem = geometria("dark_default")   # tile_border 0

        self.assertGreater(com["theme"]["border"], 0)
        self.assertEqual(sem["theme"]["border"], 0)

        # Role DIVIDER (4) no outline do tile NAO focado.
        nao_focado_com = com["tiles"][1]["outline"]
        nao_focado_sem = sem["tiles"][1]["outline"]
        self.assertEqual(nao_focado_com, 4,
                         "tema com borda nao desenhou contorno no tile comum")
        self.assertEqual(nao_focado_sem, 0,
                         "tema sem borda desenhou contorno no tile comum")

    def test_modo_de_dicas_muda_a_hint_bar(self):
        com = geometria("neon_rose")       # features.hints = completo
        sem = geometria("mono_gamepad")    # features.hints = so_gamepad

        self.assertEqual(com["theme"]["hints"], "completo")
        self.assertEqual(sem["theme"]["hints"], "so_gamepad")
        self.assertIn("Setas", com["hint"])
        self.assertNotIn("Setas", sem["hint"])

    def test_tema_inexistente_nao_derruba_o_dump(self):
        # Um id errado tem de cair no padrao com mensagem, nao em geometria
        # vazia — o preview roda isso em lote.
        saida = subprocess.run(
            [str(DUMP), "--size", "1280x720", "--theme", "nao_existe",
             "--assets", str(ROOT / "assets")],
            capture_output=True, text=True)
        self.assertEqual(saida.returncode, 0, saida.stderr)
        doc = json.loads(saida.stdout)
        self.assertGreater(doc["tokens"]["columns"], 0)


class MenuSistemaTest(unittest.TestCase):
    """As 7 linhas do submenu Sistema têm de caber em TODO tema e painel.

    A 7ª linha (Tema) entrou depois e o render tem um `break` silencioso quando
    a linha não cabe — a última opção simplesmente não apareceria, sem erro.
    O cálculo usa a face do atlas que o render vai usar de fato (line_height
    do font.json), não uma estimativa.
    """

    def test_sete_linhas_cabem_em_todos_os_temas_e_paineis(self):
        if not DUMP.is_file():
            raise unittest.SkipTest("dump-geometry nao compilado")
        font = json.loads((ROOT / "assets" / "font.json").read_text())
        faces = {(f["px"], f["weight"]): f for f in font["sizes"]}

        for item in catalogo()["themes"]:
            for size in ("1280x720", "640x480"):
                g = geometria(item["theme_id"], size, settings=1, level=1)
                t, tb, hb = g["tokens"], g["topbar"], g["hint_bar"]
                margin = t["margin"]
                panel_y = tb["h"] + margin
                panel_h = (hb["y"] - tb["h"]) - 2 * margin
                titulo = faces[(t["font_tab"], "bold")]
                linha = faces[(t["font_tab"], "regular")]
                y = (panel_y + margin + titulo["ascent"] +
                     titulo["line_height"] + t["gap"])
                row_h = linha["line_height"] + t["gap"]
                limite = panel_y + panel_h - margin
                cabem = max(0, int((limite - y) / row_h) + 1)

                self.assertGreaterEqual(
                    cabem, 7,
                    f"{item['theme_id']} em {size}: cabem {cabem} linhas, o "
                    f"submenu Sistema tem 7 e a 7a seria omitida sem aviso")

    def test_o_dump_publica_a_mesma_lista_do_overlay(self):
        """A lista estava duplicada: o preview mostrava 6 linhas, o aparelho 7."""
        if not DUMP.is_file():
            raise unittest.SkipTest("dump-geometry nao compilado")
        g = geometria("dark_default", settings=1, level=1, index=6)
        itens = g["settings"]["items"]

        self.assertEqual(len(itens), 7, "submenu Sistema com numero errado")
        self.assertEqual(itens[-1]["label"], "Tema",
                         "a ultima linha do submenu tem de ser o Tema")


class PrecedenciaTest(unittest.TestCase):
    """Qual tema vale: --theme > GIOCHI_THEME > settings.json > aparelho > padrao.

    Medido antes de existir este teste: com `settings.json` ausente, o
    `theme_id` do default COMPILADO (dark_default) vencia o tema de fabrica do
    aparelho — a X55 subia em dark_default com neon_rose gravado na imagem.
    """

    LAUNCHER = ROOT / "build" / "host" / "bin" / "giochi-launcher"

    # A suite le a linha "tema: ..." do log, e o runner exporta
    # GIOCHI_LOG_LEVEL=error (tests/run_tests.sh) — sem isto o teste passa
    # sozinho e falha dentro do `make test`, que foi exatamente o que
    # aconteceu. Nivel fixado AQUI, nao herdado do ambiente.
    ENV = dict(os.environ, GIOCHI_LOG_LEVEL="info")

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.user = self.dir / "user"
        self.user.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def _rodar(self, *extra):
        if not self.LAUNCHER.is_file():
            raise unittest.SkipTest("giochi-launcher nao compilado")
        r = subprocess.run(
            [str(self.LAUNCHER), "--headless", "--frames", "1",
             "--asset-dir", str(ROOT / "assets"),
             "--user-dir", str(self.user), *extra],
            capture_output=True, text=True, env=self.ENV)
        self.assertEqual(r.returncode, 0, r.stderr)
        m = re.search(r"tema: '([a-z_]+)'", r.stderr)
        self.assertIsNotNone(m, r.stderr)
        return m.group(1)

    def test_sem_settings_vale_o_padrao_do_catalogo(self):
        # Sem device-theme.json e sem settings.json: padrao do indice.
        self.assertEqual(self._rodar(), catalogo()["default"])

    def test_cli_vence_tudo(self):
        self.assertEqual(self._rodar("--theme", "ios_glass"), "ios_glass")

    def test_settings_vence_o_padrao(self):
        (self.user / "settings.json").write_text(
            json.dumps({"appearance": {"theme_id": "mono_gamepad"}}),
            encoding="utf-8")
        self.assertEqual(self._rodar(), "mono_gamepad")

    def test_tema_inexistente_avisa_e_nao_derruba(self):
        r = subprocess.run(
            [str(self.LAUNCHER), "--headless", "--frames", "1",
             "--asset-dir", str(ROOT / "assets"), "--user-dir", str(self.user),
             "--theme", "nao_existe"],
            capture_output=True, text=True, env=self.ENV)
        self.assertEqual(r.returncode, 0)
        self.assertIn("nao existe no catalogo", r.stderr)
        self.assertIn(catalogo()["default"], r.stderr)


if __name__ == "__main__":
    unittest.main()
