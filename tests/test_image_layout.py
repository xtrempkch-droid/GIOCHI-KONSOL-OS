#!/usr/bin/env python3
"""Contrato do layout do cartao (genimage.cfg) — Giochi Konsol OS v1.2.

Motivo de existir: o `genimage.cfg` declarava **8 MiB** para a regiao raw do
`u-boot-rockchip.bin`, mas o artefato medido no build real tem **8,97 MiB** (o
`u-boot.itb` comeca em 6 MiB). O build compilava U-Boot, kernel e rootfs por
1-3 h e morria no ultimo passo com "image too big for partition".

O `post-image.sh` agora confere isso em tempo de build, mas essa conferencia so
acontece depois de compilar tudo. Este teste roda em `make test` e pega o mesmo
erro em segundos, sem Buildroot e sem `genimage`.

Os tamanhos de referencia vem de medicoes reais (docs/BUILD.md), nao de
estimativa:

    u-boot-rockchip.bin   9 401 856 B  (8,97 MiB)
    Image (kernel 6.12)  47 516 160 B  (45,3 MiB)
    boot.vfat                64 MiB    (SIZE_MB do mkbootimg.sh)
    rootfs.squashfs      28-35 MB      (orcamento da secao 8: <= 150 MB)
"""

import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOARD = os.path.join(ROOT, "buildroot", "board", "giochi")
CFGS = ["genimage.cfg", "genimage-no-uboot.cfg"]

# Medido no build real da X55. A margem existe para nao voltar a ficar no fio.
UBOOT_MIN = 12 * 1024 * 1024
BOOT_VFAT_MIN = 64                # SIZE_MB do mkbootimg.sh
KERNEL_IMAGE = 47516160
SYSTEM_MIN = 150                  # orcamento da secao 8 (SquashFS <= 150 MB)

SIZE_UNITS = {"K": 1024, "M": 1024 ** 2, "G": 1024 ** 3, "": 1}


def parse_size(text):
    m = re.match(r"^(\d+)([KMGTkmgt]?)$", text.strip())
    if not m:
        raise ValueError("tamanho nao reconhecido no genimage.cfg: %r" % text)
    n = int(m.group(1))
    unit = m.group(2).upper()
    if unit == "T":
        return n * 1024 * 1024 * 1024
    return n * SIZE_UNITS[unit]


def parse_cfg(path):
    """Le as particoes do genimage.cfg: dicts com name/image/offset/size."""
    parts = []
    cur = None
    with open(path, encoding="utf-8") as fh:
        for raw in fh:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            m = re.match(r"partition\s+(\S+)\s*\{", line)
            if m:
                cur = {"name": m.group(1), "image": None, "offset": 0,
                       "size": None, "offset_declared": False}
                parts.append(cur)
                continue
            if cur is None:
                continue
            m = re.match(r"(image|offset|size)\s*=\s*\"?([^\";]+)\"?", line)
            if not m:
                continue
            key, val = m.group(1), m.group(2).strip()
            if key == "image":
                cur["image"] = val
            elif key == "offset":
                cur["offset"] = parse_size(val)
                cur["offset_declared"] = True
            else:
                cur["size"] = parse_size(val)
    return parts


def layout(parts):
    """Resolve os offsets implicitos e devolve [(nome, ini, fim)]."""
    out = []
    cursor = 0
    for p in parts:
        start = max(cursor, p["offset"]) if p["offset_declared"] else cursor
        end = start + p["size"]
        out.append((p["name"], start, end))
        cursor = end
    return out


class ImageLayoutTest(unittest.TestCase):
    def each_cfg(self):
        for name in CFGS:
            path = os.path.join(BOARD, name)
            self.assertTrue(os.path.isfile(path), "%s ausente" % name)
            yield name, parse_cfg(path)

    def test_toda_particao_tem_tamanho(self):
        # O post-image.sh deriva o user.ext4 do cfg: sem 'size' nao ha como.
        for name, parts in self.each_cfg():
            self.assertTrue(parts, "%s sem particoes" % name)
            for p in parts:
                self.assertIsNotNone(
                    p["size"],
                    "%s: particao '%s' sem 'size'" % (name, p["name"]))

    def test_particoes_nao_se_sobrepoem(self):
        for name, parts in self.each_cfg():
            pieces = layout(parts)
            for (n1, _, end1), (n2, start2, _) in zip(pieces, pieces[1:]):
                self.assertLessEqual(
                    end1, start2,
                    "%s: '%s' termina em %d mas '%s' comeca em %d"
                    % (name, n1, end1, n2, start2))

    def test_uboot_cabe_na_regiao_raw(self):
        parts = dict((p["name"], p) for p in
                     parse_cfg(os.path.join(BOARD, "genimage.cfg")))
        self.assertIn("u-boot", parts, "genimage.cfg sem regiao raw de u-boot")
        self.assertGreaterEqual(
            parts["u-boot"]["size"], UBOOT_MIN,
            "regiao do u-boot menor que o artefato medido (8,97 MiB)")
        self.assertEqual(parts["u-boot"]["offset"], 32 * 1024,
                         "o U-Boot mainline do RK3566 grava em 32 KiB")

    def test_regiao_do_uboot_nao_invade_a_particicao_de_boot(self):
        parts = parse_cfg(os.path.join(BOARD, "genimage.cfg"))
        uboot = next(p for p in parts if p["name"] == "u-boot")
        boot = next(p for p in parts if p["name"] == "boot")
        self.assertGreaterEqual(
            boot["offset"], uboot["offset"] + uboot["size"],
            "a particao de boot comeca antes do fim da regiao do u-boot")
        # O setor de backup do GPT vive nos ultimos 33 setores do primeiro MiB.
        self.assertGreaterEqual(boot["offset"], 8 * 1024 * 1024,
                                "boot abaixo de 8 MiB colide com o GPT")

    def test_layout_sem_uboot_reserva_a_faixa_do_vendor(self):
        # Procedimento documentado: gravar o bootloader do vendor em 32 KiB e a
        # imagem a partir de 8 MiB. Logo a primeira particao tem de comecar
        # nesse offset (nem antes, nem depois).
        parts = dict((p["name"], p) for p in
                     parse_cfg(os.path.join(BOARD, "genimage-no-uboot.cfg")))
        self.assertNotIn("u-boot", parts)
        self.assertEqual(parts["boot"]["offset"], 8 * 1024 * 1024)

    def test_particicao_de_boot_cabe_o_kernel(self):
        # boot.vfat carrega Image + dtb + boot.scr + splash, e o mkbootimg.sh
        # cria um FAT de 64 MB: a particao precisa ser >= a isso, e o proprio
        # boot.vfat precisa caber o Image medido com folga.
        kernel_mb = (KERNEL_IMAGE + 1048575) // 1048576      # 46 MB
        self.assertLess(kernel_mb + 8, BOOT_VFAT_MIN,
                        "o boot.vfat de 64 MB nao cabe mais o Image medido")
        for name, parts in self.each_cfg():
            boot = next(p for p in parts if p["name"] == "boot")
            self.assertGreaterEqual(
                boot["size"] / 1048576, BOOT_VFAT_MIN,
                "%s: particao de boot menor que o boot.vfat do mkbootimg.sh"
                % name)

    def test_slots_a_b_tem_o_mesmo_tamanho(self):
        # A/B so funciona com os dois slots iguais (o update escreve na copia
        # inativa). Alem disso a distancia A->B e o que o xz usa como janela de
        # deduplicacao no post-image.sh.
        for name, parts in self.each_cfg():
            by = dict((p["name"], p) for p in parts)
            self.assertEqual(by["system-a"]["size"], by["system-b"]["size"],
                             "%s: slots A e B com tamanhos diferentes" % name)
            self.assertGreaterEqual(
                by["system-a"]["size"] / 1048576, SYSTEM_MIN,
                "%s: slot menor que o orcamento (150 MB)" % name)

    def test_user_e_gravavel_com_no_minimo_512m(self):
        for name, parts in self.each_cfg():
            by = dict((p["name"], p) for p in parts)
            self.assertGreaterEqual(
                by["user"]["size"] / 1048576, 512,
                "%s: particao C abaixo do minimo de 512 MB" % name)

    def test_post_image_nao_duplica_os_tamanhos(self):
        # O tamanho das particoes tem UMA fonte (o cfg). Se o post-image.sh
        # voltar a ter um default proprio de tamanho, um dos dois fica errado
        # sem ninguem notar (era USER_SIZE_MB=512 no script e 512M no cfg).
        path = os.path.join(BOARD, "post-image.sh")
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        self.assertNotIn("USER_SIZE_MB:-", text,
                         "post-image.sh tem tamanho padrao proprio; use o cfg")
        self.assertIn('part_size "$GENIMAGE_CFG" user', text)
        self.assertIn("fits u-boot-rockchip.bin", text,
                      "post-image.sh nao confere o tamanho do u-boot")


if __name__ == "__main__":
    unittest.main()
