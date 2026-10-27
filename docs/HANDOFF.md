# Handoff — Giochi Konsol OS v1.2

Estado de implementação por sessão do [Prompt.md](../Prompt.md). A
especificação normativa é [architecture-v1.2.md](./architecture-v1.2.md).

Para gerar imagens por aparelho (incluindo X35S/X35H com DTS externo ou
fallback de teste), veja [DEVICES.md](./DEVICES.md) e `tools/build-image.sh`.

Cada bloco `S<N>` lista o que existe hoje, o que ficou como `TODO(v1.2)` e as
decisões tomadas (`# DECISÃO`) que afetam a sessão seguinte.

Legenda: **feito** (compila e/ou roda), **esqueleto** (interface + stub com
TODO), **doc** (apenas especificação/script).

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

## S10 — USB gadget — feito (boot e runtime)

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
- Critérios de ativação documentados em README §4.10 (não reescritos aqui).

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
- `make test` (host): 9 suítes, incluindo Python.

## Pendências transversais

1. `giochi-launcher`: desenho real dos widgets e medição de VRAM.
2. `giochi-musicd`: decodificação PCM (mpg123/FLAC/vorbis) → ALSA `dmix`.
3. Daemon MTP de userland (Partição C via `f_mtp`); o gadget já está pronto.
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
