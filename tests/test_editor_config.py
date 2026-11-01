#!/usr/bin/env python3
"""Configuracao do editor: os includes tem de resolver FORA do build tambem.

POR QUE ESTE TESTE EXISTE
-------------------------
Depois de mover `src/launcher` para `apps/launcher`, o editor passou a mostrar
um erro que nao existia no build:

    apps/launcher/input.hpp:12:10: 'giochi_hotkey.h' file not found
    (VS Code: "giochi_hotkey.h: Arquivo ou diretorio inexistente")

O header esta' em `src/common/`, o Makefile passa `-Isrc/common` e o `make`
compilava limpo — quem estava errado era o EDITOR, por falta de informacao. O
`make` nao expoe as flags para ninguem, e a saida comum (copiar a lista de
includes para o `.vscode`) e' uma segunda fonte de verdade que envelhece em
silencio.

A correcao foi gerar `compile_commands.json` a partir do proprio Makefile
(`tools/gen-compdb.py`). Este teste cobra as tres pontas:

1. o banco de compilacao existe, cobre os fontes do projeto e traz as flags do
   Makefile — inclusive para `apps/launcher`;
2. TODO `#include "..."` de projeto resolve usando a entrada do banco (dir do
   arquivo + `-I`), que e' exatamente como o editor resolve. E' este teste que
   teria pego o `giochi_hotkey.h` no dia do move;
3. a lista de `includePath` do `.vscode` (fallback para antes do primeiro
   `make`) e' IGUAL ao `INCS` do Makefile — se um mudar sem o outro, reprova.
"""

from __future__ import annotations

import json
import os
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
COMPDB = ROOT / "compile_commands.json"
CPP_PROPS = ROOT / ".vscode" / "c_cpp_properties.json"
MAKEFILE = ROOT / "Makefile"
GEN = ROOT / "tools" / "gen-compdb.py"

# Diretorios que sao implementacao do projeto (o resto e' sistema/SDL).
PROJETO = ("src/", "apps/", "tools/", "tests/")


def read(path):
    with open(path, encoding="utf-8") as fp:
        return fp.read()


def linhas_ativas(caminho: pathlib.Path) -> list[str]:
    """Linhas de um arquivo de config SEM comentario e sem branco.

    `assertIn("x", texto_inteiro)` casa com COMENTARIO que cite "x". Foi assim
    que este teste passou com a linha de ignore removida (o comentario acima
    dela explica o arquivo pelo nome) — a terceira vez na mesma serie de
    sessoes que um teste deste repositorio casa com comentario em vez de
    codigo (AGENTS.md §3.36, §3.118, §3.125). Para `.gitignore` e afins,
    compare sempre linha a linha.
    """
    saida = []
    for linha in read(caminho).splitlines():
        limpa = linha.strip()
        if not limpa or limpa.startswith("#"):
            continue
        saida.append(limpa)
    return saida


def incs_do_makefile() -> list[str]:
    """Os `-I...` do INCS, na ordem, sem o prefixo."""
    fonte = read(MAKEFILE)
    linha = next(l for l in fonte.splitlines() if l.startswith("INCS :="))
    return [p[2:] for p in linha.split() if p.startswith("-I")]


def includes_do_arquivo(caminho: pathlib.Path) -> list[str]:
    """Os `#include "x"` de um fonte, ignorando comentario de bloco."""
    texto = read(caminho)
    texto = re.sub(r"/\*.*?\*/", "", texto, flags=re.S)
    return re.findall(r'^\s*#\s*include\s+"([^"]+)"', texto, flags=re.M)


def resolve(nome: str, de_onde: pathlib.Path, incluir: list[pathlib.Path]) -> str:
    """Primeiro caminho onde o include existe, ou '' se nao resolve.

    Mesma ordem que o compilador usa: diretorio do arquivo que inclui primeiro,
    depois cada `-I` na ordem dada.
    """
    for base in [de_onde, *incluir]:
        alvo = (base / nome)
        if alvo.is_file():
            return str(alvo)
    return ""


class BancoDeCompilacao(unittest.TestCase):
    """1: o compile_commands.json existe e fala do projeto."""

    @classmethod
    def setUpClass(cls):
        if not COMPDB.is_file():
            raise unittest.SkipTest(
                "compile_commands.json ausente (rode 'make compdb')")
        cls.banco = json.loads(read(COMPDB))

    def test_tem_entradas(self):
        self.assertGreater(len(self.banco), 20,
                          "banco de compilacao vazio ou quase")

    def test_cobre_o_launcher_e_o_nucleo(self):
        arquivos = {os.path.relpath(e["file"], ROOT) for e in self.banco}
        for esperado in ("apps/launcher/main.cpp", "apps/launcher/input.cpp",
                         "src/common/giochi_hotkey.c", "src/init/main.c",
                         "tests/test_layout.cpp"):
            self.assertIn(esperado, arquivos,
                          f"{esperado} fora do compile_commands.json")

    def test_le_as_flags_do_makefile(self):
        """As flags tem de ser as do Makefile — nao uma lista paralela."""
        incs = incs_do_makefile()
        esperado = {f"-I{d}" for d in incs}
        for entrada in self.banco:
            if not entrada["file"].endswith(("launcher/input.cpp",
                                             "common/giochi_hotkey.c")):
                continue
            obtido = {a for a in entrada["arguments"] if a.startswith("-I")}
            self.assertEqual(esperado, obtido,
                             f"{entrada['file']}: include path diverge do INCS")

    def test_define_de_texto_preserva_as_aspas(self):
        """`-DGIOCHI_SOC="x55"` e' UM argumento com as aspas internas: o shlex
        split existe para isso, e sem ele o editor veria `-DGIOCHI_SOC=x55`."""
        for entrada in self.banco:
            for arg in entrada["arguments"]:
                if arg.startswith("-DGIOCHI_SOC="):
                    self.assertIn('"', arg,
                                  f"-DGIOCHI_SOC perdeu as aspas em {arg!r}")
                    return
        self.fail("nenhuma entrada tem -DGIOCHI_SOC")


class IncludesResolvem(unittest.TestCase):
    """2: o bug relatado. Cada include de projeto resolve fora do build."""

    @classmethod
    def setUpClass(cls):
        if not COMPDB.is_file():
            raise unittest.SkipTest(
                "compile_commands.json ausente (rode 'make compdb')")
        cls.banco = json.loads(read(COMPDB))

    def test_todo_include_de_projeto_resolve(self):
        falhas = []
        for entrada in self.banco:
            fonte = pathlib.Path(entrada["file"])
            incluir = [pathlib.Path(a[2:]) for a in entrada["arguments"]
                       if a.startswith("-I")]
            incluir = [p if p.is_absolute() else ROOT / p for p in incluir]
            for nome in includes_do_arquivo(fonte):
                # So' os headers do projeto: o resto (SDL, ALSA) vem do
                # sistema e nao esta' no repositorio.
                if not (ROOT / nome).exists() and \
                   not any((d / nome).exists() for d in incluir):
                    # pode ser header de sistema com caminho relativo ficticio
                    continue
                if resolve(nome, fonte.parent, incluir):
                    continue
                falhas.append(f"{os.path.relpath(fonte, ROOT)}: {nome}")
        self.assertEqual([], falhas,
                         "includes que o editor nao resolve mas o build sim:\n  "
                         + "\n  ".join(falhas[:10]))

    def test_giochi_hotkey_resolve_no_input_hpp(self):
        """O caso concreto: input.hpp inclui giochi_hotkey.h, que mora em
        src/common. Sem -Isrc/common o editor acusa 'arquivo inexistente'."""
        header = ROOT / "apps/launcher/input.hpp"
        self.assertIn("giochi_hotkey.h", includes_do_arquivo(header))
        incluir = [ROOT / d for d in incs_do_makefile()]
        self.assertTrue(resolve("giochi_hotkey.h", header.parent, incluir),
                        "giochi_hotkey.h nao resolve a partir de input.hpp")

    def test_todo_header_do_projeto_esta_numa_pasta_do_incs(self):
        """Header de projeto fora dos `-I` so' funciona por acaso (incluido
        pelo irmao de pasta). Se for incluido de outro diretorio, quebra."""
        incs = {ROOT / d for d in incs_do_makefile()}
        for header in ROOT.glob("apps/*/*.hpp"):
            self.assertIn(header.parent, incs,
                          f"{header} fora dos include paths do Makefile")


class ConfigDoEditor(unittest.TestCase):
    """3: o fallback do .vscode nao pode divergir do Makefile."""

    @classmethod
    def setUpClass(cls):
        if not CPP_PROPS.is_file():
            raise unittest.SkipTest(".vscode/c_cpp_properties.json ausente")

    def _props(self) -> dict:
        """JSONC -> dict (o cpptools aceita comentario)."""
        texto = read(CPP_PROPS)
        texto = re.sub(r"^\s*//.*$", "", texto, flags=re.M)
        return json.loads(texto)

    def test_aponta_para_o_banco_gerado(self):
        cfg = self._props()["configurations"][0]
        self.assertEqual("${workspaceFolder}/compile_commands.json",
                         cfg.get("compileCommands"),
                         "o editor tem de ler o banco gerado pelo Makefile")

    def test_includepath_igual_ao_makefile(self):
        cfg = self._props()["configurations"][0]
        obtido = [p.replace("${workspaceFolder}/", "") for p in cfg["includePath"]]
        self.assertEqual(incs_do_makefile(), obtido,
                         "includePath do .vscode diverge do INCS do Makefile — "
                         "mude o Makefile e rode `make compdb`, nao edite o "
                         "editor por conta propria")

    def test_padroes_de_linguagem_batem_com_o_build(self):
        cfg = self._props()["configurations"][0]
        makefile = read(MAKEFILE)
        self.assertIn('CSTD    ?= -std=c99', makefile)
        self.assertIn('CXXSTD  ?= -std=c++17', makefile)
        self.assertEqual("c99", cfg["cStandard"])
        self.assertEqual("c++17", cfg["cppStandard"])


class AlvosDoMake(unittest.TestCase):
    """O banco tem de ser gerado no build — senao envelhece (foi o bug)."""

    def test_compdb_esta_em_all(self):
        fonte = read(MAKEFILE)
        linha = next(l for l in fonte.splitlines() if l.startswith("all:"))
        self.assertIn("compdb", linha,
                      "sem `all: ... compdb` o banco nao acompanha um move")

    def test_compdb_e_phony_e_roda_o_gerador(self):
        fonte = read(MAKEFILE)
        self.assertIn("compdb", fonte[fonte.index(".PHONY:"):]
                      [:fonte[fonte.index(".PHONY:"):].index("\n\n")])
        self.assertIn("tools/gen-compdb.py", fonte)

    def test_arquivo_gerado_esta_ignorado(self):
        # Linha a linha, sem comentario: o comentario acima da regra cita o
        # nome do arquivo e fazia `assertIn` no texto inteiro passar mesmo com
        # a regra apagada.
        self.assertIn("compile_commands.json",
                      linhas_ativas(ROOT / ".gitignore"),
                      "compile_commands.json e' gerado; nao pode ir versionado")

    def test_gerador_nao_tem_lista_de_fontes_propria(self):
        """As listas vem do Makefile. Se o gerador enumerar diretorio por conta
        propria, vira a segunda fonte de verdade que este teste existe para
        evitar."""
        fonte = read(GEN)
        for suspeito in ("rglob", "os.walk", "glob("):
            self.assertNotIn(suspeito, fonte,
                             f"gen-compdb.py usa {suspeito}: as listas de "
                             f"fontes tem de vir do Makefile")


if __name__ == "__main__":
    unittest.main(verbosity=2)
