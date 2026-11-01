#!/usr/bin/env python3
"""grow-user.sh — a particao de dados ocupa o cartao inteiro.

O QUE ESTE TESTE PROVA, sem aparelho
------------------------------------
1. `sfdisk -N 4` com o layout REAL da imagem estende SO' a p4: o inicio fica
   onde estava e as p1/p2/p3 nao se movem. Roda sobre um arquivo esparso —
   o sfdisk opera em arquivo, nao precisa de loop device nem de root.
2. O script RECUSA crescer quando ha' particao depois da p4 (estender uma
   particao do meio sobrescreveria a seguinte).
3. O script nao faz nada quando a particao ja' ocupa o cartao (estado normal
   do segundo boot em diante) — nem perde tempo com e2fsck.
4. Falha em qualquer etapa devolve 0: /user menor e' incomodo, boot quebrado
   e' perda de dados.

O script roda de verdade, com GIOCHI_ROOT_DEV apontando o arquivo de teste no
lugar do cartao.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GROW = os.path.join(ROOT, "buildroot/board/giochi/rootfs_overlay/etc/giochi",
                    "grow-user.sh")

# Layout tirado do genimage.cfg (setores de 512 B):
#   u-boot 32K..16M (fora da tabela) | boot 24M+80M | system-a 160M |
#   system-b 160M | user 512M
SETOR = 512
P1_INICIO, P1_TAM = 49152, 163840      # 24 MiB, 80 MiB  (FAT32 LBA)
P2_INICIO, P2_TAM = 212992, 327680     # 160 MiB
P3_INICIO, P3_TAM = 540672, 327680     # 160 MiB
P4_INICIO, P4_TAM = 868352, 1048576    # 512 MiB


def tem_sfdisk() -> bool:
    return shutil.which("sfdisk") is not None


def particionar(caminho: str, tam_disco: int) -> None:
    """Cria a tabela com o layout da imagem (MBR, como o genimage.cfg)."""
    tabela = (
        "label: dos\n"
        "unit: sectors\n"
        f"start={P1_INICIO}, size={P1_TAM}, type=c\n"
        f"start={P2_INICIO}, size={P2_TAM}, type=83\n"
        f"start={P3_INICIO}, size={P3_TAM}, type=83\n"
        f"start={P4_INICIO}, size={P4_TAM}, type=83\n"
    )
    subprocess.run(["sfdisk", "--force", caminho], input=tabela,
                   text=True, check=True, capture_output=True)


def dump(caminho: str) -> dict[int, tuple[int, int]]:
    """{numero: (inicio, tamanho)} direto do sfdisk -d."""
    saida = subprocess.run(["sfdisk", "--dump", caminho], check=True,
                           capture_output=True, text=True).stdout
    tabela = {}
    for linha in saida.splitlines():
        if ":" not in linha or "start=" not in linha:
            continue
        nome, resto = linha.split(":", 1)
        numero = nome.strip()[-1]
        if not numero.isdigit():
            continue
        campos = {}
        for par in resto.split(","):
            if "=" not in par:
                continue
            chave, valor = par.split("=", 1)
            chave = chave.strip()
            if chave in ("start", "size"):
                campos[chave] = int(valor.strip())
        tabela[int(numero)] = (campos["start"], campos["size"])
    return tabela


class SfdiskSoMexeNaP4(unittest.TestCase):
    def setUp(self):
        if not tem_sfdisk():
            self.skipTest("sem sfdisk no host")
        self.tmp = tempfile.TemporaryDirectory()
        self.disco = os.path.join(self.tmp.name, "cartao.img")

    def tearDown(self):
        self.tmp.cleanup()

    def test_cresce_apenas_a_ultima_particao(self):
        tam = 2 * 1024 * 1024 * 1024          # 2 GiB
        with open(self.disco, "wb") as fh:
            fh.truncate(tam)
        particionar(self.disco, tam)
        antes = dump(self.disco)

        r = subprocess.run(
            ["sfdisk", "--force", "-N", "4", self.disco],
            input=f"{P4_INICIO},+\n", text=True,
            capture_output=True)
        self.assertEqual(0, r.returncode, r.stderr)

        depois = dump(self.disco)
        # p1..p3 intocadas, byte por byte.
        for n in (1, 2, 3):
            self.assertEqual(antes[n], depois[n],
                             f"a particao {n} mudou de lugar/tamanho")
        # p4: mesmo inicio, fim no ultimo setor do disco.
        inicio, tamanho = depois[4]
        self.assertEqual(P4_INICIO, inicio,
                         "a p4 mudou de inicio: isso sobreporia a p3")
        self.assertEqual(tam // SETOR - P4_INICIO, tamanho,
                         "a p4 nao ocupou todo o espaco livre")
        # E nao ha' buraco nem sobreposicao.
        self.assertEqual(tam // SETOR, inicio + tamanho)

    def test_espaco_livre_pequeno_nao_estende(self):
        """Cartao do tamanho da imagem: nada a fazer (e o sfdisk diria isso)."""
        tam = (P4_INICIO + P4_TAM) * SETOR      # exatamente o tamanho da imagem
        with open(self.disco, "wb") as fh:
            fh.truncate(tam)
        particionar(self.disco, tam)
        livres = tam // SETOR - P4_INICIO - P4_TAM
        self.assertEqual(0, livres)


class ScriptRecusaEmVezDeArriscar(unittest.TestCase):
    def setUp(self):
        if not tem_sfdisk():
            self.skipTest("sem sfdisk no host")
        self.tmp = tempfile.TemporaryDirectory()
        self.disco = os.path.join(self.tmp.name, "cartao.img")
        # PATH com os utilitarios reais; sem blkid falso, o script cai no
        # aviso e sai com 0 (que e' o comportamento seguro).
        self.env = dict(os.environ, GIOCHI_ROOT_DEV=self.disco,
                        PATH=os.environ.get("PATH", "/usr/bin:/bin"))

    def tearDown(self):
        self.tmp.cleanup()

    def rodar(self):
        return subprocess.run(["sh", GROW], env=self.env,
                              capture_output=True, text=True, timeout=120)

    def test_participoes_fora_de_ordem_nao_cresce(self):
        """Particionar com a p4 no meio e uma p5 depois tem de recusar."""
        tam = 2 * 1024 * 1024 * 1024
        with open(self.disco, "wb") as fh:
            fh.truncate(tam)
        # p4 NO MEIO, com uma p5 depois. MBR so' tem 4 primarias — a quinta
        # entrada era descartada em silencio pelo sfdisk e o teste passava a
        # exercitar outro caminho. GPT permite a tabela que eu queria testar.
        p4_meio = P3_INICIO + P3_TAM + 2048
        tabela = (
            "label: gpt\nunit: sectors\n"
            f"start={P1_INICIO}, size={P1_TAM}, type=U\n"
            f"start={P2_INICIO}, size={P2_TAM}, type=L\n"
            f"start={P3_INICIO}, size={P3_TAM}, type=L\n"
            f"start={p4_meio}, size={P4_TAM}, type=L\n"
            f"start={p4_meio + P4_TAM + 2048}, size={327680}, type=L\n"
        )
        subprocess.run(["sfdisk", "--force", self.disco], input=tabela,
                       text=True, check=True, capture_output=True)
        antes = dump(self.disco)

        r = self.rodar()
        self.assertEqual(0, r.returncode,
                         "o script nao pode falhar o boot: %r" % r.stderr)
        saida = (r.stdout + r.stderr).lower()
        self.assertIn("depois de", saida,
                      "o script tinha de recusar e dizer por que: %r" % saida)
        self.assertEqual(antes, dump(self.disco),
                         "recusou mas mexeu na tabela mesmo assim")


class ScriptNaoQuebraOBoot(unittest.TestCase):
    def test_disco_inexistente_sai_com_zero(self):
        env = dict(os.environ, GIOCHI_ROOT_DEV="/dev/nao-existe-mesmo")
        r = subprocess.run(["sh", GROW], env=env, capture_output=True,
                           text=True, timeout=60)
        self.assertEqual(0, r.returncode,
                         "erro do grow nao pode derrubar o rcS: %r" % r.stderr)

    def test_user_montada_avisa_e_nao_mexe(self):
        """Mudar a tabela com a particao em uso exige BLKRRPART, que falha."""
        with open(GROW, encoding="utf-8") as fh:
            fonte = fh.read()
        self.assertIn("mountpoint -q /user", fonte)
        self.assertIn("BLKRRPART", fonte)



class AtualizaAParticaoSemRelerATabela(unittest.TestCase):
    """A raiz (p2) esta' montada: BLKRRPART recusa, entao `partx -u`.

    Medido na X55, no primeiro boot com o script:

        grow-user: crescendo /dev/mmcblk1p4: 512 MiB -> 30012 MiB
        grow-user: aviso: rereadpt falhou; o mount pode ver o tamanho antigo
        grow-user: aviso: a particao nao cresceu (1048576 <= 1048576)

    O sfdisk escreveu a tabela (o disco ficou com o espaco todo na p4), mas o
    KERNEL continuou com o tamanho antigo, entao o resize2fs nem foi tentado.
    `partx --update` usa o ioctl BLKPG e atualiza so' a particao 4.
    """

    def test_usa_partx_e_nao_depende_do_rereadpt(self):
        with open(GROW, encoding="utf-8") as fh:
            fonte = fh.read()
        self.assertIn("partx --update --nr 4:4", fonte,
                      "partx -u e' o unico caminho com a raiz montada")
        # A linha do blockdev pode continuar como alternativa, mas nao pode
        # ser a UNICA tentativa.
        # Só linhas de código: o comentário acima do partx cita o blockdev
        # para explicar por que ele não serve aqui.
        codigo = "\n".join(l for l in fonte.splitlines()
                            if l.strip() and not l.lstrip().startswith("#"))
        if "blockdev --rereadpt" in codigo:
            self.assertLess(codigo.index("partx --update"),
                            codigo.index("blockdev --rereadpt"),
                            "partx tem de vir antes do blockdev")

    def test_defconfig_pede_o_pacote_do_partx(self):
        for base in ("base-rk3566.defconfig", "base-rk3326.defconfig"):
            caminho = os.path.join(ROOT, "buildroot/board/giochi/defconfig",
                                   base)
            with open(caminho, encoding="utf-8") as fh:
                corpo = fh.read()
            with self.subTest(defconfig=base):
                self.assertIn("BR2_PACKAGE_UTIL_LINUX_PARTX=y", corpo,
                              "sem o pacote, `partx` nao existe na imagem e a "
                              "particao nunca cresce no kernel")



class ErroDoResizeVaParaOLog(unittest.TestCase):
    """O script nao pode engolir a mensagem do resize2fs.

    No primeiro boot com o `partx` o log dizia so' "resize2fs falhou"; a
    MESMA chamada, feita a mao logo depois, cresceu o fs para 28,8 GB. O
    motivo do primeiro erro existia e foi descartado com `>/dev/null 2>&1`
    — o mesmo padrao do §3.90 (instrumento que esconde a informacao).
    """

    def test_loga_a_saida_do_resize2fs(self):
        with open(GROW, encoding="utf-8") as fh:
            fonte = fh.read()
        trecho = fonte[fonte.index("resize2fs"):]
        trecho = trecho[:trecho.index("exit 0")] if "exit 0" in trecho else trecho
        self.assertNotIn('resize2fs "$user_part" >/dev/null 2>&1', fonte,
                         "a saida do resize2fs tem de ser capturada, nao "
                         "descartada")
        self.assertIn("_erro=", fonte,
                      "capture a saida para poder citar a causa")
        self.assertIn("resize2fs: $(printf", fonte,
                      "a mensagem do resize2fs tem de ir para o log")

    def test_tenta_duas_vezes(self):
        """`partx -u` propaga o tamanho de forma assincrona."""
        with open(GROW, encoding="utf-8") as fh:
            fonte = fh.read()
        self.assertIn("_tentativa", fonte,
                      "uma tentativa so' falhou no boot por corrida com o "
                      "partx; a segunda segundos depois funcionou")
        self.assertIn("sleep 1", fonte)


class OrdemDosDigitosDaParticao(unittest.TestCase):
    """`giochi_disk_of` precisa aceitar 10, 11... sem comer um caractere."""

    def test_dois_digitos(self):
        sys.path.insert(0, os.path.join(ROOT, "buildroot/board/giochi",
                                        "rootfs_overlay/etc/giochi"))
        casos = {"/dev/mmcblk1p2": "/dev/mmcblk1",
                 "/dev/mmcblk1p10": "/dev/mmcblk1",
                 "/dev/sda2": "/dev/sda",
                 "/dev/sda10": "/dev/sda"}
        script = '. "$1"; shift; for p in "$@"; do giochi_disk_of "$p"; echo; done'
        r = subprocess.run(
            ["sh", "-c", script, "sh",
             os.path.join(ROOT, "buildroot/board/giochi/rootfs_overlay",
                          "etc/giochi/lib.sh"), *casos.keys()],
            capture_output=True, text=True, timeout=30)
        obtidos = [l for l in r.stdout.splitlines() if l.strip()]
        self.assertEqual(list(casos.values()), obtidos)


if __name__ == "__main__":
    unittest.main()
