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
MDEV_CONF = os.path.join(ROOT, "buildroot/board/giochi/mdev.conf")
BUSYBOX_FRAGMENT = os.path.join(ROOT, "buildroot/board/giochi/busybox.fragment")
LINUX_POWKIDDY = os.path.join(ROOT, "buildroot/board/giochi/linux-powkiddy.fragment")
S20GRAPHICS = os.path.join(ROOT, "scripts/init.d/S20graphics")
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

        Medido na X55: o S20graphics exportava
        `MESA_LOADER_DRIVER_OVERRIDE=panfrost`. Nesta placa o dispositivo DRM e'
        o *display* (rockchip-drm); quem fala com ele e' o `rockchip_dri.so`
        (kmsro), que delega a RENDERIZACAO ao panfrost. Forcando `panfrost`, o
        Mesa carrega o driver de render direto contra o no' do display, o EGL
        falha e o SDL so' diz "EGL not initialized" — tela apagada com o DRM
        bindado.
        """
        codigo = sh_code(read(S20GRAPHICS))

        self.assertNotIn(
            "MESA_LOADER_DRIVER_OVERRIDE=panfrost", codigo,
            "S20graphics volta a forcar o driver de RENDER: o kmsro "
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
        codigo = sh_code(read(S20GRAPHICS))

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
        codigo = sh_code(read(S20GRAPHICS))

        self.assertIn("kms_swrast", codigo,
                      "S20graphics perdeu o fallback de software")

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

        # O S20graphics liga o splash por 10 s (tempo de olhar e ver) e deixa
        # desligar por marcador, sem recompilar — mesmo padrao do `nolog`.
        script = sh_code(read(S20GRAPHICS))
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

        script = read(S20GRAPHICS)
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

        O giochi-init pode ser iniciado pelo rcS, pelo S99launcher ou pelo
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

    def test_s20graphics_le_soc_do_device_conf(self):
        """O `compatible` do RK3566 começa por 'powkiddy' (fabricante).

        Ler o primeiro campo dava "SoC 'powkiddy' nao reconhecido" e o Mesa
        ficava sem `MESA_LOADER_DRIVER_OVERRIDE`.
        """
        body = read(S20GRAPHICS)

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
        body = read(S20GRAPHICS)

        self.assertIn("devolver_framebuffer", body,
                      "S20graphics nao entrega o framebuffer para a UI")
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
        body = read(S20GRAPHICS)
        mk = read(TOOLS_MK)

        self.assertIn("giochi-gpu-probe", mk,
                      "o pacote giochi-tools nao instala a sonda")
        self.assertIn("giochi-gpu-probe", body,
                      "S20graphics nao chama a sonda")
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


if __name__ == "__main__":
    unittest.main()
