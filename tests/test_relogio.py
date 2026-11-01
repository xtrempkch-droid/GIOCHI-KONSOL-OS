#!/usr/bin/env python3
"""S01relogio + rcK — o relogio sobrevive ao desligamento.

POR QUE ESTE TESTE EXISTE
-------------------------
Medido no aparelho, relogio do console contra o do PC:

    console: Sat Aug  5 09:03:57 UTC 2017   (epoch 1501923837)
    PC:      dom 01 nov 2026 15:10          (epoch 1793556647)

`grep -ci rtc` no dmesg completo do boot da X55 devolve ZERO: nao ha'
relogio de hardware. O kernel acorda com uma epoca fixa e tudo que depende de
data fica errado — `resize2fs` recusa (lastcheck < mtime), certificado TLS da
Loja aparece como "ainda nao valido", o log sai datado de 2017.

O conserto e' guardar `date +%s` na particao de dados no desligamento (rcK) e
restaurar no boot (S01relogio) — o mesmo principio do fake-hwclock do Debian.

COMO E' TESTADO SEM APARELHO
----------------------------
`mountpoint` e `date` sao substituidos por scripts no PATH. O falso `date`
registra cada chamada em ARQUIVO_CHAMADAS, entao o teste verifica a INTERFACE
com o utilitario (`date -s @<epoch>`), nao um efeito no relogio do host.
"""

from __future__ import annotations

import os
import stat
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S01 = os.path.join(ROOT, "scripts", "init.d", "S01relogio")
RCK = os.path.join(ROOT,
                   "buildroot/board/giochi/rootfs_overlay/etc/init.d/rcK")

# Faixa aceita pelo script: 2020-01-01 .. 2100-01-01.
EPOCA_2026 = 1793556647
EPOCA_2017 = 1501923837


class RelogioBase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.bin = os.path.join(self.dir, "bin")
        self.user = os.path.join(self.dir, "user")
        os.makedirs(self.bin)
        os.makedirs(os.path.join(self.user, ".giochi"))
        self.chamadas = os.path.join(self.dir, "chamadas.txt")
        open(self.chamadas, "w").close()
        self._montado = True

        # `mountpoint -q X` responde pelo arquivo de controle, para o teste
        # poder fingir "particao de dados fora do ar".
        # Aceita QUALQUER caminho: o /user do teste e' um diretorio temporario,
        # entao um `case` preso ao texto "/user" nunca casaria.
        self._shim("mountpoint", """#!/bin/sh
[ -e "%s/montado" ] || exit 1
exit 0
""" % self.dir)
        # `date` falso: registra o que foi pedido e devolve uma epoca
        # PLAUSIVEL (2026) — o caso base. O cenario do relogio errado (2017)
        # e' um teste proprio, porque o rcK tem de RECUSAR salvar aquilo.
        # `date` falso e REALISTA: aceita `-s @<digitos>` e `-s` com data
        # no formato YYYY-MM-DD[ hh:mm[:ss]], e RECUSA o resto — igual ao
        # `parse_datestr` do BusyBox. Um shim que aceita qualquer `-s`
        # deixava o teste do pedido-lixo passar pelo caminho errado: o
        # "lixo" era "aplicado" e o script removia o arquivo, exatamente o
        # que nao pode acontecer.
        self._shim("date", """#!/bin/sh
echo "date $*" >> "%s"
case "$1" in
    -s)
        a="$2"
        case "$a" in
            @[0-9]*) exit 0 ;;
            [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]*) exit 0 ;;
            *) exit 1 ;;
        esac ;;
    -r) exec /usr/bin/stat -c %%Y "$2" ;;
    +%%s) echo "%d"; exit 0 ;;
    +%%Y) echo "2026"; exit 0 ;;
    -u)
        case "$2" in
            +%%Y) echo "2026"; exit 0 ;;
            *) exit 0 ;;
        esac ;;
esac
exit 0
""" % (self.chamadas, EPOCA_2026))
        # `logger -s` do BusyBox imprime em stderr alem de mandar ao syslog.
        # Um shim que so' devolve 0 apagaria todas as mensagens do script.
        self._shim("logger", """#!/bin/sh
tem_s=0
for a in "$@"; do [ "$a" = "-s" ] && tem_s=1; done
[ "$tem_s" = 1 ] && echo "$*" >&2
exit 0
""")
        self._shim("sync", "#!/bin/sh\nexit 0\n")
        self._shim("swapoff", "#!/bin/sh\nexit 0\n")
        # `mount`/`umount` fingidos: os arquivos do "cartao" ja' existem no
        # diretorio apontado por GIOCHI_RELOGIO_BOOT_MNT.
        self._shim("mount", "#!/bin/sh\nexit 0\n")
        self._shim("mkdir", "#!/bin/sh\nexec /usr/bin/mkdir \"$@\"\n")
        self._shim("umount", "#!/bin/sh\nexit 0\n")
        self._shim("tr", "#!/bin/sh\nexec /usr/bin/tr \"$@\"\n")
        open(os.path.join(self.dir, "montado"), "w").close()
        self._shim("cat", "#!/bin/sh\nexec /usr/bin/cat \"$@\"\n")

    def tearDown(self):
        self.tmp.cleanup()

    def _shim(self, nome, corpo):
        caminho = os.path.join(self.bin, nome)
        with open(caminho, "w", encoding="utf-8") as fh:
            fh.write(corpo)
        os.chmod(caminho, os.stat(caminho).st_mode | stat.S_IEXEC)

    def _rodar(self, script, extra_env=None):
        """Roda o script com /user e /user2 redirecionados para o tmp."""
        with open(script, encoding="utf-8") as fh:
            fonte = fh.read()
        # Troca os caminhos reais pelos de teste, sem tocar na logica.
        # "/user2" CONTEM "/user" como prefixo: trocar na ordem errada
        # produz "<tmp>/<tmp>/user2". O marcador evita isso.
        fonte = fonte.replace("/user2", "@@USER2@@")
        fonte = fonte.replace("/user", self.user)
        fonte = fonte.replace("@@USER2@@", self.user + "2")
        alvo = os.path.join(self.dir, os.path.basename(script) + ".test")
        with open(alvo, "w", encoding="utf-8") as fh:
            fh.write(fonte)
        os.chmod(alvo, 0o755)
        env = {"PATH": self.bin + ":/usr/bin:/bin"}
        env.update(extra_env or {})
        return subprocess.run(
            ["sh", alvo, "start"],
            capture_output=True, text=True, timeout=60, env=env,
        )

    def _chamadas_date(self):
        with open(self.chamadas, encoding="utf-8") as fh:
            return [l.strip() for l in fh if l.strip()]


class RelogioRestaura(RelogioBase):
    """S01relogio: le a epoca salva e aplica com `date -s @<epoch>`."""

    def test_restaura_epoca_salva(self):
        with open(os.path.join(self.user, ".giochi", "relogio"), "w") as fh:
            fh.write("%d\n" % EPOCA_2026)
        r = self._rodar(S01)
        self.assertEqual(0, r.returncode, r.stderr)
        self.assertIn("date -s @%d" % EPOCA_2026, self._chamadas_date(),
                      "tinha de restaurar a epoca salva: %r"
                      % self._chamadas_date())

    def test_primeiro_boot_sem_arquivo_nao_quebra(self):
        r = self._rodar(S01)
        self.assertEqual(0, r.returncode, r.stderr)
        # Nao pode inventar uma epoca: sem arquivo, nao chama `date -s`.
        self.assertNotIn("date -s", " ".join(self._chamadas_date()))

    def test_lixo_no_arquivo_e_ignorado(self):
        with open(os.path.join(self.user, ".giochi", "relogio"), "w") as fh:
            fh.write("nao-e-um-numero\n")
        r = self._rodar(S01)
        self.assertEqual(0, r.returncode, r.stderr)
        self.assertNotIn("date -s", " ".join(self._chamadas_date()),
                         "lixo no arquivo nao pode virar data")

    def test_epoca_fora_de_faixa_e_ignorada(self):
        # 1 = 1970. Restaurar isso quebraria TLS de um jeito pior que 2017.
        with open(os.path.join(self.user, ".giochi", "relogio"), "w") as fh:
            fh.write("1\n")
        r = self._rodar(S01)
        self.assertEqual(0, r.returncode, r.stderr)
        self.assertNotIn("date -s @1", " ".join(self._chamadas_date()))
        self.assertIn("fora de faixa", r.stdout + r.stderr)

    def test_sem_particiao_de_dados_nao_falha(self):
        os.unlink(os.path.join(self.dir, "montado"))
        r = self._rodar(S01)
        self.assertEqual(0, r.returncode,
                         "sem /user o boot NAO pode abortar: %r" % r.stderr)
        self.assertIn("particao de dados fora do ar", r.stdout + r.stderr)



class DataDoCartaoNoPrimeiroBoot(RelogioBase):
    """Sem epoca salva, a hora vem dos arquivos que o PC gravou no cartao.

    No primeiro boot depois de gravar o cartao, /user nasce VAZIO (o
    post-image.sh cria user.ext4 vazio), entao nao ha' nada ali de onde tirar
    a hora. Mas a particao de boot foi escrita pelo PC: o mtime mais novo dali
    e' uma data real.

    Sem este recurso o conserto nao se sustentava: o rcK recusa salvar 2017
    (fora de faixa), entao nunca existiria uma epoca plausivel para restaurar
    e o relogio ficaria errado para sempre.
    """

    def _cartao(self, mtimes):
        d = os.path.join(self.dir, "cartao")
        os.makedirs(os.path.join(d, "dtbs"), exist_ok=True)
        caminhos = []
        for i, quando in enumerate(mtimes):
            caminho = os.path.join(d, "arquivo%d" % i)
            with open(caminho, "w") as fh:
                fh.write("x")
            os.utime(caminho, (quando, quando))
            caminhos.append(caminho)
        return d

    def test_usa_o_mtime_mais_novo(self):
        # Um arquivo com data de 2017 (criado no proprio aparelho) e outros
        # dois com a data real do PC (2026). O MAIOR tem de vencer: o de 2017
        # e' justamente o que nao serve.
        d = self._cartao([EPOCA_2017, EPOCA_2026, EPOCA_2026 - 3600])
        r = self._rodar(S01, {
            "GIOCHI_RELOGIO_BOOT_DEV": os.path.join(d, "arquivo0"),
            "GIOCHI_RELOGIO_BOOT_MNT": d,
        })
        self.assertEqual(0, r.returncode, r.stderr)
        self.assertIn("date -s @%d" % EPOCA_2026, self._chamadas_date(),
                      "tinha de usar o mtime MAIS NOVO do cartao: %r"
                      % self._chamadas_date())

    def test_data_do_cartao_tambem_precisa_ser_plausivel(self):
        # Cartao com data absurda nao pode virar relogio.
        d = self._cartao([EPOCA_2017])
        r = self._rodar(S01, {
            "GIOCHI_RELOGIO_BOOT_DEV": os.path.join(d, "arquivo0"),
            "GIOCHI_RELOGIO_BOOT_MNT": d,
        })
        self.assertEqual(0, r.returncode, r.stderr)
        self.assertNotIn("date -s @%d" % EPOCA_2017,
                         " ".join(self._chamadas_date()))
        self.assertIn("sem data utilizavel", r.stdout + r.stderr)

    def test_epoca_salva_tem_prioridade_sobre_o_cartao(self):
        with open(os.path.join(self.user, ".giochi", "relogio"), "w") as fh:
            fh.write("%d\n" % (EPOCA_2026 + 86400))
        d = self._cartao([EPOCA_2026])
        self._rodar(S01, {
            "GIOCHI_RELOGIO_BOOT_DEV": os.path.join(d, "arquivo0"),
            "GIOCHI_RELOGIO_BOOT_MNT": d,
        })
        self.assertIn("date -s @%d" % (EPOCA_2026 + 86400),
                      self._chamadas_date(),
                      "a epoca salva e' mais precisa que a data do cartao")



class PedidoDeDataViaMtp(RelogioBase):
    """docs/RELÓGIO.md: o usuario corrige a data criando um arquivo via MTP.

    O MTP expoe /user ao PC; `relogio.pedido` e' a porta de entrada da data
    quando a bateria do RTC e' trocada e a epoca salva ficou velha. Aceita
    epoca (so' digitos) ou data legivel para `date -s`.
    """

    def _pedido(self, conteudo):
        caminho = os.path.join(self.user, ".giochi", "relogio.pedido")
        with open(caminho, "w", encoding="utf-8") as fh:
            fh.write(conteudo)
        return caminho

    def _salvo(self):
        caminho = os.path.join(self.user, ".giochi", "relogio")
        if os.path.exists(caminho):
            with open(caminho, encoding="utf-8") as fh:
                return fh.read().strip()
        return None

    def test_pedido_epoch_aplica_e_remove(self):
        pedido = self._pedido("%d\n" % EPOCA_2026)
        r = self._rodar(S01)
        self.assertEqual(0, r.returncode, r.stderr)
        self.assertIn("date -s @%d" % EPOCA_2026, self._chamadas_date(),
                      "a epoca do pedido tinha de ser aplicada: %r"
                      % self._chamadas_date())
        self.assertFalse(os.path.exists(pedido),
                         "pedido aplicado tem de ser REMOVIDO (senao "
                         "reaplica em todo boot)")
        self.assertIsNotNone(self._salvo(),
                             "a fonte persistente tinha de ser atualizada")

    def test_pedido_data_legivel_aplica_e_remove(self):
        pedido = self._pedido("2026-11-01 15:30\n")
        r = self._rodar(S01)
        self.assertEqual(0, r.returncode, r.stderr)
        self.assertIn("date -s 2026-11-01 15:30", self._chamadas_date(),
                      "a data legivel tinha de ir ao `date -s` como um "
                      "argumento: %r" % self._chamadas_date())
        self.assertFalse(os.path.exists(pedido))

    def test_pedido_com_crlf_e_aceito(self):
        # Arquivo criado no Windows chega com \r\n; o script remove o \r.
        pedido = self._pedido("%d\r\n" % EPOCA_2026)
        r = self._rodar(S01)
        self.assertEqual(0, r.returncode, r.stderr)
        self.assertIn("date -s @%d" % EPOCA_2026, self._chamadas_date())
        self.assertFalse(os.path.exists(pedido))

    def test_pedido_lixo_nao_aplica_e_mantem(self):
        pedido = self._pedido("nao sou uma data\n")
        r = self._rodar(S01)
        self.assertEqual(0, r.returncode, r.stderr)
        self.assertTrue(os.path.exists(pedido),
                        "pedido invalido fica no lugar: o aviso a cada boot "
                        "e' proposital, lembra que ha' algo para corrigir")
        self.assertIn("pedido de data invalido", r.stdout + r.stderr)

    def test_pedido_fora_de_faixa_mantem(self):
        pedido = self._pedido("1\n")
        r = self._rodar(S01)
        self.assertEqual(0, r.returncode, r.stderr)
        self.assertNotIn("date -s @1", " ".join(self._chamadas_date()))
        self.assertTrue(os.path.exists(pedido))
        self.assertIn("fora de faixa", r.stdout + r.stderr)

    def test_pedido_que_termina_em_ano_errado_e_rejeitado(self):
        # `date -s` aceitou a string, mas o relogio ficou em 2017: nao pode
        # persistir nem remover o pedido — e a epoca salva anterior volta.
        self._shim("date", """#!/bin/sh
echo "date $*" >> "%s"
case "$1" in
    -s)
        a="$2"
        case "$a" in
            @[0-9]*) exit 0 ;;
            [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]*) exit 0 ;;
            *) exit 1 ;;
        esac ;;
    +%%s) echo "%d"; exit 0 ;;
    +%%Y) echo "2017"; exit 0 ;;
    -u)
        case "$2" in
            +%%Y) echo "2017"; exit 0 ;;
            *) exit 0 ;;
        esac ;;
esac
exit 0
""" % (self.chamadas, EPOCA_2026))
        with open(os.path.join(self.user, ".giochi", "relogio"), "w") as fh:
            fh.write("%d\n" % (EPOCA_2026 - 86400))
        pedido = self._pedido("2026-11-01 15:30\n")
        r = self._rodar(S01)
        self.assertEqual(0, r.returncode, r.stderr)
        self.assertTrue(os.path.exists(pedido),
                        "pedido com resultado implausivel NAO e' removido")
        self.assertIn("date -s @%d" % (EPOCA_2026 - 86400),
                      self._chamadas_date(),
                      "a epoca salva anterior tinha de ser restaurada: %r"
                      % self._chamadas_date())
        self.assertIn("rejeitado", r.stdout + r.stderr)

    def test_pedido_tem_prioridade_sobre_epoca_salva(self):
        with open(os.path.join(self.user, ".giochi", "relogio"), "w") as fh:
            fh.write("%d\n" % (EPOCA_2026 - 86400))
        self._pedido("%d\n" % EPOCA_2026)
        r = self._rodar(S01)
        self.assertEqual(0, r.returncode, r.stderr)
        ordem = [l for l in self._chamadas_date()
                 if l.startswith("date -s")]
        self.assertTrue(ordem, "nenhuma aplicacao registrada")
        self.assertEqual("date -s @%d" % EPOCA_2026, ordem[0],
                         "o pedido do usuario tem de ser a PRIMEIRA "
                         "aplicacao, antes da epoca salva: %r" % ordem)


class RcKGuarda(RelogioBase):
    """rcK: grava `date +%s` antes de desmontar a particao de dados."""

    def test_grava_epoca_plausivel(self):
        r = self._rodar(RCK)
        self.assertEqual(0, r.returncode, r.stderr)
        caminho = os.path.join(self.user, ".giochi", "relogio")
        self.assertTrue(os.path.exists(caminho),
                        "o rcK tinha de gravar o relogio: %r" % r.stderr)
        with open(caminho) as fh:
            self.assertEqual(str(EPOCA_2026), fh.read().strip())

    def test_grava_antes_de_desmontar(self):
        with open(RCK, encoding="utf-8") as fh:
            fonte = fh.read()
        # O `umount` de /user tem de vir DEPOIS da gravacao, senao o arquivo
        # some junto com o ponto de montagem.
        pos_gravacao = fonte.find("relogio\" 2>/dev/null")
        pos_umount = fonte.find('for fs in /user /user2')
        self.assertGreater(pos_gravacao, 0, "nao achei a gravacao do relogio")
        self.assertGreater(pos_umount, pos_gravacao,
                           "o umount vem antes da gravacao: o relogio seria "
                           "escrito num diretorio ja' desmontado")

    def test_relogio_implausivel_nao_e_salvo(self):
        # Se o relogio esta' em 2017 (ou pior), salvar perpetuaria o erro.
        self._shim("date", """#!/bin/sh
echo "date $*" >> "%s"
case "$1" in
    +%%s) echo "1"; exit 0 ;;
esac
exit 0
""" % self.chamadas)
        r = self._rodar(RCK)
        self.assertEqual(0, r.returncode, r.stderr)
        caminho = os.path.join(self.user, ".giochi", "relogio")
        self.assertFalse(os.path.exists(caminho),
                         "nao pode salvar epoca 1 (1970): %r" % r.stderr)
        self.assertIn("implausivel", r.stderr)


class OrdemNoBoot(unittest.TestCase):
    """A ordem importa: o log de boot tem de nascer com a data certa."""

    def test_rcs_roda_relogio_antes_do_bootlog(self):
        with open(os.path.join(ROOT, "buildroot/board/giochi/rootfs_overlay",
                               "etc/init.d/rcS"), encoding="utf-8") as fh:
            fonte = fh.read()
        pos_relogio = fonte.find("S01relogio")
        pos_bootlog = fonte.find("S00bootlog start")
        self.assertGreater(pos_relogio, 0, "rcS nao chama S01relogio")
        self.assertLess(
            pos_relogio, pos_bootlog,
            "S01relogio tem de rodar ANTES do S00bootlog, senao o proprio log "
            "de boot nasce datado de 2017")

    def test_rcs_nao_roda_relogio_duas_vezes(self):
        with open(os.path.join(ROOT, "buildroot/board/giochi/rootfs_overlay",
                               "etc/init.d/rcS"), encoding="utf-8") as fh:
            fonte = fh.read()
        self.assertIn("*/S01relogio) continue", fonte,
                      "sem o `continue` o laco chama o S01relogio uma segunda "
                      "vez (a armadilha do S10mdev, AGENTS.md 3.45)")

    def test_script_esta_nas_listas_obrigatorias(self):
        for caminho, motivo in (
            ("buildroot/board/giochi/post-build.sh",
             "sem entrar em ini_list o script nao vai para a imagem"),
            ("tools/check-tracked.sh",
             "sem entrar na lista o lint nao acusa se ele sumir do git"),
        ):
            with open(os.path.join(ROOT, caminho), encoding="utf-8") as fh:
                self.assertIn("S01relogio", fh.read(), motivo)


if __name__ == "__main__":
    unittest.main(verbosity=2)
