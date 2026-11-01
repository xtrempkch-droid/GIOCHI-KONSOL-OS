#!/usr/bin/env python3
"""Catalogo de engines e recomendacao por aparelho.

Fonte unica: assets/engines.json. Este script faz duas coisas:

  1. VALIDA a coerencia entre os tres lugares onde a capacidade do aparelho
     aparece -- device/<id>/device.conf, a tabela por SoC de
     src/init/device_profile.c e os perfis manuais do giochi-forge. Se
     discordarem, o aparelho mente sobre o que pode rodar e a Loja recomenda
     engine errada.

  2. RECOMENDA: cruza a capacidade de cada aparelho com o que cada engine exige
     e diz o que roda, o que nao roda e por que. E isso que o app da Loja usa
     para propor o teto -- em vez de uma lista escrita a mao no codigo.

Uso:
    tools/check-engines.py                 # valida + matriz de todos
    tools/check-engines.py --device x55    # so um aparelho
    tools/check-engines.py --quiet         # valida em silencio (lint)
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CATALOG = ROOT / "assets" / "engines.json"
DEVICES_DIR = ROOT / "device"
DEVICE_C = ROOT / "src" / "init" / "device_profile.c"
PROFILE_PY = ROOT / "apps" / "forge" / "giochi_forge" / "profile.py"
METADATA_SCHEMA = ROOT / "schemas" / "metadata.schema.json"


def version_tuple(text: str) -> tuple[int, ...]:
    return tuple(int(p) for p in re.findall(r"\d+", str(text))[:3])


def _req_versions(text: str) -> tuple[int, ...]:
    parts = version_tuple(text)
    # "3.0" vira (3, 0); comparar com "3.1" precisa da mesma largura
    return parts + (0,) * (3 - len(parts))


class Device:
    def __init__(self, device_id: str, soc: str, driver: str,
                 gl_desktop: str, gles: str, vulkan: bool,
                 ram_mb: int | None = None):
        self.id = device_id
        self.soc = soc
        self.driver = driver
        self.gl_desktop = gl_desktop
        self.gles = gles
        self.vulkan = vulkan
        self.ram_mb = ram_mb

    @property
    def label(self) -> str:
        return f"{self.id} ({self.soc}/{self.driver}, GL {self.gl_desktop}, GLES {self.gles})"


def load_devices() -> list[Device]:
    """Le devices/<id>.conf -- a fonte de verdade por aparelho."""
    devices: list[Device] = []
    for path in sorted(DEVICES_DIR.glob("*/device.conf")):
        conf: dict[str, str] = {}
        for line in path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, _, value = line.partition("=")
            conf[key.strip()] = value.strip().strip('"')
        if "GIOCHI_GPU_DRIVER" not in conf:
            raise SystemExit(f"{path.parent.name}: falta GIOCHI_GPU_DRIVER "
                             f"(capacidade grafica do SoC)")
        devices.append(Device(
            device_id=conf.get("GIOCHI_ID", path.stem),
            soc=conf.get("GIOCHI_SOC", ""),
            driver=conf["GIOCHI_GPU_DRIVER"],
            gl_desktop=conf.get("GIOCHI_GL_DESKTOP", "2.1"),
            gles=conf.get("GIOCHI_GLES", "2.0"),
            vulkan=conf.get("GIOCHI_VULKAN", "no") in ("yes", "true", "1"),
        ))
    return devices


def soc_graphics_from_c() -> dict[str, dict]:
    """Extrai a tabela g_soc_graphics de device_profile.c."""
    text = DEVICE_C.read_text(encoding="utf-8")
    start = text.find("static const giochi_soc_graphics g_soc_graphics[]")
    if start < 0:
        raise SystemExit("device_profile.c: tabela g_soc_graphics nao encontrada")
    bloco = text[start:text.find("};", start)]
    out: dict[str, dict] = {}
    for m in re.finditer(
            r'\{\s*"([a-z0-9]+)"\s*,\s*"([a-z]+)"\s*,\s*"([0-9.]+)"\s*,'
            r'\s*"([0-9.]+)"\s*,\s*([01])\s*\}', bloco):
        out[m.group(1)] = {"driver": m.group(2), "gl_desktop": m.group(3),
                           "gles": m.group(4), "vulkan": m.group(5) == "1"}
    if not out:
        raise SystemExit("device_profile.c: nenhuma linha em g_soc_graphics")
    return out


def manual_profiles_from_py() -> dict[str, dict]:
    """Extrai driver/gles_max dos perfis manuais do forge (regex, sem importar)."""
    text = PROFILE_PY.read_text(encoding="utf-8")
    out: dict[str, dict] = {}
    bloco = text[text.find("MANUAL_PROFILES"):]
    for m in re.finditer(
            r'"soc":\s*"([a-z0-9]+)".*?"driver":\s*"([a-z]+)",\s*"gles_max":\s*"([0-9.]+)"',
            bloco, re.S):
        out.setdefault(m.group(1), {"driver": m.group(2), "gles": m.group(3)})
    return out


def check_coherence(devices: list[Device]) -> list[str]:
    """Os tres lugares que descrevem o aparelho tem de concordar."""
    erros: list[str] = []
    c_tbl = soc_graphics_from_c()

    for dev in devices:
        if dev.soc not in c_tbl:
            erros.append(f"{dev.id}: SoC '{dev.soc}' nao esta em g_soc_graphics")
            continue
        t = c_tbl[dev.soc]
        if t["driver"] != dev.driver:
            erros.append(f"{dev.id}: driver {dev.driver} no .conf x "
                         f"{t['driver']} em device_profile.c")
        if t["gl_desktop"] != dev.gl_desktop:
            erros.append(f"{dev.id}: GL desktop {dev.gl_desktop} no .conf x "
                         f"{t['gl_desktop']} em device_profile.c")
        if t["gles"] != dev.gles:
            erros.append(f"{dev.id}: GLES {dev.gles} no .conf x "
                         f"{t['gles']} em device_profile.c")

    # Lima (RK3326) nao chega ao GL 3.0 desktop: dizer "gles3: true" e' mentira
    # que aparece no perfil e faz a Loja recomendar engine que nao roda.
    for dev in devices:
        if dev.driver == "lima" and version_tuple(dev.gles) >= (3, 0):
            erros.append(f"{dev.id}: lima declara GLES {dev.gles}; o Mesa so "
                         f"entrega 2.0 no Mali-G31")

    py_tbl = manual_profiles_from_py()
    for soc, t in py_tbl.items():
        if soc not in c_tbl:
            erros.append(f"profile.py: SoC '{soc}' nao esta em g_soc_graphics")
            continue
        if t["driver"] != c_tbl[soc]["driver"]:
            erros.append(f"profile.py: {soc} com driver {t['driver']}, "
                         f"device_profile.c diz {c_tbl[soc]['driver']}")
        if t["gles"] != c_tbl[soc]["gles"]:
            erros.append(f"profile.py: {soc} com gles_max {t['gles']}, "
                         f"device_profile.c diz {c_tbl[soc]['gles']}")
    return erros


def backend_ok(device: Device, mode: dict) -> bool:
    backend = mode.get("backend", "")
    if backend == "software":
        return True
    if backend == "gles":
        return _req_versions(device.gles) >= _req_versions(mode.get("gles_min", "2.0"))
    if backend == "gl_desktop":
        return (_req_versions(device.gl_desktop)
                >= _req_versions(mode.get("gl_desktop_min", "2.1")))
    if backend == "vulkan":
        return device.vulkan
    return False


def recommend(engine: dict, device: Device) -> tuple[str, str]:
    """Devolve (veredito, motivo). Veredito: roda | parcial | nao."""
    modes = engine.get("modes", [])
    servem = [m for m in modes if backend_ok(device, m)]
    if not servem:
        pedidos = ", ".join(
            f"{m['backend']} {m.get('gles_min') or m.get('gl_desktop_min') or ''}".strip()
            for m in modes)
        return "nao", f"exige {pedidos}; aparelho tem GL {device.gl_desktop} / GLES {device.gles}"

    modo = servem[0]
    motivo = modo.get("backend", "")
    if modo.get("gles_min"):
        motivo += f" {modo['gles_min']}"
    elif modo.get("gl_desktop_min"):
        motivo += f" {modo['gl_desktop_min']}"
    if modo.get("note"):
        motivo += f" ({modo['note']})"

    # Um modo pode ser "serve, mas com ressalva" (ex.: Godot 3.5 no RK3326, quando
    # os jogos do repositorio sao 4.3). Ressalva declarada no dado, nao no codigo.
    if modo.get("partial"):
        return "parcial", f"{motivo}; {modo['partial']}"

    if device.ram_mb is not None and engine.get("ram_min_mb"):
        if device.ram_mb < engine["ram_min_mb"]:
            return "parcial", (f"{motivo}; pede {engine['ram_min_mb']} MB de RAM, "
                               f"aparelho tem {device.ram_mb}")
    if not engine.get("verified_on_device", False):
        return "parcial", f"{motivo}; nao verificado em aparelho"
    return "roda", motivo


def check_catalog(catalog: dict) -> list[str]:
    erros: list[str] = []
    schema = json.loads(METADATA_SCHEMA.read_text(encoding="utf-8"))
    runtimes = set(schema["properties"]["runtime"]["enum"])
    engines = catalog.get("engines", [])
    if not engines:
        erros.append("catalogo sem engines")

    vistos: set[str] = set()
    for eng in engines:
        eid = eng.get("id", "?")
        if eid in vistos:
            erros.append(f"{eid}: id repetido")
        vistos.add(eid)
        for campo in ("title", "license", "modes", "evidence"):
            if not eng.get(campo):
                erros.append(f"{eid}: falta '{campo}'")
        if not eng.get("modes"):
            continue
        for modo in eng["modes"]:
            b = modo.get("backend")
            if b not in catalog.get("backends", {}):
                erros.append(f"{eid}: backend '{b}' nao declarado em 'backends'")
            if b == "gles" and not modo.get("gles_min"):
                erros.append(f"{eid}: backend gles sem gles_min")
            if b == "gl_desktop" and not modo.get("gl_desktop_min"):
                erros.append(f"{eid}: backend gl_desktop sem gl_desktop_min")
        rt = eng.get("runtime")
        if rt is not None and rt not in runtimes:
            erros.append(f"{eid}: runtime '{rt}' nao existe em "
                         f"schemas/metadata.schema.json ({sorted(runtimes)})")
    return erros


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="catalogo de engines e recomendacao")
    parser.add_argument("--device", help="so este aparelho (id de devices/<id>.conf)")
    parser.add_argument("--quiet", action="store_true", help="so o resultado da validacao")
    parser.add_argument("--json", action="store_true", help="matriz em JSON")
    args = parser.parse_args(argv)

    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    erros = check_catalog(catalog)
    devices = load_devices()
    erros += check_coherence(devices)
    if erros:
        for e in erros:
            print(f"engines: {e}", file=sys.stderr)
        print(f"engines: {len(erros)} problema(s)", file=sys.stderr)
        return 1
    if args.quiet:
        print(f"ok ({len(catalog['engines'])} engine(s), {len(devices)} aparelho(s))")
        return 0

    alvo = devices
    if args.device:
        alvo = [d for d in devices if d.id == args.device]
        if not alvo:
            print(f"engines: aparelho '{args.device}' nao existe", file=sys.stderr)
            return 2

    if args.json:
        print(json.dumps({
            d.id: {e["id"]: recommend(e, d)[0] for e in catalog["engines"]}
            for d in alvo}, indent=2, ensure_ascii=False))
        return 0

    for dev in alvo:
        print(f"\n== {dev.label}")
        ordem = {"roda": 0, "parcial": 1, "nao": 2}
        linhas = sorted(((recommend(e, dev), e) for e in catalog["engines"]),
                        key=lambda r: (ordem[r[0][0]], r[1]["id"]))
        for (veredito, motivo), eng in linhas:
            marca = {"roda": "OK  ", "parcial": "~   ", "nao": "--  "}[veredito]
            print(f"  {marca}{eng['id']:<16} {motivo}")
    print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
