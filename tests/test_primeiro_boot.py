"""Invariantes do primeiro boot sadio — cada uma veio de um defeito medido.

Todos os itens abaixo apareceram no log serial de um boot real da X55 (imagem
de 2026-10-31) e nenhum deles impedia o sistema de subir: o kernel entrava, o
`giochi-init` subia, o Launcher desenhava — e o usuário via a tela apagada, sem
biblioteca, sem música e com o mapa de botões errado. Por isso viram teste.

O que este módulo NÃO cobre: se o DRM faz bind do painel (isso é kernel, ver
AGENTS.md §3.44) e se o cartão grava bem (tests/test_verify_card.py).
"""

import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

RCS = os.path.join(ROOT, "buildroot/board/giochi/rootfs_overlay/etc/init.d/rcS")
POST_BUILD = os.path.join(ROOT, "buildroot/board/giochi/post-build.sh")
S10STORAGE = os.path.join(ROOT, "scripts/init.d/S10storage")
LAUNCHER_MAIN = os.path.join(ROOT, "src/launcher/main.cpp")
DEVICECONF_C = os.path.join(ROOT, "src/common/giochi_deviceconf.c")
BUILD_IMAGE = os.path.join(ROOT, "tools/build-image.sh")
S05MODULES = os.path.join(ROOT, "scripts/init.d/S05modules")
S50USB = os.path.join(ROOT, "scripts/init.d/S50usb")
S55USBNET = os.path.join(ROOT, "scripts/init.d/S55usbnet")
USB_MODE = os.path.join(ROOT, "scripts/giochi-usb-mode")
MT_PD = os.path.join(ROOT, "buildroot/package/giochi-mtpd/giochi-mtpd")
LIB_SH = os.path.join(ROOT,
                      "buildroot/board/giochi/rootfs_overlay/etc/giochi/lib.sh")
MDEV_CONF = os.path.join(ROOT, "buildroot/board/giochi/mdev.conf")
BUSYBOX_FRAGMENT = os.path.join(ROOT, "buildroot/board/giochi/busybox.fragment")
LINUX_POWKIDDY = os.path.join(ROOT, "buildroot/board/giochi/linux-powkiddy.fragment")
GRAPHICS = os.path.join(ROOT, "scripts/init.d/S90graphics")
INITTAB = os.path.join(ROOT, "buildroot/board/giochi/rootfs_overlay/etc/inittab")
TOOLS_MK = os.path.join(ROOT, "buildroot/package/giochi-tools/giochi-tools.mk")


def read(path):
    with open(path, encoding="utf-8") as fp:
        return fp.read()


def sh_code(text):
    """Remove comentários de um script sh.

    Necessário porque o cabeçalho destes scripts CITA o código que ele
    explica: uma guarda que casa com o texto inteiro passa mesmo com o laço
    removido (testado ao contrário).
    """
    out = []
    for linha in text.splitlines():
        despojada = linha.lstrip()
        if despojada.startswith("#"):
            continue
        out.append(linha)
    return "\n".join(out)


class PrimeiroBootTest(unittest.TestCase):
    def test_mesa_nao_forca_driver_de_render(self):
        """O driver do barramento e' o kmsro, nao o panfrost.

        Medido na X55: o S90graphics exportava
        `MESA_LOADER_DRIVER_OVERRIDE=panfrost`. Nesta placa o dispositivo DRM e'
        o *display* (rockchip-drm); quem fala com ele e' o `rockchip_dri.so`
        (kmsro), que delega a RENDERIZACAO ao panfrost. Forcando `panfrost`, o
        Mesa carrega o driver de render direto contra o no' do display, o EGL
        falha e o SDL so' diz "EGL not initialized" — tela apagada com o DRM
        bindado.
        """
        codigo = sh_code(read(GRAPHICS))

        self.assertNotIn(
            "MESA_LOADER_DRIVER_OVERRIDE=panfrost", codigo,
            "S90graphics volta a forcar o driver de RENDER: o kmsro "
            "(rockchip_dri.so) e' quem fala com o display")
        # Unset explicito no caminho padrao: e' o que deixa o kmsro decidir.
        self.assertIn("unset MESA_LOADER_DRIVER_OVERRIDE", codigo)
        # Escape hatch para depurar sem recompilar.
        self.assertIn("GIOCHI_MESA_OVERRIDE", codigo)

    def test_sonda_que_nao_roda_nao_vira_sucesso(self):
        """Sonda ausente devolvia 0, e o script logava "driver passou".

        Medido no harness: `[ -x /usr/bin/giochi-gpu-probe ] || return 0` fazia
        o caminho "nao testado" ser lido como "aprovado". O log afirmava um
        resultado que ninguem mediu — o tipo de mentira que manda a
        investigacao para o lado errado.
        """
        codigo = sh_code(read(GRAPHICS))

        self.assertIn("return 2", codigo,
                      "sonda precisa distinguir 'nao testado' de 'passou'")
        self.assertIn("sonda nao executada", codigo)
        # O else nao pode disparar o fallback de software quando nada rodou.
        self.assertIn("case $?", codigo)

    def test_software_render_e_o_fallback(self):
        """Sem swrast nao existe NENHUM caminho quando o panfrost nao sobe.

        Medido na imagem da X55: /usr/lib/dri nao tinha
        kms_swrast_dri.so nem swrast_dri.so. Com panfrost como unico driver,
        uma falha dele derrubava o EGL para sempre, sem alternativa.
        """
        codigo = sh_code(read(GRAPHICS))

        self.assertIn("kms_swrast", codigo,
                      "S90graphics perdeu o fallback de software")

        for template in ("base-rk3566.defconfig", "base-rk3326.defconfig"):
            caminho = os.path.join(ROOT, "buildroot/board/giochi/defconfig",
                                   template)
            self.assertIn(
                "BR2_PACKAGE_MESA3D_GALLIUM_DRIVER_SWRAST=y", read(caminho),
                f"{template}: sem swrast, uma falha do panfrost nao tem "
                f"para onde cair")

        # E o defconfig GERADO tem de acompanhar (esquecer `make defconfigs`
        # deixa o template certo e a imagem errada).
        for aparelho in ("x55", "x35s", "x35h"):
            gerado = os.path.join(ROOT, "buildroot/configs",
                                  f"giochi_{aparelho}_defconfig")
            self.assertIn(
                "BR2_PACKAGE_MESA3D_GALLIUM_DRIVER_SWRAST=y", read(gerado),
                f"{aparelho}: defconfig gerado sem swrast (rode make defconfigs)")

    def test_splash_e_marcador_de_diagnostico(self):
        """O splash e' o teste "o SDL desenha?" — e tem de deixar rastro.

        Se o Launcher nao abre o SDL, o splash tambem nao aparece: nesse caso o
        log e' a unica testemunha. Por isso os dois avisos sao obrigatorios:
        "splash: exibindo N quadros" e o do driver de video escolhido.
        """
        render = read(os.path.join(ROOT, "src/launcher/render.cpp"))

        self.assertIn("GIOCHI_SPLASH_ALWAYS", render,
                      "sem modo permanente nao da' para isolar o SDL da UI")
        self.assertIn("splash: exibindo", render)
        self.assertIn("render: SDL2 driver=", render,
                      "sem o driver no log, 'nao desenhou' nao tem culpado")
        # Default antigo (45 quadros) e' curto demais para o olho achar a tela
        # ligando e concluir que algo foi desenhado.
        self.assertNotIn("splash_frames_from_env(45)", render)
        self.assertIn("splash_frames_from_env(180)", render)

        # Falha de video tem de devolver o console: tela preta nao e' pista.
        self.assertIn("devolver_console_de_video(", render)
        self.assertIn("/sys/class/vtconsole/vtcon%d/bind", render)

        # O S90graphics liga o splash por 10 s (tempo de olhar e ver) e deixa
        # desligar por marcador, sem recompilar — mesmo padrao do `nolog`.
        script = sh_code(read(GRAPHICS))
        self.assertIn("GIOCHI_SPLASH_FRAMES=600", script)
        self.assertIn("nosplash", script)
        self.assertIn("/boot/nosplash", script)

    def test_geometria_usa_o_layout_e_nao_a_saida_fisica(self):
        """Medido na X55: painel 720x1280 em retrato, layout 1280x720.

        A primeira versao calculava a geometria sobre `SDL_GetRendererOutputSize`
        (a saida fisica). Resultado no aparelho: escala 2,66x, uma coluna e a UI
        em pe' — o "layout bugado" que o mantenedor viu. A geometria tem de sair
        do tamanho LOGICO (layout.json) e a orientacao e' resolvida girando na
        apresentacao.
        """
        render = read(os.path.join(ROOT, "src/launcher/render.cpp"))

        self.assertIn("ui.layout->width", render)
        self.assertIn("calcular_geometria", render)
        self.assertIn("ui_present(", render,
                      "sem ui_present a orientacao fica do lado do kernel, "
                      "que nao gira nada")
        self.assertIn("SDL_RenderCopyEx", render,
                      "so' RenderCopyEx sabe girar; RenderCopy nao")
        # A rotacao tem de vir do aparelho, nao fixa no codigo.
        self.assertIn("GIOCHI_ROTATION", render)

        script = read(GRAPHICS)
        self.assertIn('export GIOCHI_ROTATION="$ROTATION"', script,
                      "sem exportar a rotacao o Launcher gira no sentido errado")
        # Escape hatch de campo: acertar 90 x 270 sem recompilar 40 min.
        self.assertIn("/boot/rotate", script)

    def test_rotacao_do_console_vem_do_device_conf(self):
        """Uma fonte para as duas rotacoes: UI e console de video.

        Medido no aparelho: o mantenedor relatou "a orientacao ficou ok, mas o
        terminal tambem girou" — porque a UI girava por um caminho (ui_present)
        e o console por outro (`setenv giochi_fbcon` a mao, no U-Boot). Duas
        rotacoes independentes que concordassem era sorte, nao desenho.
        Agora o post-image.sh deriva `fbcon=rotate:N` de GIOCHI_ROTATION/90, a
        MESMA fonte que alimenta o ui_present.
        """
        cmd = read(os.path.join(ROOT, "buildroot/board/giochi/boot.cmd"))
        post = read(POST_BUILD.replace("post-build.sh", "post-image.sh"))

        self.assertIn("@FBCON_ROTATE@", cmd,
                      "boot.cmd sem o marcador: o console fica com rotacao fixa")
        self.assertIn("@FBCON_ROTATE@", post,
                      "post-image.sh nao substitui o marcador")
        # A convencao: fbcon 0..3 = 0/90/180/270, igual ao SDL.
        for graus, indice in (("90", "1"), ("180", "2"), ("270", "3")):
            achou = re.search(
                rf"^\s*{graus}\)\s+FBCON_ROTATE={indice}\b", post, re.M)
            self.assertIsNotNone(
                achou,
                f"mapeamento {graus} graus -> rotate:{indice} sumiu")
        # Substituicao num arquivo temporario: nunca editar o boot.cmd original
        # (ele e' versionado e serve todos os aparelhos).
        self.assertIn("boot.cmd.gerado", post)

    def test_launcher_le_rotacao_do_device_conf(self):
        """Quem inicia o Launcher NAO pode decidir a orientacao.

        O giochi-init pode ser iniciado pelo rcS (via S90graphics) ou pelo
        inittab — e so' o caminho do rcS exportava GIOCHI_ROTATION. Ler do
        /etc/giochi/device.conf (regra da §3.47) torna o resultado o mesmo
        qualquer que seja o caminho.
        """
        render = read(os.path.join(ROOT, "src/launcher/render.cpp"))

        self.assertIn("giochi_deviceconf_get", render)
        self.assertIn("GIOCHI_ROTATION", render)
        self.assertIn("GIOCHI_DEVICECONF_PATH", render)

    def test_falha_de_video_avisa_na_tela(self):
        """Tela preta nao e' diagnostico: a falha escreve no /dev/tty1.

        Sem `console=tty0` no bootargs, o printk vai so' para o serial —
        re-bindar o fbcon, sozinho, NAO mostra nada. Medido: o bootargs desta
        imagem tem apenas `console=ttyS2`.
        """
        render = read(os.path.join(ROOT, "src/launcher/render.cpp"))

        self.assertIn("/dev/tty1", render,
                      "a mensagem de falha precisa ir para o VT")
        self.assertIn("nao iniciou", render)
        self.assertIn("giochi-boot.log", render,
                      "a mensagem tem de dizer ONDE esta' o log")

    def test_sonda_reporta_render_node_e_drivers_dri(self):
        """A sonda tem de dizer o que o Mesa PODE carregar.

        "EGL not initialized" cobre tres causas com correcoes opostas: sem
        render node, driver de render ausente, driver de display forcado
        errado. A sonda lista os dois inventarios antes de tentar o EGL.
        """
        probe = read(os.path.join(ROOT, "src/tools/gpu_probe.c"))

        self.assertIn("/dev/dri", probe)
        self.assertIn("renderD", probe)
        self.assertIn("kms_swrast_dri.so", probe)
        self.assertIn("rockchip_dri.so", probe)

    def test_rcs_nao_chama_servico_duas_vezes(self):
        """'$script' start || '$script' rodava o serviço sem argumento.

        Foi assim que o boot da X55 imprimiu
        "Usage: /etc/init.d/S10mdev {start|stop|restart}": o `start` do
        S10mdev saía != 0 (não há modprobe no rootfs) e o fallback chamava o
        script de novo, sem argumento.
        """
        body = read(RCS)

        self.assertNotRegex(
            body,
            r'"?\$script"?\s+start\s*\|\|',
            "rcS voltou a chamar o serviço duas vezes (start || sem argumento)",
        )
        self.assertIn('"$script" start', body)
        self.assertIn("if ! \"$script\" start; then", body)

    def test_s10mdev_nao_entra_na_imagem(self):
        """Sem modprobe, o S10mdev só gera erro — quem carrega módulo é o hotplug."""
        body = read(POST_BUILD)
        match = re.search(r"for conflito in ([^;]+); do", body)

        self.assertIsNotNone(match, "post-build.sh sem a lista de conflitos")
        conflitos = match.group(1).split()
        for nome in ("S01syslogd", "S02klogd", "S50dropbear", "S10mdev"):
            self.assertIn(nome, conflitos, f"{nome} deveria sair da imagem")

    def test_arvore_de_user_e_criada(self):
        """/user é o ponto de montagem de um ext4 VAZIO.

        Os diretórios criados no SquashFS ficam escondidos depois do mount, e
        o Launcher registrou "'/user/games' inacessivel" → 0 pacotes.
        """
        body = read(S10STORAGE)

        for sub in ("music", "games", "ports", "saves"):
            self.assertIn(f'"$_u/{sub}"', body)
        self.assertIn("seed_user_tree", body)
        # A chamada tem de existir, não só a definição.
        self.assertGreaterEqual(body.count("seed_user_tree"), 2)
        # Prova de escrita de verdade: mkdir -p num diretório que já existe no
        # SquashFS devolve 0 mesmo com a partição fora do ar.
        self.assertIn("boot-write-test", body)

    def test_launcher_le_device_conf_pela_chave_inteira(self):
        """A chave tem 19 caracteres; o código pulava 20 e lia "efault"."""
        body = read(LAUNCHER_MAIN)

        self.assertIn("giochi_deviceconf_get", body)
        self.assertNotIn('.find("GIOCHI_BUTTONS_MAP="', body)
        self.assertNotRegex(
            body,
            r"pos\s*\+\s*20",
            "voltou a pular 20 caracteres depois da chave de 19",
        )

    def test_deviceconf_recusa_truncar_valor(self):
        """Valor maior que o buffer devolve fallback, não uma string cortada."""
        body = read(DEVICECONF_C)

        self.assertIn("strlen(value) + 1 > out_size", body)
        self.assertIn("line[key_len] != '='", body)

    def test_build_ressincroniza_pacote_local(self):
        """Pacote local com .stamp_rsynced NÃO trazia código novo para a imagem.

        Sintoma medido: as correções do commit 39d17b1 estavam no repositório,
        `make` terminou sem erro, e o aparelho continuou registrando
        `(device=rk3326 soc=rk3326)` e `buttons.map: secao [efault]` — porque o
        Buildroot só roda o rsync de `SITE_METHOD = local` quando reconstrói o
        pacote, e a versão (1.2) não muda.

        Este teste falha se alguém remover o `rm` dos stamps.
        """
        body = read(BUILD_IMAGE)

        self.assertIn("build/build/giochi-*", body,
                      "build-image.sh não limpa os pacotes locais")
        self.assertRegex(body, r'rm -f "\$pkg"/\.stamp_\*',
                         "build-image.sh não apaga os stamps do pacote local")

    def test_coldplug_de_modulos_no_boot(self):
        """Sem coldplug no espaço de usuário, nenhum .ko é carregado.

        Medido no boot #3 da X55: `/lib/modules/6.12.0/` cheio e
        `dw-mipi-dsi-rockchip: failed to get mipi dphy: -517`, ALSA sem placa,
        nenhum `rtw88`. O kernel só chama `request_module()` depois que o rootfs
        existe, e os dispositivos de plataforma já nasceram antes disso.
        """
        script = sh_code(read(S05MODULES))
        mdev = sh_code(read(MDEV_CONF))

        # `mdev -s` NAO faz coldplug de modulo: o -s do BusyBox só cria nós,
        # com make_device(NULL, path, OP_add) — sem MODALIAS no ambiente. A
        # regra $MODALIAS do mdev.conf só casa em evento de HOTPLUG. Medido no
        # aparelho: "coldplug concluido (0 modulo(s) carregado(s))" com 1268
        # .ko na imagem.
        self.assertRegex(script,
                         r"/sys/bus/\*/devices/\*/modalias",
                         "S05modules tem de varrer os modalias (como o "
                         "udevadm trigger), nao confiar no 'mdev -s'")
        # A varredura tem de CHAMAR modprobe, nao só listar.
        self.assertRegex(script, r"modprobe\s+-q\s+\"?\$alias_name")
        self.assertIn("modprobe", script)
        # Pular quem já tem driver é o que torna a varredura barata.
        self.assertIn('"$dev/driver"', script,
                      "sem pular dispositivo já ligado a varredura fica cara")
        self.assertIn("S05modules", read(POST_BUILD),
                      "post-build.sh não instala o S05modules")
        # A regra do modalias é o que transforma o evento em modprobe.
        self.assertRegex(mdev, r'(?m)^\$MODALIAS=\.\*\s+root:root\s+\S+\s+'
                               r'@modprobe "?\$MODALIAS"?\s*$',
                         "mdev.conf sem a regra $MODALIAS -> modprobe")

        # 'modprobe -b' (blacklist) exige CONFIG_FEATURE_MODPROBE_BLACKLIST, que
        # NAO esta ligado no BusyBox desta imagem: o modprobe recusaria a opcao
        # e nada seria carregado — o mesmo defeito, com um erro a mais.
        bb = read(BUSYBOX_FRAGMENT)
        if re.search(r"modprobe\s+-b\b", mdev):
            self.assertIn("CONFIG_FEATURE_MODPROBE_BLACKLIST=y", bb,
                          "mdev.conf usa 'modprobe -b' sem a feature no BusyBox")

        # Sem FEATURE_MDEV_CONF o mdev ignora o mdev.conf por completo.
        self.assertIn("CONFIG_FEATURE_MDEV_CONF=y", bb,
                      "sem FEATURE_MDEV_CONF o mdev.conf e' ignorado")
        self.assertIn("CONFIG_MDEV=y", bb, "sem MDEV nao ha coldplug")

    def test_phy_do_dsi_e_embutido(self):
        """PHY DSI como módulo deixava o DSI esperando (-517) para sempre.

        O painel não acende sem /dev/dri/card0, e o card0 só existe quando o
        agregado do DRM completa — o que depende do PHY do DSI estar pronto.
        """
        body = read(LINUX_POWKIDDY)

        self.assertIn("CONFIG_PHY_ROCKCHIP_INNO_DSIDPHY=y", body,
                      "PHY do DSI voltou a ser carregável (=m)")

    def test_s90graphics_le_soc_do_device_conf(self):
        """O `compatible` do RK3566 começa por 'powkiddy' (fabricante).

        Ler o primeiro campo dava "SoC 'powkiddy' nao reconhecido" e o Mesa
        ficava sem `MESA_LOADER_DRIVER_OVERRIDE`.
        """
        body = read(GRAPHICS)

        self.assertIn("GIOCHI_SOC", body)
        self.assertIn("DEVICE_CONF", body)
        self.assertNotRegex(
            body,
            r"SOC=\$\(tr -d '\\0' </proc/device-tree/compatible[^)]*\| head -n1\)",
            "voltou a pegar o primeiro campo do compatible como SoC",
        )

    def test_framebuffer_entregue_a_ui(self):
        """fbcon pinta por cima do SDL: cursor piscando + printk novo.

        Medido no aparelho: a tela acendeu mostrando o console (o kernel tem
        fbcon porque DRM_FBDEV_EMULATION=y faz "select FB", apesar do
        CONFIG_FB=n do fragmento) e o Launcher desenhava em headless. Com o
        SDL2/KMSDRM funcionando, qualquer printk — e o aparelho imprime
        "rk817-charger: Battery voltage ... below minimum" a cada 8 s — apaga o
        que a UI desenhou.
        """
        body = read(GRAPHICS)

        self.assertIn("devolver_framebuffer", body,
                      "S90graphics nao entrega o framebuffer para a UI")
        # A chamada tem de existir, nao so a definicao.
        self.assertGreaterEqual(body.count("devolver_framebuffer"), 2,
                                "devolver_framebuffer definida e nao chamada")
        self.assertIn("/sys/class/vtconsole", body,
                      "nao desvincula o fbcon do framebuffer")
        self.assertIn("/proc/sys/kernel/printk", body,
                      "nao baixa o console_loglevel")
        # Escape hatch para depurar com a tela.
        self.assertIn("GIOCHI_KEEP_FBCON", body,
                      "sem GIOCHI_KEEP_FBCON fica impossivel depurar na tela")

    def test_amplificador_de_som_e_embutido(self):
        """`spk_amp` como módulo deixa o card `sound` em deferred probe eterno.

        Medido no boot da X55:
        `platform sound: deferred probe pending: (reason unknown)` e
        `ALSA device list: No soundcards found.`

        O card declara `simple-audio-card,aux-devs = <&spk_amp>`; com o driver
        do amplificador em `=m` e nenhum módulo carregado, o aux-dev nunca
        existe e o card espera para sempre. O `(reason unknown)` é o sintoma: o
        card não devolveu -EPROBE_DEFER com motivo, ficou esperando um nó que
        ninguém nunca atendeu.
        """
        frag = read(LINUX_POWKIDDY)

        self.assertIn("CONFIG_SND_SOC_SIMPLE_AMPLIFIER=y", frag,
                      "o amplificador de som voltou a ser módulo (=m)")
        # A cadeia toda tem de ser embutida: um elo em =m segura o card.
        for simbolo in ("CONFIG_SND_SOC_ROCKCHIP=y",
                        "CONFIG_SND_SOC_RK817=y",
                        "CONFIG_SND_SIMPLE_CARD=y"):
            self.assertIn(simbolo, frag,
                          "%s tem de ser embutido" % simbolo)

    def test_var_e_gravavel(self):
        """/var na raiz SquashFS e' somente leitura; nada la pode ser criado.

        Medido no serial da X55:
        `seedrng: can't create directory '/var/lib/seedrng': Read-only file
        system` -> o pool de entropia nao persiste e o "crng init done" so
        aparecia aos 11 s de uptime. O `mkdir -p /var/log` do inittab tambem
        falhava em silencio.
        """
        inittab = read(INITTAB)

        self.assertRegex(inittab, r"mount -t tmpfs[^\n]* /var\b",
                         "/var nao tem tmpfs: continua somente leitura")
        self.assertIn("/var/lib/seedrng", inittab,
                      "sem /var/lib/seedrng o seedrng falha e a entropia nao persiste")

    def test_sonda_de_video_roda_no_boot(self):
        """O Launcher so diz "EGL not initialized" — nao diz QUAL passo falhou.

        A sonda (giochi-gpu-probe) faz os cinco passos (abrir /dev/dri, ver
        conector, criar GBM, eglInitialize, eglCreateContext) e o log do cartao
        passa a dizer onde parou.
        """
        body = read(GRAPHICS)
        mk = read(TOOLS_MK)

        self.assertIn("giochi-gpu-probe", mk,
                      "o pacote giochi-tools nao instala a sonda")
        self.assertIn("giochi-gpu-probe", body,
                      "S90graphics nao chama a sonda")
        # Roda DEPOIS do handover do framebuffer, senao mede outro estado.
        # A tentativa padrao (`sonda_video ""`) e' a que decide o driver que o
        # SDL vai encontrar, e ela escreve com >> para nao apagar o bloco da
        # tentativa anterior.
        self.assertLess(body.index("devolver_framebuffer\n"),
                        body.index('sonda_video ""'),
                        "a sonda tem de rodar depois de devolver_framebuffer")
        self.assertIn('giochi-gpu-probe >> "$PROBE_TXT"', body,
                      "a sonda tem de APPENDAR: rodar duas vezes e' o normal")
        self.assertIn("GIOCHI_NO_GPU_PROBE", body,
                      "sem escape hatch a sonda nao pode ser desligada")

    # --- ordem do rcS: o supervisor tem de ser o ULTIMO ------------------
    def _scripts_de_init(self):
        """Todo script S* que a imagem instala, com o caminho de origem."""
        itens = {}
        d = os.path.join(ROOT, "scripts/init.d")
        for nome in sorted(os.listdir(d)):
            if re.fullmatch(r"S\d\d.*", nome):
                itens[nome] = os.path.join(d, nome)
        # Pacotes do Buildroot instalam os deles (ex.: giochi-mtpd -> S60mtpd).
        base = os.path.join(ROOT, "buildroot/package")
        for pacote in sorted(os.listdir(base)):
            d = os.path.join(base, pacote)
            if not os.path.isdir(d):
                continue
            for nome in sorted(os.listdir(d)):
                if re.fullmatch(r"S\d\d.*", nome):
                    itens.setdefault(nome, os.path.join(d, nome))
        return itens

    def test_supervisor_e_o_ultimo_script(self):
        """`exec` no meio do rcS mata o resto do boot (AGENTS.md 3.86).

        Medido no boot real da X55: com o supervisor em S20, o rcs imprimiu
        `start /etc/init.d/S20graphics` e nada mais. S30swap, S50usb, S55usbnet,
        S60mtpd e S62sshd nunca rodaram — sem gadget USB, sem rede, sem MTP e
        sem SSH, e sem nenhuma mensagem de erro.
        """
        scripts = self._scripts_de_init()
        supervisores = []
        for nome, caminho in scripts.items():
            with open(caminho, encoding="utf-8") as fp:
                corpo = sh_code(fp.read())
            if re.search(r"^\s*exec\s+.*giochi-init", corpo, re.M):
                supervisores.append(nome)

        self.assertEqual(
            1, len(supervisores),
            "esperava exatamente um script de init com `exec giochi-init`; "
            "achei %r" % (supervisores,))
        ultimo = max(scripts)
        self.assertEqual(
            ultimo, supervisores[0],
            "o script que faz `exec` do supervisor tem de ser o ULTIMO do rcS "
            "(e' %r, o ultimo e' %r): com `exec` antes dele, todo script "
            "posterior nunca roda" % (supervisores[0], ultimo))

    def test_scripts_de_rede_e_usb_rodam_antes_do_supervisor(self):
        """O que o usuário percebe como "o USB nao funciona" mora aqui."""
        scripts = self._scripts_de_init()
        supervisor = None
        for nome, caminho in scripts.items():
            with open(caminho, encoding="utf-8") as fp:
                if re.search(r"^\s*exec\s+.*giochi-init",
                             sh_code(fp.read()), re.M):
                    supervisor = nome
        self.assertIsNotNone(supervisor)
        for obrigatorio in ("S50usb", "S55usbnet", "S62sshd", "S60mtpd"):
            with self.subTest(script=obrigatorio):
                self.assertIn(obrigatorio, scripts,
                              "%s nao e' mais instalado na imagem" % obrigatorio)
                self.assertLess(
                    obrigatorio, supervisor,
                    "%s roda DEPOIS do supervisor (%s): nunca e' alcancado"
                    % (obrigatorio, supervisor))

    def test_rcs_nao_faz_exec(self):
        """`exec` no proprio rcS tem o mesmo efeito, so' que em todos."""
        corpo = sh_code(read(RCS))
        self.assertNotRegex(
            corpo, r"(?m)^\s*exec\s",
            "o rcS nao pode fazer `exec`: ele substituiria o processo do laco")

    def test_laco_do_rcs_ordena_por_nome(self):
        """A ordem do boot e' a ordem alfabetica do glob — nada mais."""
        corpo = read(RCS)
        self.assertIn("/etc/init.d/S[0-9][0-9]*", corpo,
                      "o laco do rcS nao varre os scripts S* em ordem")
        self.assertIn("run_scripts", corpo)

    def test_listas_de_scripts_cobrem_o_supervisor(self):
        """Nome trocado e o script nao entra na imagem (falha silenciosa)."""
        post = read(POST_BUILD)
        m = re.search(r'ini_list="(.*?)"', post, re.S)
        self.assertIsNotNone(m, "post-build.sh nao declara ini_list")
        instalados = m.group(1).split()
        self.assertIn("S90graphics", instalados,
                      "post-build.sh nao instala o S90graphics")
        self.assertNotIn("S20graphics", instalados)
        self.assertNotIn("S99launcher", instalados)

        tracked = read(os.path.join(ROOT, "tools/check-tracked.sh"))
        self.assertIn("S90graphics", tracked)

    def test_post_build_remove_script_aposentado(self):
        """O TARGET_DIR sobrevive ao build e nao apaga o que saiu do repo.

        Medido na imagem de 2026-11-01, depois de mover S20graphics para
        S90graphics: o squashfs tinha S20graphics, S90graphics E S99launcher
        juntos. Como o rcS ordena pelo nome, o ANTIGO rodava primeiro e o
        `exec` matava o laco — a correcao do §3.86 existia no repositorio e
        nao existia na imagem, com `make` rc=0.

        Roda o post-build.sh REAL contra um TARGET_DIR temporario: um teste
        que so' casasse texto passaria mesmo com o `rm` removido.
        """
        import subprocess
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            target = os.path.join(tmp, "target")
            initd = os.path.join(target, "etc/init.d")
            os.makedirs(initd)
            for antigo in ("S20graphics", "S90graphics", "S99launcher"):
                with open(os.path.join(initd, antigo), "w",
                          encoding="utf-8") as fp:
                    fp.write("#!/bin/sh\n")

            env = dict(os.environ, GIOCHI_DEVICE="x55")
            proc = subprocess.run(
                ["sh", POST_BUILD, target], env=env,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            self.assertEqual(0, proc.returncode,
                             "post-build.sh falhou:\n%s"
                             % proc.stdout.decode("utf-8", "replace"))

            sobrou = sorted(os.listdir(initd))
            self.assertIn("S90graphics", sobrou,
                          "o supervisor nao foi instalado: %r" % (sobrou,))
            self.assertNotIn(
                "S20graphics", sobrou,
                "S20graphics continua na arvore: o rcS roda o script ANTIGO "
                "primeiro e o `exec` mata o resto do boot")
            self.assertNotIn("S99launcher", sobrou,
                             "S99launcher (codigo morto) continua na arvore")

            # Manifesto fora da imagem e idempotente: rodar de novo nao muda.
            before = sorted(os.listdir(initd))
            subprocess.run(["sh", POST_BUILD, target], env=env,
                           stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
            self.assertEqual(before, sorted(os.listdir(initd)),
                             "post-build.sh nao e' idempotente")

    def test_build_nao_anuncia_imagem_quando_make_falha(self):
        """'if ! build_device ...' desliga o 'set -e' dentro da função.

        Medido: o kernel falhou no passo de patch (patch duplicado), o script
        seguiu, achou o `sdcard.img` VELHO em images/ e imprimiu
        "imagens geradas" com rc=0. Ou seja: falha de build anunciada como
        sucesso, com uma imagem de antes entregue ao aparelho.
        """
        body = read(BUILD_IMAGE)

        # Toda invocacao de make precisa de tratamento de erro explicito.
        linhas = body.splitlines()
        janelas = []
        for i, linha in enumerate(linhas):
            if 'make -C "$buildroot"' not in linha:
                continue
            janela = [linha]
            j = i
            while j < len(linhas) and \
                    linhas[j].rstrip().endswith("\\"):
                j += 1
                janela.append(linhas[j])
            if j + 1 < len(linhas):
                janela.append(linhas[j + 1])
            janelas.append("\n".join(janela))

        self.assertGreaterEqual(len(janelas), 2,
                                "esperava 'make defconfig' e 'make' no script")
        for trecho in janelas:
            with self.subTest(make=trecho.splitlines()[0][:60]):
                self.assertIn("||", trecho,
                              "make sem tratamento de erro: falha vira sucesso")

    def test_s50usb_sonda_o_controlador_antes_do_gadget(self):
        """"O USB nao apareceu" tem quatro causas; o log nao dizia qual.

        Medido no serial da X55: `HOST MAC` (funcao ECM criada) seguido de
        `failed to enable ep0out` (SETEPCONFIG recusado no bind). Eu li a
        primeira linha como "o UDC existe e vai funcionar" e a segunda como
        "nao existe UDC" — as duas leituras erradas, e custou uma sessao.
        A sonda grava os tres fatos que separam os casos.
        """
        corpo = sh_code(read(S50USB))

        self.assertIn("/sys/class/udc", corpo,
                      "o S50usb nao registra se o controlador tem modo "
                      "dispositivo (UDC vazio = problema de DT/PHY)")
        self.assertIn("dr_mode", corpo,
                      "o S50usb nao registra o dr_mode efetivo: 'otg' e "
                      "'peripheral' dao o mesmo log quando funciona")
        self.assertIn("usb_gadget/giochi/UDC", corpo,
                      "o S50usb nao confere o arquivo UDC do configfs: o "
                      "helper sai 0 mesmo quando nao anexa")

        # A sonda tem de rodar ANTES do helper: se ele reconfigura o configfs,
        # ler depois mede outro estado (e o UDC pode ter sido limpo no
        # comeco do helper, que faz `echo "" > UDC`).
        indice_sonda = corpo.index("sonda_udc")
        indice_helper = corpo.index('"$HELPER" "$mode"')
        self.assertLess(indice_sonda, indice_helper,
                        "a sonda tem de medir o estado ANTES de o helper mexer "
                        "no configfs")

    def test_s50usb_funciona_sem_udc_e_nao_aborta_o_boot(self):
        """Sonda com /sys vazio nao pode derrubar o rcS.

        Roda o script REAL apontando SETTINGS e HELPER para arquivos de teste.
        O `logger` do host engoliria a saida, entao um `logger` falso no PATH
        a manda para stderr — e assim o teste ve o que o aparelho veria no log.
        """
        import subprocess
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            binfalso = os.path.join(tmp, "bin")
            os.makedirs(binfalso)
            with open(os.path.join(binfalso, "logger"), "w",
                      encoding="utf-8") as fp:
                # Escreve em STDOUT, nao em stderr: o `log()` do script manda
                # o stderr do logger para /dev/null, entao um logger falso que
                # escreve em stderr fica invisivel (medido: a primeira versao
                # deste teste "nao via" a sonda que o script estava printando).
                fp.write("#!/bin/sh\nshift 2\necho \"$*\"\n")
            os.chmod(os.path.join(binfalso, "logger"), 0o755)

            settings = os.path.join(tmp, "settings.json")
            with open(settings, "w", encoding="utf-8") as fp:
                fp.write('{"system":{"usb_mode":"mtp"}}')

            helper = os.path.join(tmp, "helper")
            with open(helper, "w", encoding="utf-8") as fp:
                fp.write("#!/bin/sh\nexit 0\n")
            os.chmod(helper, 0o755)

            env = dict(os.environ,
                       PATH=binfalso + os.pathsep + os.environ.get("PATH", ""),
                       SETTINGS=settings, HELPER=helper)
            proc = subprocess.run(["sh", S50USB], env=env,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, timeout=60)
            self.assertEqual(0, proc.returncode,
                             "S50usb falhou: %s"
                             % proc.stderr.decode("utf-8", "replace"))
            saida = (proc.stdout + proc.stderr).decode("utf-8", "replace")
            self.assertIn("sonda", saida,
                          "a sonda nao escreveu nada no log: %r" % saida)
            # Sem UDC no host, ou com o do host, a sonda tem de dizer qual dos
            # dois casos e' — nunca ficar calada.
            self.assertTrue(
                "VAZIO" in saida or "UDC(s)" in saida,
                "a sonda nao classificou o estado do UDC: %r" % saida)
            self.assertIn("NAO anexado", saida,
                          "sem configfs o gadget nao anexa e o log tem de "
                          "dizer isso, nao apenas 'configurado': %r" % saida)

    def _logger_falso(self, tmp):
        """`logger` que imprime no stdout: o log() manda o stderr do logger
        para /dev/null, então um logger falso que escreve em stderr fica
        invisível para o teste."""
        binfalso = os.path.join(tmp, "bin")
        os.makedirs(binfalso, exist_ok=True)
        caminho = os.path.join(binfalso, "logger")
        with open(caminho, "w", encoding="utf-8") as fp:
            fp.write('#!/bin/sh\nshift 2\necho "$*"\n')
        os.chmod(caminho, 0o755)
        # `giochi_wait_for` dorme 100 ms por tentativa (30 = 3 s). Sem usleep
        # no host ela cai para `sleep 1` e o teste custaria 30 s; aqui o
        # no-op mantem a MESMA contagem de tentativas sem esperar.
        no_op = os.path.join(binfalso, "usleep")
        with open(no_op, "w", encoding="utf-8") as fp:
            fp.write("#!/bin/sh\nexit 0\n")
        os.chmod(no_op, 0o755)
        return binfalso

    def test_s50usb_espelha_o_settings_antes_de_soltar_o_user(self):
        """O modo `mass` desmonta /user; quem roda depois não pode ficar cego.

        Medido na X55 (imagem de 01/11), no serial:

            [1.794] EXT4-fs (mmcblk1p4): mounted filesystem ... r/w
            [5.737] EXT4-fs (mmcblk1p4): unmounting filesystem ...
            [5.753] configfs-gadget.giochi gadget.0: HOST MAC ...

        O S55usbnet roda depois do S50usb e decide o Modo Desenvolvedor
        lendo /user/settings.json — que nesse instante não existe mais. Ele
        concluía "desligado", deixava o usb0 DOWN, e o PC ficava em
        NO-CARRIER sem 10.55.0.2. O espelho em /run (tmpfs) corta a
        dependência de ordem. Roda o script REAL.
        """
        import subprocess
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            binfalso = self._logger_falso(tmp)
            run = os.path.join(tmp, "run")
            os.makedirs(run)
            settings = os.path.join(tmp, "settings.json")
            with open(settings, "w", encoding="utf-8") as fp:
                fp.write('{"system":{"usb_mode":"mass"},'
                         '"developer":{"ssh_usb_enabled":true}}')
            helper = os.path.join(tmp, "helper")
            with open(helper, "w", encoding="utf-8") as fp:
                fp.write("#!/bin/sh\nexit 0\n")
            os.chmod(helper, 0o755)

            env = dict(os.environ,
                       PATH=binfalso + os.pathsep + os.environ.get("PATH", ""),
                       SETTINGS=settings, HELPER=helper, GIOCHI_RUN_DIR=run,
                       GIOCHI_LIB=LIB_SH)
            proc = subprocess.run(["sh", S50USB], env=env,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, timeout=60)
            self.assertEqual(0, proc.returncode,
                             proc.stderr.decode("utf-8", "replace"))
            espelho = os.path.join(run, "settings.json")
            self.assertTrue(os.path.isfile(espelho),
                            "S50usb não espelhou o settings em /run: o "
                            "S55usbnet vai ler um arquivo que o modo mass "
                            "acabou de desmontar")
            with open(espelho, encoding="utf-8") as fp:
                self.assertIn("ssh_usb_enabled", fp.read(),
                              "o espelho não tem o conteúdo do original")
            saida = (proc.stdout + proc.stderr).decode("utf-8", "replace")
            self.assertIn("modo USB: mass", saida,
                          "o log não diz qual modo foi escolhido: %r" % saida)
            self.assertIn("origem: " + settings, saida,
                          "o log não diz DE ONDE veio o modo (usuário x "
                          "padrão): %r" % saida)

    def test_s55usbnet_le_o_espelho_quando_o_user_sumiu(self):
        """Prova que o espelho é LIDO, não só gravado.

        Com /user fora do ar, só o espelho existe. Se o S55usbnet ignorasse o
        espelho ele sairia calado (gate "desligado"); lendo o espelho ele
        passa o gate e segue até procurar a interface — que aqui não existe,
        então a mensagem "nao apareceu" é justamente a prova de que passou.
        """
        import subprocess
        import tempfile

        def rodar(mirror_com_ssh):
            with tempfile.TemporaryDirectory() as tmp:
                binfalso = self._logger_falso(tmp)
                run = os.path.join(tmp, "run")
                os.makedirs(run)
                with open(os.path.join(run, "settings.json"), "w",
                          encoding="utf-8") as fp:
                    fp.write('{"developer":{"ssh_usb_enabled":%s}}'
                             % ("true" if mirror_com_ssh else "false"))
                env = dict(os.environ,
                           PATH=binfalso + os.pathsep + os.environ.get("PATH", ""),
                           # SETTINGS aponta para um arquivo que NÃO existe:
                           # é o estado real depois do `umount /user`.
                           SETTINGS=os.path.join(tmp, "settings-que-sumiu.json"),
                           GIOCHI_RUN_DIR=run, GIOCHI_LIB=LIB_SH,
                           USB_NET_IFACE="giochi-iface-inexistente")
                proc = subprocess.run(["sh", S55USBNET], env=env,
                                      stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE, timeout=60)
                return (proc.stdout + proc.stderr).decode("utf-8", "replace")

        com = rodar(True)
        self.assertIn("nao apareceu", com,
                      "com ssh_usb_enabled=true no espelho o S55usbnet tinha "
                      "de seguir e procurar a interface; saída: %r" % com)
        sem = rodar(False)
        self.assertNotIn("nao apareceu", sem,
                         "com o espelho dizendo false o gate tem de barrar: "
                         "%r" % sem)

    def test_giochi_user_part_devolve_no_de_bloco_nunca_partuuid(self):
        """`f_mass_storage` não abre "PARTUUID=..."; o PC vê 0 setores.

        Medido no serial da X55 (imagem de 01/11):

            mass_storage.0/lun.0: unable to open backing file: PARTUUID=47494f43-04

        e no PC: `sde 0B`, a função anunciada e nenhuma linha de erro. A
        montagem tinha sido desmontada para isso — ou seja, o modo mass
        custava /user e não entregava nada. A correção é devolver /dev/<nó>.
        """
        import subprocess
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            # /proc/cmdline e /proc/mounts falsos não são possíveis (caminho
            # absoluto); então exercitamos a função num shell que resolve a
            # raiz por um cmdline simulado — a função lê /proc direto, e o
            # que este teste pode provar sem root é o CONTRATO: o valor
            # devolvido nunca tem o formato de mount.
            script = (
                '. "$1" 2>/dev/null\n'
                'v=$(giochi_user_part 2>/dev/null)\n'
                'printf "%s" "$v"\n'
            )
            proc = subprocess.run(["sh", "-c", script, "sh", LIB_SH],
                                  env=dict(os.environ),
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, timeout=30)
            valor = proc.stdout.decode("utf-8", "replace").strip()
            self.assertNotIn("PARTUUID", valor,
                             "giochi_user_part devolveu um nome que o "
                             "f_mass_storage nao abre (%r): e' o bug do "
                             "disco de 0 setores" % valor)
            self.assertNotIn("LABEL", valor,
                             "giochi_user_part devolveu um label de "
                             "mount (%r); o gadget precisa de /dev" % valor)

        # A guarda de verdade está no código: o laço final só aceita `-b`.
        with open(LIB_SH, encoding="utf-8") as fp:
            fonte = fp.read()
        trecho = fonte[fonte.index("giochi_user_part()"):]
        trecho = trecho[:trecho.index("\n}\n")]
        self.assertNotIn("PARTUUID", trecho.split("#")[0],
                         "o corpo de giochi_user_part ainda monta PARTUUID")
        self.assertIn("[ -b ", trecho,
                      "giochi_user_part precisa conferir que o caminho e' um "
                      "no' de bloco antes de devolver")

    def test_usb_mode_recusa_alvo_que_nao_e_no_de_bloco(self):
        """O helper tem de dizer POR QUE não anexou, não anunciar 0 setores."""
        import subprocess
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            binfalso = self._logger_falso(tmp)
            # `giochi_user_part` vem do lib.sh real; aqui o que interessa é o
            # caminho de erro do helper quando o alvo não é dispositivo.
            lib = os.path.join(tmp, "lib.sh")
            with open(lib, "w", encoding="utf-8") as fp:
                fp.write('giochi_user_part() { echo "' + tmp + '/nao-e-bloco"; }\n')
            env = dict(os.environ,
                       PATH=binfalso + os.pathsep + os.environ.get("PATH", ""),
                       GIOCHI_LIB=lib, GIOCHI_DRY_RUN="1",
                       GADGET=os.path.join(tmp, "gadget"))
            proc = subprocess.run(["sh", USB_MODE, "mass"], env=env,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, timeout=60)
            saida = (proc.stdout + proc.stderr).decode("utf-8", "replace")
            self.assertIn("particao /user nao resolvida", saida,
                          "o helper tentou anexar um alvo que nao e' no' de "
                          "bloco sem avisar: %r" % saida)




GROW = os.path.join(ROOT, "buildroot/board/giochi/rootfs_overlay/etc/giochi/grow-user.sh")


class ArmazenamentoDoUsuario(unittest.TestCase):
    """A particao de dados tem de ocupar o cartao; e os nomes, ser legiveis.

    Tres defeitos medidos na X55:

    1. `/user` nascia com 512 MiB e o cartao tinha 29,7 GiB — 28,8 GiB
       inuteis (`/sys/class/block/mmcblk1p4/size` = 1048576 setores contra
       62333952 do disco).
    2. `giochi_disk_of` com `root=PARTUUID=...` devolvia lixo, entao
       `giochi_other_disk` nao reconhecia o cartao de boot e podia devolver
       o PROPRIO cartao de boot como "slot 2".
    3. O rotulo da particao aparecia como `GIOCHI_USER` no PC e nos textos
       do Launcher — jargao para quem so' quer copiar um jogo.
    """

    def fonte(self, caminho):
        with open(caminho, encoding="utf-8") as fh:
            return fh.read()

    def test_grow_roda_antes_do_mount_a(self):
        """Com /user montada o kernel nao rele a tabela (BLKRRPART falha)."""
        # Só as DIRETIVAS: o comentário acima delas também cita "mount -a" e
        # `index()` acharia a menção no comentário, não a linha executada.
        diretas = [l for l in self.fonte(INITTAB).splitlines()
                   if l.startswith("::")]
        corpo = "\n".join(diretas)
        self.assertIn("grow-user.sh", corpo,
                      "o inittab precisa chamar o grow antes do mount -a")
        self.assertLess(corpo.index("grow-user.sh"), corpo.index("mount -a"),
                        "chamar depois do mount -a nao funciona: a particao "
                        "ja' esta' em uso e o kernel nao rele a tabela")

    def test_grow_nunca_encolhe_nem_move(self):
        corpo = self.fonte(GROW)
        self.assertNotIn("sfdisk --delete", corpo)
        # O "+" e' "mesmo inicio, tamanho maximo"; sem ele o sfdisk poderia
        # mover a particao.
        # O START vai explicito: se alguem remover o `-N` da chamada, `,`
        # passa a significar "primeiro setor livre" e a p4 encostaria na p3.
        self.assertIn('printf \'%s,+\\n\' "$p4_inicio"', corpo,
                      "o sfdisk tem de receber '<inicio>,+' (mesmo inicio, "
                      "cresce o fim)")

    def test_grow_confere_que_a_p4_e_a_ultima(self):
        """Estender uma particao do meio sobrescreveria a seguinte."""
        corpo = self.fonte(GROW)
        self.assertIn("ha' particao depois de", corpo,
                      "falta a guarda que recusa crescer se houver particao "
                      "depois da p4")
        self.assertIn("e2fsck", corpo,
                      "resize2fs precisa do fs limpo; o cartao acabou de ser "
                      "gravado em bloco")

    def test_grow_nao_derruba_o_boot(self):
        """Falhar ao crescer e' incomodo; quebrar /user e' perda de dados."""
        corpo = self.fonte(GROW)
        self.assertIn("exit 0", corpo)
        # Todos os caminhos de erro terminam em exit 0 (sem derrubar o rcS).
        import re

        for m in re.finditer(r"log \"ERRO[^\n]*\n\s*(exit \d)", corpo):
            self.assertEqual("exit 0", m.group(1),
                             "erro do grow nao pode falhar o boot")

    def test_disk_of_aceita_dois_digitos(self):
        """`/dev/sda10` virava `/dev/sda1` (o caso de 1 digito vinha antes)."""
        import subprocess

        casos = {"/dev/mmcblk1p2": "/dev/mmcblk1",
                 "/dev/mmcblk1p10": "/dev/mmcblk1",
                 "/dev/sda2": "/dev/sda",
                 "/dev/sda10": "/dev/sda"}
        script = '. "$1"; for p in "$@"; do :; done; ' \
                 'for p in /dev/mmcblk1p2 /dev/mmcblk1p10 /dev/sda2 /dev/sda10; ' \
                 'do giochi_disk_of "$p"; echo; done'
        r = subprocess.run(["sh", "-c", script, "sh", LIB_SH],
                           capture_output=True, text=True, timeout=30)
        obtidos = [l for l in r.stdout.splitlines() if l.strip()]
        self.assertEqual(list(casos.values()), obtidos,
                         "giochi_disk_of errou o corte de particao")

    def test_other_disk_nao_devolve_o_cartao_de_boot(self):
        """Sem saber qual e' o disco de boot, devolver 1 e' o certo.

        A versao anterior comparava contra `giochi_disk_of(root)` com
        `root=PARTUUID=...`, que devolve lixo — a comparacao nunca casava e o
        cartao de boot passava como "outro disco".
        """
        corpo = self.fonte(LIB_SH)
        self.assertIn("giochi_boot_disk", corpo,
                      "falta resolver o disco de boot antes de comparar")
        self.assertIn("blkid -t", corpo,
                      "com root=PARTUUID e' o blkid que resolve o dispositivo")
        # E tem de desistir quando nao sabe.
        trecho = corpo[corpo.index("giochi_other_disk()"):]
        trecho = trecho[:trecho.index("\n}\n")]
        self.assertIn("return 1", trecho)

    def test_other_disk_ignora_emmc_e_leitor_vazio(self):
        corpo = self.fonte(LIB_SH)
        trecho = corpo[corpo.index("giochi_other_disk()"):]
        trecho = trecho[:trecho.index("\n}\n")]
        self.assertIn("mmcblk0", trecho, "eMMC nao e' slot 2")
        self.assertIn("removable", trecho,
                      "so' leitor de cartao serve; SDIO/eMMC ficam de fora")
        self.assertIn("_tem_part", trecho,
                      "leitor vazio (size=0) nao tem particao e nao serve")


    def test_defconfig_pede_as_ferramentas_do_grow(self):
        """Sem resize2fs/sfdisk/blkid o script nao tem como crescer nada.

        `BR2_PACKAGE_E2FSPROGS` NAO instala o `resize2fs` no Buildroot
        2024.02 (ha' um simbolo proprio), e o `sfdisk`/`blkid` de verdade
        vem de `UTIL_LINUX_BINARIES` — o `blkid` do BusyBox nao resolve
        PARTUUID.
        """
        for base in ("base-rk3566.defconfig", "base-rk3326.defconfig"):
            caminho = os.path.join(ROOT, "buildroot/board/giochi/defconfig",
                                   base)
            with open(caminho, encoding="utf-8") as fh:
                corpo = fh.read()
            with self.subTest(defconfig=base):
                for simbolo in ("BR2_PACKAGE_E2FSPROGS_RESIZE2FS=y",
                                "BR2_PACKAGE_UTIL_LINUX_BINARIES=y"):
                    self.assertIn(simbolo, corpo,
                                  f"sem {simbolo} o grow-user.sh nao roda")

    def test_rotulo_da_particao_e_giochi(self):
        """O usuario ve esse nome no PC; `GIOCHI_USER` e' jargao."""
        fstab = self.fonte(os.path.join(
            ROOT, "buildroot/board/giochi/rootfs_overlay/etc/fstab"))
        self.assertIn("LABEL=GIOCHI\t/user", fstab)
        post = self.fonte(os.path.join(
            ROOT, "buildroot/board/giochi/post-image.sh"))
        self.assertIn('USER_LABEL="GIOCHI"', post,
                      "o rotulo gravado pelo mkfs tem de casar com o fstab")
        self.assertIn('mkfs.ext4 -q -F -L "$USER_LABEL"', post,
                      "o mkfs tem de usar a variavel, nao um literal solto")
        # O .ext4 vive no BINARIES_DIR, que sobrevive ao build incremental:
        # sem esta comparacao o cartao sai com o rotulo antigo (aconteceu).
        self.assertIn("e2label", post,
                      "mudar o rotulo nao recria o user.ext4: o post-image "
                      "precisa comparar o rotulo do arquivo existente")

    def test_textos_do_launcher_sem_jargao(self):
        """O Launcher nao deve mostrar caminho de arquivo nem nome de particao."""
        import json
        import re

        proibidos = ("GIOCHI_USER", "/user/")
        for lang in ("pt_BR", "en"):
            caminho = os.path.join(ROOT, "assets/locales", lang + ".json")
            with open(caminho, encoding="utf-8") as fh:
                d = json.load(fh)
            for chave, valor in d.items():
                if not isinstance(valor, str):
                    continue
                for termo in proibidos:
                    self.assertNotIn(
                        termo, valor,
                        f"{lang}.json:{chave} mostra '{termo}' ao usuario: "
                        f"{valor!r}")


class MtpdChamaOUmtprdComAFlagCerta(unittest.TestCase):
    """`umtprd` usa `-conf`, não `-c` (AGENTS.md §3.99).

    O helper chamava `"$BIN" -c "$CONF"`. O uMTP-Responder 1.8.1 tem, em
    `src/umtprd.c`:

        #define PARAMETER_CONF "-conf"
        ...
        else { PRINT_ERROR("Unknown command line option: %s", argv[1]); exit(1); }

    Ou seja: saía com 1 antes de qualquer tentativa de abrir o FunctionFS. E
    como a saída ia toda para `/run/giochi/umtprd.log`, o boot registrava só

        giochi-usb: aviso: umtprd nao iniciou; MTP nao respondera

    sem a causa. O bind no UDC então falhava com -19 (FunctionFS sem
    descritores) e o kernel dizia "couldn't find an available UDC or it's
    busy", que é a mensagem errada para o problema errado.
    """

    def test_helper_usa_a_flag_conf(self):
        import re

        with open(MT_PD, encoding="utf-8") as fh:
            fonte = fh.read()
        # `-c ` seguido de variavel: a flag curta que nao existe.
        self.assertIsNone(
            re.search(r'"\$BIN"\s+-c\s+', fonte),
            "giochi-mtpd ainda chama `umtprd -c`: a opcao e' `-conf`, e o "
            "binario sai com 1 sem nem tentar abrir o FunctionFS.")
        self.assertIn('"$BIN" -conf "$CONF"', fonte,
                      "o helper tem de passar a configuracao com `-conf`")

    def test_falha_do_umtprd_mostra_o_log(self):
        """Sem isto, "MTP nao sobe" chega ao serial sem nenhuma pista."""
        with open(MT_PD, encoding="utf-8") as fh:
            fonte = fh.read()
        self.assertIn('tail -n 3 "$LOG"', fonte,
                      "quando o umtprd morre ao iniciar, as ultimas linhas de "
                      "$LOG sao a unica pista; elas tem de ir para o log de "
                      "boot")

    def test_mtpd_loga_no_stderr(self):
        """O cartao pode estar no PC (modo mass); o serial nao."""
        with open(MT_PD, encoding="utf-8") as fh:
            fonte = fh.read()
        self.assertIn('echo "giochi-mtpd: $*" >&2', fonte,
                      "log() do giochi-mtpd tem de escrever no stderr tambem: "
                      "e' o que o rcS duplica para o serial")



class MeuCuidadoComOEp0DoFunctionfs(unittest.TestCase):
    """`ep0` do FunctionFS é ARQUIVO REGULAR, não char device (§3.100).

    `drivers/usb/gadget/function/f_fs.c`:

        ctx->perms.mode = S_IFREG | 0600;

    Quem cria `/dev/<nome>` como char device é o **gadgetfs**, outro
    subsistema. O `giochi-mtpd` exigia `[ -c "$FFS_DIR/ep0" ]`, condição que
    nunca é verdadeira: o mount estava certo e o guarda recusava, dizendo
    "FunctionFS nao montado". Medido na X55 — nenhuma linha de erro de mount
    e a mensagem em todo boot, mandando investigar o lugar errado.
    """

    def test_nao_exige_char_device(self):
        with open(MT_PD, encoding="utf-8") as fh:
            fonte = fh.read()
        self.assertNotIn('[ -c "$FFS_DIR/ep0" ]', fonte,
                         "ep0 do functionfs e' S_IFREG; `[ -c ]` reprova "
                         "sempre. Use `[ -e ]`.")
        self.assertIn('[ -e "$FFS_DIR/ep0" ]', fonte,
                      "a checagem do ep0 tem de aceitar arquivo regular")

    def test_helper_de_montagem_tambem_usa_e(self):
        with open(USB_MODE, encoding="utf-8") as fh:
            fonte = fh.read()
        self.assertNotIn('[ ! -e "$FFS_DIR/ep0" ]',
                         fonte.replace('[ ! -e "$FFS_DIR/ep0" ]', ''))
        self.assertIn('"$FFS_DIR/ep0"', fonte,
                      "o helper tem de conferir o ep0 depois do mount")

    def test_sonda_nao_inventa_caminho_no_sysfs(self):
        """Nem `/sys/module/usb_f_fs` nem `/sys/bus/usb-function` existem.

        Duas tentativas erradas de responder "a funcao `f_fs` esta' no
        kernel?", ambas com falso negativo na X55:

        * `/sys/module/usb_f_fs` — so' aparece para `=y` **com** parametros;
          o `f_fs` esta' em vmlinux (`nm vmlinux | grep ffs_alloc` acha);
        * `/sys/bus/usb-function/drivers/ffs` — **nao existe**:
          `usb_function_register()` (drivers/usb/gadget/functions.c) so'
          insere numa lista em memoria, sem barramento sysfs.

        A sonda confiavel e' USAR: `mkdir functions/ffs.<nome>` no configfs,
        que e' o que o helper faz. O teste cobra que o script nao volte a
        perguntar ao sysfs por um caminho inventado.
        """
        with open(S50USB, encoding="utf-8") as fh:
            fonte = fh.read()
        # Só as linhas de código: o comentário cita os caminhos para explicar
        # por que não servem.
        codigo = [l for l in fonte.splitlines()
                  if l.strip() and not l.lstrip().startswith("#")]
        self.assertFalse(
            any("/sys/bus/usb-function" in l for l in codigo),
            "esse barramento nao existe; a sonda tem de usar o configfs")
        self.assertFalse(
            any("/sys/module/usb_f_fs" in l for l in codigo),
            "codigo =y sem parametros nao cria entrada em /sys/module")


if __name__ == "__main__":
    unittest.main()
