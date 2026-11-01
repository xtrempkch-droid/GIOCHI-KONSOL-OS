"""Testes do catalogo de engines e do recomendador por aparelho.

O que protegem, em ordem de importancia:

1. os tres lugares que descrevem a capacidade do aparelho concordam
   (devices/<id>.conf, a tabela por SoC do device_profile.c e os perfis
   manuais do forge) -- quando discordavam, o RK3326 aparecia como panfrost e
   GLES 3.2, e a Loja recomendava engine que nao roda nele;
2. um aparelho nunca e' recomendado para engine acima do seu teto;
3. somar engine nova e' acrescentar dado, nao codigo: o catalogo valida sozinho
   e o checker recusa backend/versao ausente.
"""

import json
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CHK = ROOT / "tools" / "check-engines.py"
sys.path.insert(0, str(ROOT / "tools"))


def _load():
    import importlib.util
    spec = importlib.util.spec_from_file_location("check_engines", CHK)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


ce = _load()


class DeviceCoherenceTest(unittest.TestCase):
    def test_os_tres_lugares_concordam(self):
        self.assertEqual(ce.check_coherence(ce.load_devices()), [])

    def test_todo_aparelho_declara_capacidade_grafica(self):
        for dev in ce.load_devices():
            self.assertTrue(dev.driver, f"{dev.id} sem driver")
            self.assertRegex(dev.gl_desktop, r"^\d+\.\d+$")
            self.assertRegex(dev.gles, r"^\d+\.\d+$")

    def test_lima_nao_chega_a_gles3(self):
        for dev in ce.load_devices():
            if dev.driver == "lima":
                self.assertLess(ce.version_tuple(dev.gles), (3, 0),
                                f"{dev.id}: lima so entrega GLES 2.0")

    def test_tabela_c_extraida(self):
        tbl = ce.soc_graphics_from_c()
        self.assertIn("rk3566", tbl)
        self.assertIn("rk3326", tbl)
        self.assertEqual(tbl["rk3326"]["driver"], "lima")
        self.assertEqual(tbl["rk3326"]["gles"], "2.0")


class CatalogTest(unittest.TestCase):
    def setUp(self):
        self.catalog = json.loads((ROOT / "assets" / "engines.json").read_text(encoding="utf-8"))

    def test_catalogo_valido(self):
        self.assertEqual(ce.check_catalog(self.catalog), [])

    def test_runtime_citado_existe_no_schema_de_metadata(self):
        schema = json.loads((ROOT / "schemas" / "metadata.schema.json").read_text(encoding="utf-8"))
        permitidos = set(schema["properties"]["runtime"]["enum"])
        for eng in self.catalog["engines"]:
            if eng.get("runtime") is not None:
                self.assertIn(eng["runtime"], permitidos, eng["id"])

    def test_schema_do_catalogo_aceita_o_arquivo(self):
        try:
            import jsonschema
        except ImportError:
            self.skipTest("jsonschema nao instalado")
        schema = json.loads((ROOT / "schemas" / "engines.schema.json").read_text(encoding="utf-8"))
        jsonschema.validate(self.catalog, schema)

    def test_engine_sem_modo_e_recusada(self):
        ruim = {"backends": {"software": {}}, "engines": [
            {"id": "x", "title": "X", "license": "MIT", "modes": [],
             "evidence": "nao tem"}]}
        self.assertTrue(ce.check_catalog(ruim))

    def test_backend_inexistente_e_recusado(self):
        ruim = {"backends": {"software": {}}, "engines": [
            {"id": "x", "title": "X", "license": "MIT",
             "modes": [{"backend": "magia"}], "evidence": "inventado"}]}
        erros = ce.check_catalog(ruim)
        self.assertTrue(any("magia" in e for e in erros))

    def test_runtime_inventado_e_recusado(self):
        ruim = {"backends": {"software": {}}, "engines": [
            {"id": "x", "title": "X", "license": "MIT", "runtime": "unity",
             "modes": [{"backend": "software"}], "evidence": "inventado"}]}
        erros = ce.check_catalog(ruim)
        self.assertTrue(any("unity" in e for e in erros))


class RecommendTest(unittest.TestCase):
    def setUp(self):
        self.catalog = json.loads((ROOT / "assets" / "engines.json").read_text(encoding="utf-8"))
        self.devices = {d.id: d for d in ce.load_devices()}
        self.engines = {e["id"]: e for e in self.catalog["engines"]}

    def test_godot_roda_no_rk3566(self):
        veredito, _ = ce.recommend(self.engines["godot"], self.devices["x55"])
        self.assertEqual(veredito, "roda")

    def test_godot_e_parcial_no_rk3326(self):
        veredito, motivo = ce.recommend(self.engines["godot"],
                                        self.devices["rk3326-generic"])
        self.assertEqual(veredito, "parcial")
        self.assertIn("3.5", motivo)

    def test_engine_de_gl33_nao_roda_no_panfrost(self):
        # panfrost tem GL 3.1; nada que exija 3.3 pode ser recomendado
        for eid in ("melonds",):
            veredito, _ = ce.recommend(self.engines[eid], self.devices["x55"])
            self.assertEqual(veredito, "nao", eid)

    def test_nada_acima_do_teto_em_nenhum_aparelho(self):
        for dev in self.devices.values():
            for eng in self.catalog["engines"]:
                veredito, motivo = ce.recommend(eng, dev)
                if veredito == "nao":
                    continue
                # se roda, algum modo tem de caber de fato
                servem = [m for m in eng["modes"] if ce.backend_ok(dev, m)]
                self.assertTrue(servem,
                                f"{eng['id']} recomendada em {dev.id} sem modo compativel")
                self.assertTrue(motivo)

    def test_software_roda_ate_no_lima(self):
        veredito, _ = ce.recommend(self.engines["chocolate-doom"],
                                   self.devices["rk3326-generic"])
        self.assertEqual(veredito, "parcial")  # parcial por nao ter sido verificado

    def test_verificado_em_aparelho_vira_roda(self):
        eng = {"id": "x", "modes": [{"backend": "software"}],
               "verified_on_device": True}
        veredito, _ = ce.recommend(eng, self.devices["rk3326-generic"])
        self.assertEqual(veredito, "roda")


class CliTest(unittest.TestCase):
    def test_quiet_sai_com_zero(self):
        r = subprocess.run([sys.executable, str(CHK), "--quiet"],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("ok", r.stdout)

    def test_aparelho_inexistente_sai_com_2(self):
        r = subprocess.run([sys.executable, str(CHK), "--device", "nao-existe"],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 2)

    def test_json_tem_todos_os_aparelhos(self):
        r = subprocess.run([sys.executable, str(CHK), "--json"],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        dados = json.loads(r.stdout)
        self.assertEqual(set(dados), {d.id for d in ce.load_devices()})


if __name__ == "__main__":
    unittest.main()
