"""Guardas do `tools/giochi-usb-diag.sh` — a ferramenta que responde "o USB subiu?".

O defeito que originou esta suíte foi medido: o `--cartao` imprimia só as
últimas 25 linhas do `giochi-boot.log`. Num log real (1 141 linhas) a sonda do
`S50usb` fica **centenas de linhas antes do fim**, então a ferramenta feita
para responder "o gadget ancorou?" nunca mostrava a resposta — e a conclusão
saía como "não há dado", que é pior que "há dado e ele diz X".

Cada caso abaixo corresponde a um estado do controlador que pede uma ação
diferente. Nenhum deles é hipotético: todos saíram do log do aparelho ou do
código do kernel 6.12 desta árvore.
"""

import os
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIAG = os.path.join(ROOT, "tools/giochi-usb-diag.sh")


def read(path):
    with open(path, encoding="utf-8") as fp:
        return fp.read()


def extrai_usb_do_log():
    """A função `usb_do_log`, isolada, para rodar sem montar cartão.

    Chamar o script inteiro exigiria um cartão montado em /run/media; isolar a
    função testa a MESMA classificação que o usuário vê.
    """
    script = read(DIAG)
    ini = script.index("usb_do_log() {")
    fim = script.index("\n}\n", ini) + 3
    return script[ini:fim]


def classifica(linhas):
    """Roda usb_do_log sobre um log sintético e devolve a saída."""
    with tempfile.TemporaryDirectory() as tmp:
        log = os.path.join(tmp, "giochi-boot.log")
        with open(log, "w", encoding="utf-8") as fp:
            fp.write("[    0.000000] Booting Linux\n")
            fp.write("\n".join(linhas) + "\n")
            # Rabo longo: é o que fazia a sonda ficar fora do `tail -25`.
            fp.write("\n".join("[    9.9] filler" for _ in range(800)) + "\n")

        harness = os.path.join(tmp, "h.sh")
        with open(harness, "w", encoding="utf-8") as fp:
            fp.write(extrai_usb_do_log())
            fp.write('\nlog() { printf "%s\\n" "$*"; }\n')
            fp.write('usb_do_log "$1"\n')

        proc = subprocess.run(["sh", harness, log],
                              stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=60)
        return proc.stdout.decode("utf-8", "replace"), \
            proc.stderr.decode("utf-8", "replace")


class UsbDiagTest(unittest.TestCase):
    def test_script_existe(self):
        self.assertTrue(os.path.isfile(DIAG))

    def test_sonda_aparece_mesmo_longe_do_fim_do_log(self):
        """O bug medido: `tail -25` escondia a sonda de um log de 1100 linhas."""
        saida, _ = classifica([
            "S50usb: sonda: /sys/class/udc VAZIO (controlador sem modo "
            "dispositivo)",
            "S50usb: sonda: dr_mode USB-C = otg",
        ])
        self.assertIn("udc VAZIO", saida,
                      "a sonda nao foi encontrada: a ferramenta voltou a "
                      "olhar so' o fim do log")
        self.assertIn("dr_mode USB-C = otg", saida)

    def test_peripheral_sem_udc_aponta_o_probe_do_phy(self):
        """DT certo e nenhum UDC = o probe do dwc3/PHY nao completou.

        É o estado mais confuso: quem le "peripheral" conclui que o patch
        resolveu. Não resolveu — o controlador nem apareceu.
        """
        saida, _ = classifica([
            "S50usb: sonda: /sys/class/udc VAZIO (controlador sem modo "
            "dispositivo)",
            "S50usb: sonda: dr_mode USB-C = peripheral",
        ])
        self.assertIn("probe do dwc3/PHY nao completou", saida)

    def test_dr_mode_otg_aponta_o_patch_de_dt(self):
        saida, _ = classifica([
            "S50usb: sonda: UDC(s):fcc00000.usb ",
            "S50usb: sonda: dr_mode USB-C = otg",
        ])
        self.assertIn("DT ainda esta' em OTG", saida)

    def test_ep0out_menos_110_e_timeout(self):
        """-110 decide o diagnóstico; a mensagem sem número não decide nada.

        Sem o código, `-ETIMEDOUT`, `-EINVAL` (NO_RESOURCE) e `-EAGAIN`
        (BUS_EXPIRY) parecem o mesmo problema e pedem ações opostas.
        """
        saida, _ = classifica([
            "dwc3 fcc00000.usb: failed to enable ep0out: -110",
        ])
        self.assertIn("TIMEOUT", saida)
        self.assertIn("PRTCAPDIR", saida)

    def test_ep0out_menos_22_e_no_resource(self):
        saida, _ = classifica([
            "dwc3 fcc00000.usb: failed to enable ep0out: -22",
        ])
        self.assertIn("NO_RESOURCE", saida)

    def test_ep0out_sem_codigo_avisa_que_a_imagem_e_antiga(self):
        saida, _ = classifica([
            "dwc3 fcc00000.usb: failed to enable ep0out",
        ])
        self.assertIn("patch 0004", saida)
        self.assertNotIn("TIMEOUT", saida,
                         "sem o codigo de erro nao se pode afirmar que e' "
                         "timeout")

    def test_bind_ok_e_reconhecido(self):
        saida, _ = classifica([
            "S50usb: sonda: UDC(s):fcc00000.usb ",
            "S50usb: sonda: dr_mode USB-C = peripheral",
            "S50usb: gadget anexado ao UDC 'fcc00000.usb'",
        ])
        self.assertIn("bind: OK", saida)
        self.assertIn("dr_mode USB-C = peripheral", saida)

    def test_bind_falho_e_reconhecido(self):
        saida, _ = classifica([
            "S50usb: gadget NAO anexado: nenhum UDC aceitou o bind",
        ])
        self.assertIn("bind: FALHOU", saida)

    def test_log_sem_sonda_avisa_que_a_imagem_e_antiga(self):
        saida, _ = classifica(["[    6.0] so' linhas de boot"])
        self.assertIn("sem sonda", saida)

    def test_o_script_nao_faz_tail_apenas(self):
        """Guarda de regressão do defeito: a sonda tem de ser extraida.

        `tail` sozinho passa por todos os outros testes se o log for curto —
        por isso a verificação é no texto do script.
        """
        corpo = read(DIAG)
        self.assertIn("usb_do_log", corpo)
        ini = corpo.index("ler_cartao() {")
        fim = corpo.index("\n}\n", ini)
        corpo_ler = corpo[ini:fim]
        self.assertIn("usb_do_log", corpo_ler,
                      "ler_cartao() voltou a so' fazer tail")


if __name__ == "__main__":
    unittest.main()
