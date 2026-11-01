"""Guardas do `tools/flash-card.sh` — o caminho PC -> cartao.

Cada recusa deste script corresponde a um jeito de perder tempo ou perder
dados, e todas foram medidas nesta sessao:

  * `dd` sem `conv=fsync` "termina" com dados no cache de pagina e o kernel do
    aparelho da' EIO dentro do SquashFS (AGENTS.md §3.42);
  * gravar numa PARTICAO (/dev/sdd1) em vez do disco inteiro deixa o cartao
    com metade da imagem e um MBR velho;
  * gravar no disco que contem `/` nao tem desfazer;
  * gravar num leitor USB SEM cartao (0 setores) falha no meio do `dd` e
    deixa o usuario sem saber se o cartao foi tocado;
  * regravar o cartao APAGA o `settings.json` da particao /user, e o Modo
    Desenvolvedor volta a ficar desligado — o SSH que funcionava "para de
    funcionar" sem nenhuma relacao com a imagem.
"""

import os
import re
import shutil
import subprocess
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FLASH = os.path.join(ROOT, "tools/flash-card.sh")
VERIFY = os.path.join(ROOT, "tools/verify-card.py")
DEV_MODE = os.path.join(ROOT, "tools/giochi-dev-mode.sh")


def read(path):
    with open(path, encoding="utf-8") as fp:
        return fp.read()


def sh_code(text):
    """Sem comentarios: os cabecalhos CITAM o codigo que explicam."""
    return "\n".join(l for l in text.splitlines()
                     if not l.lstrip().startswith("#"))


class FlashCardTest(unittest.TestCase):
    def test_script_existe_e_e_executavel(self):
        self.assertTrue(os.path.isfile(FLASH))
        self.assertTrue(os.access(FLASH, os.X_OK),
                        "flash-card.sh precisa ser executavel")

    def test_dd_usa_conv_fsync(self):
        """`dd` sem fsync retorna antes do cartao ter os dados."""
        corpo = sh_code(read(FLASH))
        self.assertRegex(
            corpo, r"dd\s+if=.*conv=fsync",
            "o dd do flash-card.sh perdeu o `conv=fsync`: o comando termina "
            "com dados no cache de pagina e o aparelho le lixo (§3.42)")

    def test_recusa_particao(self):
        """Gravar em /dev/sdd1 deixa o MBR velho e a imagem pela metade."""
        corpo = sh_code(read(FLASH))
        self.assertIn("parece PARTICAO", corpo)

    def test_recusa_disco_da_raiz(self):
        corpo = sh_code(read(FLASH))
        self.assertIn("contem a raiz do sistema", corpo)
        # A comparacao tem de ser pelo DISCO que contem a raiz, nao pelo nome.
        self.assertRegex(corpo, r"lsblk\s+-no\s+PKNAME")

    def test_recusa_leitor_sem_cartao(self):
        """0 setores = leitor vazio; o dd falharia no meio."""
        corpo = sh_code(read(FLASH))
        self.assertIn("reporta 0 setores", corpo)

    def test_verifica_depois_de_gravar(self):
        """Um cartao pode gravar "sem erro" e entregar lixo (§3.42)."""
        corpo = sh_code(read(FLASH))
        self.assertIn("verify-card.py", corpo)
        # A verificacao tem de estar DEPOIS do dd e nao pode ser opcional.
        self.assertLess(corpo.index("dd if="), corpo.index("verify-card.py"))
        self.assertNotRegex(
            corpo, r"verify-card\.py.*\|\|\s*true",
            "verificar nao pode ser opcional: falha tem de interromper")

    def test_reabre_modo_desenvolvedor(self):
        """Regravar apaga o settings.json da /user e fecha o SSH (§3.9)."""
        corpo = sh_code(read(FLASH))
        self.assertIn("giochi-dev-mode.sh", corpo)
        self.assertIn("--sem-ssh", corpo)

    def test_chave_resolvida_antes_da_elevacao(self):
        """Depois do `pkexec` o HOME e' /root e a chave do usuario some.

        Medido: a primeira gravacao real passou pelo `dd`, a verificacao deu
        OK e o Modo Desenvolvedor falhou com "chave publica nao encontrada",
        deixando o cartao gravado e SEM SSH. A chave tem de ser descoberta
        como usuario e viajar por argumento ate o giochi-dev-mode.sh.
        """
        corpo = sh_code(read(FLASH))

        # `--key` tem de existir e ser aceito no parsing (antes da elevacao).
        self.assertIn("--key)", corpo)
        indice_key = corpo.index("--key)")
        indice_elevacao = corpo.index("GIOCHI_FLASH_ELEVADO")
        self.assertLess(
            indice_key, indice_elevacao,
            "o --key tem de ser lido ANTES da re-execucao com privilegio")

        # A chamada do giochi-dev-mode.sh tem de passar a chave por argumento.
        self.assertRegex(
            corpo, r'giochi-dev-mode\.sh"\s+--key',
            "a chave nao esta' sendo passada ao giochi-dev-mode.sh")

        # E tem de ser REPASSADA na re-execucao: sem isso o processo elevado
        # recalcula a chave com HOME=/root e volta a nao achar nada.
        self.assertRegex(
            corpo, r'set -- --key "\$CHAVE_PUB"',
            "a chave nao sobrevive a elevacao: o processo com privilegio "
            "recalcula com HOME=/root e falha")

    def test_chave_do_usuario_e_encontrada_antes_do_pkexec(self):
        """Cobre SUDO_USER/PKEXEC_UID: os dois trocam o HOME."""
        corpo = sh_code(read(FLASH))
        for marca in ("SUDO_USER", "PKEXEC_UID"):
            with self.subTest(marca=marca):
                self.assertIn(
                    marca, corpo,
                    "%s nao e' considerada: elevado, o HOME vira /root e a "
                    "chave do usuario nao e' achada" % marca)

    def test_elevacao_testa_antes_de_exec(self):
        """`exec cmd || outro` nunca executa o `outro` (AGENTS.md §3.82)."""
        corpo = sh_code(read(FLASH))
        self.assertNotRegex(
            corpo, r"exec\s+sudo[^\n]*\|\|",
            "`exec ... || ...` nunca roda o lado direito do `||`")
        for linha in corpo.splitlines():
            if "exec sudo" in linha or "exec env" in linha:
                self.assertNotIn("||", linha,
                                 "elevacao com `||` na mesma linha do exec: %r"
                                 % linha)
        # O elevador tem de ser TESTADO antes (`sudo -n true`).
        self.assertIn("sudo -n true", corpo)

    def test_argumentos_sobrevivem_a_elevacao(self):
        """pkexec limpa o ambiente; o que importa vai por argumento."""
        corpo = sh_code(read(FLASH))
        self.assertIn("GIOCHI_FLASH_ELEVADO", corpo)
        self.assertRegex(corpo, r'exec\s+(sudo|env)[^\n]*"\$SELF"',
                         "a re-execucao tem de usar o caminho ABSOLUTO ($SELF)")

    def test_ferramentas_que_ele_chama_existem(self):
        for caminho in (VERIFY, DEV_MODE):
            with self.subTest(ferramenta=os.path.basename(caminho)):
                self.assertTrue(os.path.isfile(caminho))

    def test_listar_nao_grava_nada(self):
        """`--listar` e' o que o usuario roda antes; nao pode pedir senha."""
        proc = subprocess.run(["sh", FLASH, "--listar"],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=60)
        self.assertEqual(0, proc.returncode,
                         proc.stderr.decode("utf-8", "replace"))
        saida = proc.stdout.decode("utf-8", "replace")
        self.assertNotIn("dd if=", saida)

    def test_recusa_alvo_que_nao_e_bloco(self):
        proc = subprocess.run(["sh", FLASH, "/caminho/que/nao/existe"],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=60)
        self.assertNotEqual(0, proc.returncode)
        self.assertIn("ERRO", proc.stderr.decode("utf-8", "replace"))

    def test_ajuda_documenta_o_uso(self):
        proc = subprocess.run(["sh", FLASH, "--help"],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=60)
        self.assertEqual(0, proc.returncode)
        saida = proc.stdout.decode("utf-8", "replace")
        for termo in ("--listar", "--device", "--sem-ssh", "--sim", "--checar"):
            with self.subTest(opcao=termo):
                self.assertIn(termo, saida)


if __name__ == "__main__":
    unittest.main()
