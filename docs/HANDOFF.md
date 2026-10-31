# Handoff — Giochi Konsol OS v1.2

Estado de implementação por sessão do [Prompt.md](../Prompt.md). A
especificação normativa é [architecture-v1.2.md](./architecture-v1.2.md).

Para gerar imagens por aparelho (incluindo X35S/X35H com DTS externo ou
fallback de teste), veja [DEVICES.md](./DEVICES.md) e `tools/build-image.sh`.

Cada bloco `S<N>` lista o que existe hoje, o que ficou como `TODO(v1.2)` e as
decisões tomadas (`# DECISÃO`) que afetam a sessão seguinte.

Legenda: **feito** (compila e/ou roda), **esqueleto** (interface + stub com
TODO), **doc** (apenas especificação/script).

## 2026-10-31 — o patch do DSI entrou, mas nenhum módulo do kernel carregava

Boot com o patch do §3.48 (cartão verificado byte a byte). A tela continuou
apagada por um bloqueio **anterior ao DSI**:

```
[   10.998616] dw-mipi-dsi-rockchip fe060000.dsi: failed to get mipi dphy: -517
[   11.009544] platform fe060000.dsi: deferred probe pending: (reason unknown)
[    0.985407] ALSA device list: No soundcards found.
```

`-517` é `-EPROBE_DEFER`. `/lib/modules/6.12.0/` estava cheio de `.ko` e
**nenhum** havia sido carregado: o sistema não tem udev, o `request_module()`
do kernel só funciona depois do rootfs, e o `mdev.conf` do projeto não tinha
regra `$MODALIAS`. Sem PHY do DSI o agregado do DRM nunca completa.

Três defeitos corrigidos (detalhe em AGENTS.md §3.50–§3.52):

| Defeito | Correção |
|---|---|
| nenhum módulo carregado no boot | `scripts/init.d/S05modules` faz `mdev -s`; regra `$MODALIAS` no `mdev.conf` |
| PHY do DSI era `=m` e nunca subia | `CONFIG_PHY_ROCKCHIP_INNO_DSIDPHY=y` |
| pacote local com binário VELHO na imagem | `build-image.sh` apaga `build/giochi-*/.stamp_*` |
| `make` falho anunciado como sucesso | cada `make` agora tem `\|\| { ...; return 1; }` |

Também: o Wi-Fi ganhou `rtw88/rtw8821c_fw.bin` + `regulatory.db` (o probe do
`rtw_8821cs` falhava com `-22`) e o `S20graphics` deixou de ler `powkiddy` como
SoC.

**Imagem regravada e verificada** (`sha256` `3530bb1b…`, kernel `#4`,
`Image` `e60baa04…`). **Não bootada ainda.**

## 2026-10-31 — vídeo da X55: a causa do `-22` era o bind feito dentro do probe

O log do cartão (`/boot/giochi-boot.log`, 1 141 linhas) mostrou duas coisas
que o serial resumido não mostrava:

1. **A pilha do bind**, que é o que faltava para fechar o §3.44:
   `rockchip_drm_bind ← try_to_bring_up_aggregate_device ← component_add ←
   dw_mipi_dsi_rockchip_host_attach ← mipi_dsi_attach ← hx8394_probe ←
   mipi_dsi_host_register ← dw_mipi_dsi_probe`. Ou seja, o painel é sondado
   **de forma síncrona** dentro do probe do DSI, e o `component_add()` fez o
   master do DRM bindar **antes de o probe terminar** — com `dsi->dmd` ainda
   NULL. Como `bridge` é o primeiro campo de `struct dw_mipi_dsi`,
   `&NULL->bridge == NULL`, e `drm_bridge_attach()` devolve `-EINVAL` **sem
   imprimir nada** (é o único `-EINVAL` silencioso da função). A ausência da
   linha `failed to attach bridge ... to encoder ...` era a pista.
2. **O cartão que estava no aparelho rodava a imagem de debug de 18:26**, não
   a de 19:14: o log tem `(device=rk3326 soc=rk3326)`, `secao [efault]` e
   `'/user/music' inacessivel`. Ou seja, as quatro correções do commit
   `39d17b1` **ainda não tinham sido testadas em hardware**.

**Correção (patch de kernel):**
`buildroot/board/giochi/patches/linux/0001-drm-rockchip-dw-mipi-dsi-adia-component-add.patch`
+ `BR2_LINUX_KERNEL_PATCH` nos dois templates. Se `dsi->dmd` ainda for NULL, o
`host_attach` guarda a intenção (`component_pending`) e o `component_add` é
feito no **fim** do probe, com `dmd` e `bridge` completos. Caminho normal e
dual-DSI/master-slave intactos. Guarda nova: suíte `patch-de-kernel`
(`tests/test_kernel_patch.py`), que também falha se `make defconfigs` for
esquecido.

**Imagem regravada e verificada** (`dist/x55/giochi-x55-sdcard.img`, kernel
`#3`, `Image` `7c75fda…`, `boot.scr` `e5eb7a3…`): `dd conv=fsync` +
`tools/verify-card.py` → *"idêntico à imagem, byte a byte"*. **Não bootada
ainda.**

### 2026-10-31 — BOOT COMPLETO em hardware (X55), tela ainda apagada

Cartão novo de 29,7 GB (gravado e verificado byte a byte). O sistema **subiu
inteiro** pela primeira vez:

```
[    0.000000] Booting Linux on physical CPU 0x0000000000
[    3.270510] rockchip-drm display-subsystem: bound fe040000.vop
[    3.279075] rockchip-drm display-subsystem: bound fe0a0000.hdmi
[    3.280577] dw-mipi-dsi-rockchip fe060000.dsi: *ERROR* Failed to bind: -22
[    3.280577] rockchip-drm display-subsystem: failed to bind fe060000.dsi
[    3.840] I: device_profile.json gravado em /user/.giochi/device_profile.json
[    3.840] I: control.sock ativo em /run/giochi/control.sock
[    3.841] I: musicd iniciado (pid 228)
[    3.842] I: launcher iniciado (pid 229)
[    3.842] I: Giochi Konsol OS v1.2 - giochi-init pronto
[    3.934] W: render: SDL2 indisponivel; usando backend headless
[    3.934] I: launcher v1.2 pronto: 5 abas, default 'home', render=null
[    3.934] I: render: primeiro frame, aba 'home'
[    3.934] I: giochi-musicd v1.2: 0 faixas, socket /run/giochi/music.sock
```

**3,9 s** de `Starting kernel` ao primeiro quadro. Bootloader, kernel, DTB,
SquashFS por `PARTUUID`, `rcS`, `giochi-init` e Launcher: tudo em aparelho.

**Bloqueio atual — vídeo, não sistema.** O bind do DSI falha com `-22`, então
não existe `/dev/dri/card0`; o `render: SDL2 indisponivel` é consequência (o
backend cai para `null`). Cadeia lida no fonte 6.12 e pontos de `-EINVAL` em
[../AGENTS.md](../AGENTS.md) §3.44. Descartado com medição: drivers `=y`/`=m`
misturados e DTS sem painel. O patch da ROCKNIX para a X55 não toca DSI/painel.

**Quatro defeitos silenciosos do mesmo log, corrigidos** (todos com guarda na
suíte `primeiro-boot`):

| Sintoma no log | Causa |
|---|---|
| `(device=rk3326 soc=rk3326)` numa X55 | os `-D` de identidade não chegavam ao compilador; agora `device.conf` manda |
| `buttons.map: secao [efault]` | a chave tem 19 caracteres e o código pulava 20 |
| `Usage: /etc/init.d/S10mdev {start\|stop\|restart}` | `rcS` chamava o serviço duas vezes; `S10mdev` saiu da imagem |
| `'/user/music' inacessivel`, `0 pacotes` | `user.ext4` é criado vazio e esconde os diretórios do SquashFS; `S10storage` semeia a árvore |

**Ainda aberto no aparelho:** Wi-Fi (`rtw8821c_fw.bin` não empacotado),
`Battery voltage` a cada 8 s, `regulatory.db`, áudio, gamepad
(`rocknix-joypad` não empacotado) e o `control.sock` pela tela.

## 2026-10-31 — primeiro boot medido em hardware (X55, console serial)

**O que ficou provado no aparelho** (CH340 em `ttyS2`, 1500000 8n1):

```
GIOCHI: boot.scr em execucao (mmc=1 slot=2 root=PARTUUID=47494f43-02)
GIOCHI: dtb dtbs/rockchip/rk3566-powkiddy-x55.dtb
GIOCHI: Image carregada de mmc 1:1
Starting kernel ...
 mmcblk1: p1 p2 p3 p4
VFS: Mounted root (squashfs filesystem) readonly on device 179:2.
```

- U-Boot mainline com BL31/TPL do rkbin **boota**.
- `boot.scr` regerado pelo `post-image.sh` **executa** e escolhe o slot.
- DTB em `dtbs/rockchip/<placa>.dtb` **é encontrado** (era o defeito do
  `fdtfile` com subdiretório).
- `root=PARTUUID=47494f43-02` **resolve** — sem depender de `/dev/mmcblk1p2`.
- SquashFS **monta**.

**Onde parou:** leitura de blocos com `EIO` → `SQUASHFS error: Failed to read
block 0x66: -5` → `No working init found`. Medido que a imagem está íntegra
(`unsquashfs` extraiu tudo, rc=0) e que os setores reclamados têm dados
válidos. É **mídia/gravação**, não build. Ferramenta:
[../tools/verify-card.py](../tools/verify-card.py) e
[BOOT-DEBUG.md](./BOOT-DEBUG.md) §7.

**Estado dos aparelhos:**

| Aparelho | Situação |
|---|---|
| X55 | imagem correta; falta vencer o erro de I/O do cartão |
| X35H | sem imagem própria; painel exige `panel-generic-dsi` (fora da árvore, GPL — licenciamento adiado). Mesmo com o DTS certo a tela não acende |

**Ordem de diagnóstico que funcionou** (e que custou vários ciclos de flash
para ser descoberta): serial → quem está rodando (§1.5 do BOOT-DEBUG) → log na
partição FAT → erro de I/O. Ver `AGENTS.md` §3.38–3.43.

## S0 — Buildroot base — feito

- `buildroot/external.desc`, `external.mk` (`GIOCHI_ARCH` por SoC), `Config.in`.
- `configs/giochi_rk3326_defconfig`, `configs/giochi_rk3566_defconfig`.
- `board/giochi/{post-build.sh,post-image.sh,genimage.cfg,device_table.txt,busybox.fragment}`.
- Pacotes: `giochi-init`, `giochi-musicd` (+ alias `BR2_PACKAGE_GIOCHI_MUSIC`),
  `giochi-launcher`, `giochi-assets`, `giochi-tools`; `BR2_PACKAGE_GIOCHI_PROTON=n`.
- `BR2_EXTERNAL` root em `buildroot/` — os defconfigs ficam em
  `buildroot/configs/`, não na raiz do repositório.

## S1 — Kernel + dongles — feito

- Base **Linux 6.12** (fixada nos defconfigs), alinhada a
  [hardware-e-drivers.md](./hardware-e-drivers.md).
- `board/giochi/linux.fragment` (obrigatórios §1.2, gadget §4.6, módulos `=m`
  §1.4, remoções), `linux-zstd.fragment` (RK3566) e
  `linux-powkiddy.fragment` (X55/X35S/X35H: DRM/DSI/HX8394, RK817+I2S TDM,
  RTL8821CS SDIO/UART-H5, DWC3, hantro/RGA).
- `assert-kernel-config.sh` protege contra o `olddefconfig` derrubar
  `SND_SOC_ROCKCHIP_I2S*` (causa real de "imagem sem áudio").
- `post-build.sh` gera `rtl8821cs_config.bin` (25 bytes, sha256
  `6ddeb15f…a7ac` conforme §F5) — sem ele o `hci0` fica com MAC zero.
- `8812au` é out-of-tree: `post-build.sh` deve buscar o `.ko` do pacote externo.

## S2 — U-Boot + A/B — feito (config) / esqueleto (flip)

- `uboot.fragment` (bootcount em env FAT, redundant env, bootcommand).
- `boot.cmd` (rollback com `bootcount > 3`), `mkbootimg.sh`, `genimage.cfg`.
- **X55 agora sai com U-Boot mainline**: `powkiddy-x55-rk3566` existe upstream
  desde a **v2024.07** (pinamos 2024.10) + BL31/DDR do `rkbin`. A doc anterior
  dizia que não existia defconfig para nenhum aparelho — estava desatualizada.
  Referência que confirmou isso: `xtrempkch-droid/Walkmam_RK3566` (bring-up dos
  mesmos aparelhos), que também fixa `rk3568_bl31_v1.43.elf` /
  `rk3566_ddr_1056MHz_v1.18.bin` e os offsets 32 KiB / 8 MiB.
- `tools/check-uboot.py` valida defconfig, símbolos do fragmento e os blobs do
  rkbin por aparelho; roda em `make lint` quando `UBOOT_DIR`/`RKBIN_DIR` existem.
- Pendência: `S05update` chama `fw_setenv giochi_next_slot` — o nome da variável
  deve ser fixado junto do layout de env real.

## S3 — Init scripts — feito

- `scripts/init.d/{S05update,S10storage,S20graphics,S30swap,S40dongle,S50usb,S99launcher}`.
- `rootfs_overlay/etc/inittab`, `rcS`, `rcK`, `fstab`, `mdev.conf`, `lib.sh`.
- Instalados por `post-build.sh`; validados com `sh -n`.
- `S30swap` reescrito para §3.1/§3.2: zRAM **512 MB (1 GB) / 1 GB (2 GB)**,
  LZ4 no RK3326, `swappiness=100` (global), `-p 100`/`-p 10`, e Parallel RAID
  Swap com benchmark **random 4K** e corte de 10% (aritmética em `awk`, sem `bc`).
- `S10storage` decide o modo do Slot 2 por marcadores objetivos
  (`TYPE=swap`, `.giochi-slot2-mode`, `swapfile`, ext4/f2fs) e publica
  `/run/giochi/slot2_mode`; `lib.sh` deriva as partições de `/proc/cmdline`
  (SD = `mmcblk1` nos aparelhos de referência).
- `scripts/giochi-usb-mode` é o helper único do gadget; `S50usb` e `USB_MODE`
  em runtime chamam o mesmo código (MTP / Mass Storage com desmontagem de `/user`).

## S4 — giochi-init — feito (ciclo de vida validado por teste de integração)

- `src/init/main.c`: loop único de `poll()` sobre self-pipe (SIGCHLD/SIGTERM),
  `control.sock`, FIFO `transition` e fds evdev do hotkey.
- `supervisor.c` (restart com backoff, jogo com SIGTERM→SIGKILL 3 s),
  `control.c` (protocolo §4.9.4 + `LAUNCH_APP`), `power.c`, `device_profile.c`,
  `hotkey.c`.
- **Ciclo de vida (§2.2/§4.7)** implementado e coberto por
  `tests/integration/lifecycle_test.sh` com processos reais:
  | Transição | Launcher | Verificação |
  |---|---|---|
  | jogo | `SIGTERM` + `waitpid`, reativado ao sair | 6 checagens (ordem inclusa) |
  | app externo (Ports/Loja) | `SIGSTOP` → `SIGCONT` (não morre) | 4 checagens |
  | pacote `.giochi` | extrai `bin/` e resolve `entrypoint` | 4 checagens |
- Bugs corrigidos nesse caminho (existiam antes): (a) o auto-restart relançava o
  Launcher **durante** o jogo; (b) ao sair do app o Launcher ficava suspenso
  para sempre. Ambos cobertos por `held`/`suspended` + `foreground_running()`.
- `src/init/game.c`: `giochi_game_prepare()` — `SHA-256` do pacote, extração
  idempotente em `/run/giochi/<sha16>/`, `entrypoint` do metadata inline (FIFO)
  com fallback para o `metadata.json` extraído, validação `bin/` sem `..`.
- Contratos expostos:
  - `control.sock`: `PING`, `POWEROFF`, `REBOOT`, `SUSPEND`, `USB_MODE MTP|MASS`,
    `SET_THEME <id>`, `WIFI_UP|WIFI_DOWN`, `BT_UP|BT_DOWN`, `LAUNCH_APP <id>`.
  - `transition` (FIFO): `giochi_transition_msg` (magic `GTC1`, version 1) —
    ver `src/common/giochi_transition.h`.
- `LAUNCH_APP` em `control.sock` usa a mesma transição de app (§4.7).
- `device_profile.json` embute SoC + aparelho (`-DGIOCHI_SOC`,
  `-DGIOCHI_DEVICE` = `generic|x55|x35s|x35h`): RAM, painel nativo,
  `network.integrated` (X55 tem RTL8821CS) e `usb_gadget.mass_storage`
  (false em X35S/X35H, F12). `device_id` ainda sem serial de hardware.

## S5 — Launcher — feito (motor) / esqueleto (widgets)

- C++17: `main.cpp`, `layout.cpp`, `theme.cpp`, `settings.cpp`, `overlay.cpp`,
  `input.cpp`, `evdev.cpp`, `ipc_client.cpp`, `metadata.cpp`, `library.cpp`,
  `render.cpp` (SDL2) + `render_null.cpp` (headless).
- Layout schema 1.6 com fallback compilado; validação de `order/default/center`.
- Rejeita `arch != aarch64`, `runtime=proton` sem suporte e `entrypoint` fora de
  `bin/` (sem `..`).
- Combinação Home/Start compartilhada com o supervisor via
  `common/giochi_hotkey.c` (uma única fonte para os 500 ms).
- **`/etc/giochi/buttons.map`** (§2.2): parser de seções (`default`, `x35s`),
  nomes canônicos → código evdev, e o `evdev.cpp` resolve os códigos pelo mapa
  carregado (fallback compilado idêntico a `buttons.default.json`). A aba
  Biblioteca navega itens e envia `LAUNCH` com o `metadata.json` inline.
- TODO: desenho do grid 4×3/topbar/hint bar e contabilidade de VRAM/LRU.

## S6 — Configurações — esqueleto

- `overlay.cpp` com os 7 itens de §4.9.3 e navegação/‑toggle.
- `settings.cpp`: schema 1.1, clamp de faixas, `defaults.json` compilado,
  corrompido → `.bak` + defaults.
- TODO: painel de detalhe, `control.sock` para USB/rede, scan `iw`.

## S7 — Música — feito (protocolo) / esqueleto (áudio)

- `musicd.c`: varredura MP3/FLAC/OGG, `PLAY/PAUSE/NEXT/PREV/SEEK/STATUS/QUEUE`
  com respostas JSON de §4.8.4.
- `audio_alsa.c` abre `default` e pausa; decodificação PCM é `TODO`.
- `assets/music.json` (Apple Music adaptado, 640×480).
- Pendência: `music_queue.json` (§4.8.4 item 4) e cliente de UI da aba.

## S8 — Gamescope + Mesa — feito (script)

- `S20graphics` com `MESA_LOADER_DRIVER_OVERRIDE` por SoC, `modetest` de
  sanidade, comando Gamescope canônico e fallback direto para `giochi-init`.

## S9 — .giochi + thumbgen — feito (pack/extract) / esqueleto (thumb)

- `tools/giochi-pack.sh` (ordem ADR-11, validação via `jq`).
- `giochi-extract.c` agora tem **dois backends**: libarchive (Buildroot) e um
  leitor **ustar embutido** (sempre disponível) — o caminho de instalação não
  depende de biblioteca externa. Valida `usta r` (`name`+`prefix`), recusa `..`
  e caminhos absolutos, limita membro a 256 MB e falha se nenhum membro
  esperado for encontrado. Validado ponta a ponta no host.
- `giochi-thumbgen.c` (152×85 RGBA; placeholder sem libpng).
- `schemas/metadata.schema.json` (draft-07).
- TODO: extração do PNG do tar antes do decode; `zstd -1` no metadata.

## S10 — USB gadget — feito (boot, runtime, MTP real e SSH)

- **Gadget composto**: `ecm.usb0` (ou `rndis.usb0`) está em **todos** os modos,
  ao lado de `ffs.mtp` ou `mass_storage.0`. É por ele que se faz SSH.
- **MTP de verdade**: o `f_mtp` do kernel é só transporte; o protocolo é
  respondido pelo `umtprd` (pacote `giochi-mtpd`) sobre FunctionFS
  (`CONFIG_USB_F_FS`). Sem isso o PC via o device e não listava arquivos — era
  a lacuna funcional do USB.
- **SSH via USB (adendo §4.6.1, Modo Desenvolvedor)**: console `usb0` =
  `10.55.0.2/24`, PC `10.55.0.1/24` **estático** (não há DHCP no console).
  `dropbear` escuta **somente** em `10.55.0.2:22`. Desativado por padrão:
  `S55usbnet`/`S62sshd` saem antes de carregar qualquer daemon (0 MB em
  repouso). Autenticação por senha **ou** chave — chave em
  `/user/ssh/authorized_keys`, senha por `GIOCHI_ROOT_PASSWORD` no build.
- `DEV_MODE ON|OFF` (control.sock) liga/desliga o Modo Desenvolvedor; o
  Launcher alterna em Configurações → Sistema → Modo Desenvolvedor e persiste
  `developer.ssh_usb_enabled` em `settings.json`.
- `USB_MODE` em runtime reaplica rede + MTP; o modo é persistido pelo Launcher
  em `settings.json` (o `settings.json` é propriedade dele, §4.9.5).
- Testes: `tests/integration/usbgadget_test.sh` valida o configfs gerado
  (rede sempre presente, ffs no modo MTP, mass no modo MASS, `off` desanexa) e
  `tests/integration/devmode_test.sh` valida o contrato do adendo nos scripts
  de init (IPs, gate do Modo Desenvolvedor, bind só em `10.55.0.2:22`).

## S10b — USB gadget (histórico)

- `scripts/giochi-usb-mode <mtp|mass|off>`: desanexa UDC, limpa links/funções,
  recria a identificação (serial de `/user/.giochi/serial`), vincula a função e
  reanexa. Mass Storage desmonta `/user` (exigência da §4.6).
- `S50usb` aplica o modo de `settings.json`; `USB_MODE MTP|MASS` em
  `control.sock` chama o mesmo helper (`power.c`), sem duplicar configfs.
- Degradação: sem `CONFIG_USB_F_MTP` cai para Mass Storage; sem UDC apenas loga
  (X35S/X35H não ancoram — hardware-e-drivers.md F12).
- TODO: daemon MTP de userland (≤ 2 MB RAM) expondo a Partição C.

## S11 — Dongles — feito (hotplug) / esqueleto (UI)

- `giochi-dongle-hotplug` (tabela VID:PID, espera ≤ 3 s), `mdev.conf`,
  `S40dongle` (Wi-Fi/BT condicionados a `settings.json`).
- TODO: indicador de topbar e telas de Rede/BT no overlay.

## S12 — Update Ed25519 — feito

- `giochi-sign.sh`, `giochi-keys.sh`, `giochi-verify.c`
  (`SHA-256` do manifesto + Ed25519), `S05update` com `dd conv=fsync`.
- Verificado ponta a ponta: assinatura válida → `OK`; imagem adulterada → exit 1;
  chave errada → exit 1; binário sem OpenSSL → exit 3.
- `# DECISÃO`: verificação via OpenSSL (libcrypto) com chave raw de 32 bytes; sem
  libcrypto o update é **recusado**, nunca aceito.

## S13 — giochi-forge — feito

- `src/forge/giochi_forge/`: `profile.py` (device_profile + 3 perfis manuais),
  `classify.py` (§4.5.3, pior caso, lista todas as pendências),
  `package.py` (build/assinar/chaves), `__main__.py` (CLI).
- Testes em `tests/forge/` (classificação, ordem do tar, rejeições).

## S14 — Proton stub — feito

- `metadata.cpp` e `classify.py` rejeitam `runtime=proton` quando
  `proton.supported=false`; `BR2_PACKAGE_GIOCHI_PROTON=n` por padrão; flag
  `GIOCHI_ENABLE_PROTON` chega ao Launcher via Buildroot.
- Critérios de ativação documentados em
  [architecture-v1.2.md §4.10](./architecture-v1.2.md) — o README deixou de
  ser a especificação; ele só resume e aponta.

## Imagens de documentação — feito

- `tools/make-screenshots.py` (+ `make screenshots`) gera `docs/screenshots/`
  a partir dos assets reais: tema, layout, regiões e o `bootlogo.rgba`.
- `docs/SCREENSHOTS.md` deixa explícito que são **renders do layout**, não
  capturas de hardware — o desenho dos widgets no SDL2 ainda é esboço.
- `tests/test_screenshots.py` amarra isso: dimensões, cor de fundo vinda do
  tema, acento presente, letterbox da X55 e splash idêntico ao asset.
- Quando `render.cpp` ficar completo, o caminho passa a ser captura no aparelho
  (`fbgrab`/DRM) e substituição dos arquivos.

## Bootsplash — feito (Launcher ligado) / pré-pronto (U-Boot e kernel)

- `tools/make-bootlogo.py` gera, a partir de `docs/logo.jpg`:
  `assets/bootlogo/bootlogo.rgba` (640×349, ~893 KB) + `bootlogo.json` para o
  Launcher, e `assets/bootlogo/<aparelho>/splash.bmp` (24-bit, painel nativo:
  640×480 ou 1280×720 na X55) para o U-Boot. `make bootlogo` regenera.
- Launcher (SDL2): desenha o splash nos primeiros `GIOCHI_SPLASH_FRAMES`
  (45 ≈ 0,75 s) ou até a primeira entrada; `GIOCHI_NO_SPLASH=1` desliga;
  arquivo ausente/inválido só gera log — nunca falha o boot.
- U-Boot: bloco `CONFIG_SPLASH_*`/`CONFIG_VIDEO_ROCKCHIP` comentado em
  `uboot.fragment`, `load ... ${splashimage}` comentado em `boot.cmd`, e
  `splash.bmp` já copiado para a partição A por `post-image.sh`/`mkbootimg.sh`.
- Kernel: `CONFIG_LOGO_CUSTOM` comentado em `linux.fragment` com a decisão
  registrada (exige patch e custo de boot; U-Boot + Launcher cobrem o caso).
- Teste: `tests/test_bootlogo.py` valida formato/dimensões contra
  `devices/*.conf` (suíte 12).

## S15 — QA — feito (scripts + checklist)

- `tools/bench-boot.sh`, `tools/stress-mem.sh`, `tools/stress-daemon.sh`.
- [QA-CHECKLIST.md](./QA-CHECKLIST.md) + matriz de testes.
- `make test` (host): todas as suítes, incluindo Python.

## Estado do CI (evidência, não suposição)

Última verificação no **runner real** (GitHub Actions), em 2026-10-27:

| Workflow | Commit | Resultado | O que prova |
|---|---|---|---|
| **CI** | `eb32b18` | ✅ success | `make lint` (18 .md, âncoras inclusas), build e as 15 suítes |
| **CI** | `b84b9ef` | ✅ success | correção dos símbolos do defconfig |
| **Images** (x55) | `b84b9ef` | 🔄 em execução | passou `Validar simbolos dos defconfigs` e `Preflight`; o erro de `BR2_LEGACY` **não ocorre mais** e o build seguiu para os pacotes de host |
| Images | `b096b8a`, `019b39b` | ❌ failure | histórico: `Makefile.legacy` (corrigido em `b84b9ef`) |

O que isso **ainda não** prova: que a imagem gerada liga no aparelho. Quando o
run terminar, baixe o artefato, grave e registre aqui o resultado (é o item 1 do
[roadmap](./roadmap.md)).

## Roadmap e instruções para agentes

- O que falta, em ordem de risco e com critério de aceite:
  [roadmap.md](./roadmap.md). Este HANDOFF conta o **passado** (o que foi feito e
  por quê); o roadmap conta o **futuro**.
- Convenções, armadilhas silenciosas e protocolo de registro:
  [../AGENTS.md](../AGENTS.md).

## Pendências transversais

1. `giochi-launcher`: desenho real dos widgets e medição de VRAM.
2. `giochi-musicd`: decodificação PCM (mpg123/FLAC/vorbis) → ALSA `dmix`.
3. ~~Daemon MTP de userland~~ — feito (`giochi-mtpd`/umtprd).
4. `S05update`: nome final da variável de env do U-Boot para o flip de slot.
   Update remoto (Wi-Fi) continua fora: só SD/arquivo, como na §4.3.
5. `giochi-thumbgen`: extração de `thumb.png`/`capa.png` do tar (libarchive).
6. `device_id`: derivar do serial do device tree.
7. **Hardware (docs/hardware-e-drivers.md §9):** trazer `rocknix-joypad`,
   `panel-generic-dsi` e o DTS `rk3566-powkiddy-x35s` (X35S/X35H) — sem eles não
   há gamepad / painel nessas duas placas. O DTS da X55 é mainline.
8. **Layout × painel:** `layout.json` é 640×480 (spec §4.1.3), mas a X55 tem
   painel 720×1280 com `rotation = <270>` (§F8). Falta um `layout-<device>.json`
   (ou escala/rotação no render) — hoje o SDL2 apenas estica para a tela.
9. **Áudio RK817:** aplicar `Playback Mux = HP` na inicialização (§3.3); em `SPK`
   o alto-falante fica mudo (jack continua funcionando). Hoje o ponto está
   marcado em `src/musicd/audio_alsa.c`.
10. **USB na X35S/X35H:** não ancora (`failed to enable ep0out`); `S50usb` deve
    degradar para "USB indisponível" em vez de logar erro repetido.
