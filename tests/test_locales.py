#!/usr/bin/env python3
"""Catalogo de idiomas: paridade, cobertura e chaves mortas.

Por que isto existe: a UI tem DUAS verdades possiveis sobre texto — o catalogo
(`assets/locales/*.json`) e as chaves escritas no codigo. Se uma chave usada
pelo `main.cpp`/`render.cpp` nao existir no catalogo, o `I18n` devolve a propria
chave e o usuario le' `settings.usb_mode` na tela; se sobrar chave no catalogo,
ninguem percebe que a traducao ficou para tras. Nenhum dos dois casos aparece
num build: a UI sobe e o texto sai errado.

O teste tambem cobra a ORDEM dos papeis do tema (`UiRole`), porque o
`preview-ui.py` traduz papel -> cor por numero e a divergencia sai silenciosa.

    python3 tests/test_locales.py
"""
from __future__ import annotations

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
LOCALES = ROOT / "assets" / "locales"
# O Launcher e' aplicativo: mora em apps/launcher, nao em src/.
LAUNCHER = ROOT / "apps" / "launcher"

PADRAO = "pt_BR"

falhas: list[str] = []
checagens = 0


def ok(cond: bool, msg: str) -> None:
    global checagens
    checagens += 1
    if not cond:
        falhas.append(msg)


def carregar(nome: str) -> dict[str, str]:
    caminho = LOCALES / f"{nome}.json"
    ok(caminho.is_file(), f"catalogo ausente: {caminho.name}")
    if not caminho.is_file():
        return {}
    try:
        dados = json.loads(caminho.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        falhas.append(f"{caminho.name}: JSON invalido ({exc})")
        return {}
    ok(isinstance(dados, dict), f"{caminho.name}: raiz nao e' objeto")
    return dados


def chaves_do_codigo() -> set[str]:
    """Chaves citadas como literal no launcher.

    Sao dois formatos:
      - `tr("chave")` / `tr_n("chave", ...)`, que e' uso direto;
      - `"chave"` solta em tabela (menus, abas), que o codigo traduz depois.
    Literais de nome de arquivo (`settings.json`, `layout.json`) nao entram:
    o filtro e' o ponto seguido de algo que NAO seja extensao conhecida.
    """
    ignorar = {"json", "hpp", "h", "cpp", "c", "conf", "map", "a8"}
    achadas: set[str] = set()
    literal = re.compile(r'"([a-z][a-z_]*\.[a-z_.]*)"')

    for arquivo in sorted(LAUNCHER.glob("*.cpp")) + sorted(LAUNCHER.glob("*.hpp")):
        texto = arquivo.read_text(encoding="utf-8", errors="replace")
        for bruto in literal.findall(texto):
            if bruto.endswith("."):
                continue        # prefixo montado em runtime ("tab." + id)
            if bruto.rsplit(".", 1)[-1] in ignorar:
                continue        # nome de arquivo, nao chave
            achadas.add(bruto)
    return achadas


def main() -> int:
    catalogo = carregar(PADRAO)
    ok(bool(catalogo), "catalogo padrao vazio")

    nomes = sorted(p.stem for p in LOCALES.glob("*.json"))
    ok(PADRAO in nomes, f"catalogo padrao ({PADRAO}) ausente")
    ok(len(nomes) >= 2, f"so' {len(nomes)} idioma(s): o pedido era outro idioma")

    # --- paridade entre idiomas -------------------------------------------
    for nome in nomes:
        if nome == PADRAO:
            continue
        outro = carregar(nome)
        faltando = sorted(set(catalogo) - set(outro))
        sobrando = sorted(set(outro) - set(catalogo))
        ok(not faltando, f"{nome}: {len(faltando)} chave(s) sem traducao "
                         f"(ex.: {faltando[:4]})")
        ok(not sobrando, f"{nome}: {len(sobrando)} chave(s) que nao existem em "
                         f"{PADRAO} (ex.: {sobrando[:4]})")

    # --- chaves usadas pelo codigo existem no catalogo --------------------
    usadas = chaves_do_codigo()
    ok(len(usadas) > 0, "nenhuma chave encontrada no codigo (o padrao mudou?)")

    for nome in nomes:
        dados = carregar(nome) if nome != PADRAO else catalogo
        mortas = sorted(usadas - set(dados))
        ok(not mortas, f"{nome}: chave usada no codigo e ausente do catalogo "
                       f"-> a tela mostraria o texto cru (ex.: {mortas[:4]})")

    # --- valor vazio ou com marca de traducao pendente --------------------
    for nome in nomes:
        dados = carregar(nome) if nome != PADRAO else catalogo
        vazias = sorted(k for k, v in dados.items() if not str(v).strip())
        ok(not vazias, f"{nome}: {len(vazias)} valor(es) vazio(s) "
                       f"(ex.: {vazias[:4]})")
        ok(str(dados.get("settings.title", "")).strip() != "",
           f"{nome}: sem chave de titulo do menu")

    # --- marcadores {n} tem de existir nos dois idiomas -------------------
    # `tr_n` substitui {n}; traducao que perde o marcador mostra "3" faltando.
    for nome in nomes:
        dados = carregar(nome) if nome != PADRAO else catalogo
        for chave, valor in dados.items():
            base = catalogo.get(chave, "")
            ok(("{n}" in str(valor)) == ("{n}" in str(base)),
               f"{nome}: '{chave}' diverge no marcador {{n}} de {PADRAO}")

    # --- ordem do enum UiRole (preview-ui.py traduz por numero) -----------
    header = (LAUNCHER / "ui_geometry.hpp").read_text(encoding="utf-8")
    trecho = header.split("enum UiRole", 1)
    ok(len(trecho) == 2, "enum UiRole nao encontrado em ui_geometry.hpp")
    if len(trecho) == 2:
        corpo = trecho[1].split("};", 1)[0]
        ordem = re.findall(r"(UI_ROLE_[A-Z_]+)", corpo)
        esperado = ["UI_ROLE_NONE", "UI_ROLE_BACKGROUND", "UI_ROLE_SURFACE",
                    "UI_ROLE_ELEVATED", "UI_ROLE_DIVIDER", "UI_ROLE_ACCENT",
                    "UI_ROLE_TEXT_PRIMARY", "UI_ROLE_TEXT_SECONDARY"]
        ok(ordem == esperado,
           f"UiRole mudou de ordem: {ordem} != {esperado} "
           f"(o preview-ui.py traduz papel por numero)")

        preview = (ROOT / "tools" / "preview-ui.py").read_text(encoding="utf-8")
        for indice, papel in enumerate(esperado):
            if papel == "UI_ROLE_NONE":
                continue
            ok(f"{indice}:" in preview,
               f"preview-ui.py nao tem o papel {indice} ({papel})")

    # --- ordem do enum UiTextRole (o preview traduz papel -> peso) --------
    trecho = header.split("enum UiTextRole", 1)
    ok(len(trecho) == 2, "enum UiTextRole nao encontrado em ui_geometry.hpp")
    if len(trecho) == 2:
        corpo = trecho[1].split("};", 1)[0]
        ordem = re.findall(r"(UI_TEXT_[A-Z_]+)", corpo)
        esperado = ["UI_TEXT_TAB_ACTIVE", "UI_TEXT_TAB_IDLE", "UI_TEXT_TITLE",
                    "UI_TEXT_BODY", "UI_TEXT_HINT_KEY", "UI_TEXT_HINT_ACTION",
                    "UI_TEXT_STATUS", "UI_TEXT_PANEL_TITLE",
                    "UI_TEXT_MENU_ROW", "UI_TEXT_COUNT"]
        ok(ordem == esperado,
           f"UiTextRole mudou de ordem: {ordem} != {esperado} "
           f"(o preview-ui.py le o peso por indice em font_weights)")

        # A tabela tem de ter exatamente UI_TEXT_COUNT entradas e cobrir
        # todos os papeis — entrada faltando vira peso regular silencioso.
        fonte = (LAUNCHER / "ui_geometry.cpp").read_text(encoding="utf-8")
        bloco = fonte.split("kTabela[UI_TEXT_COUNT]", 1)
        ok(len(bloco) == 2, "ui_text_weight sem a tabela kTabela")
        if len(bloco) == 2:
            corpo = bloco[1].split("};", 1)[0]
            valores = re.findall(r"(UI_WEIGHT_[A-Z]+)", corpo)
            ok(len(valores) == len(esperado) - 1,
               f"kTabela tem {len(valores)} pesos, esperado "
               f"{len(esperado) - 1} (um por papel)")
            # Titulo, aba ativa, tecla da hint bar e titulo de painel sao
            # negrito na proposta (docs/LAYOUT-PROPOSTA.md §3).
            for papel in ("UI_TEXT_TAB_ACTIVE", "UI_TEXT_TITLE",
                          "UI_TEXT_HINT_KEY", "UI_TEXT_PANEL_TITLE"):
                i = esperado.index(papel)
                if i < len(valores):
                    ok(valores[i] == "UI_WEIGHT_BOLD",
                       f"{papel} deveria ser BOLD, esta {valores[i]}")

    # --- normalizacao de idioma (contrato do i18n.cpp) --------------------
    fonte = (LAUNCHER / "i18n.cpp").read_text(encoding="utf-8")
    ok("pt_BR" in fonte, "i18n.cpp nao cita o catalogo padrao pt_BR")

    if falhas:
        print(f"idiomas: {checagens} checagens, {len(falhas)} falha(s)")
        for f in falhas:
            print(f"  FALHA {f}")
        return 1
    print(f"idiomas: {checagens} checagens, 0 falha(s) "
          f"({len(nomes)} idiomas, {len(catalogo)} chaves)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
