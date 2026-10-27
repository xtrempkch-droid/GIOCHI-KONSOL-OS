# Giochi Konsol OS

[![CI](https://github.com/xtrempkch-droid/GIOCHI-KONSOL-OS/actions/workflows/ci.yml/badge.svg)](https://github.com/xtrempkch-droid/GIOCHI-KONSOL-OS/actions/workflows/ci.yml)
[![Images](https://github.com/xtrempkch-droid/GIOCHI-KONSOL-OS/actions/workflows/images.yml/badge.svg)](https://github.com/xtrempkch-droid/GIOCHI-KONSOL-OS/actions/workflows/images.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

Sistema operacional enxuto para portáteis ARM (Rockchip RK3326 / RK3566), feito
para boot rápido, pouca RAM e jogos em OpenGL ES — sem systemd, sem PulseAudio,
sem desktop.

Alvo: **1–2 GB de RAM**, tela sem toque, entrada por botões (gamepad evdev),
armazenamento em microSD (single ou dual slot). Rede e Bluetooth são
**opcionais**: integrados quando o aparelho tem rádio, ou via dongle USB.

> **Estado:** v1.2. O núcleo (supervisor, launcher, daemon de música,
> empacotamento/assinatura, update A/B) está implementado e com testes. A
> renderização dos widgets e a decodificação de áudio ainda são esqueletos —
> veja a [tabela de estado](#estado-da-implementação).

## Estado da implementação

| Área | Situação | Onde |
|---|---|---|
| Buildroot (`BR2_EXTERNAL`), 5 aparelhos, genimage A/B | ✅ funcional | [buildroot/](buildroot/) |
| Kernel 6.12 + fragmentos (gadget, dongles, Powkiddy) | ✅ funcional | [board/giochi](buildroot/board/giochi) |
| `giochi-init`: supervisor, `control.sock`, FIFO, hotkey, `device_profile.json` | ✅ funcional | [src/init](src/init) |
| Ciclo de vida Launcher/jogo/app (§2.2/§4.7) | ✅ funcional, testado | [tests/integration](tests/integration/lifecycle_test.sh) |
| Launcher C++17: layout 1.6, 5 abas, `buttons.map`, biblioteca, overlay | 🟡 motor pronto, widgets pendentes | [src/launcher](src/launcher) |
| `giochi-musicd`: socket/protocolo §4.8.4 | 🟡 protocolo pronto, decode ALSA pendente | [src/musicd](src/musicd) |
| `.giochi`: pack, extract (ustar embutido), thumbgen | ✅ pack/extract, 🟡 thumbnails | [tools/](tools/) |
| Update A/B com Ed25519 + SHA-256 | ✅ funcional (verificado ponta a ponta) | [tools/giochi-verify.c](tools/giochi-verify.c) |
| `giochi-forge`: detecção, classificação, assinatura | ✅ funcional | [src/forge](src/forge) |
| USB: MTP + rede/SSH + Mass Storage (gadget composto) | ✅ funcional | [scripts/giochi-usb-mode](scripts/giochi-usb-mode), [docs/USB.md](docs/USB.md) |
| Dongles USB Wi-Fi/BT | ✅ hotplug e scripts, 🟡 UI de rede | [scripts/init.d/S40dongle](scripts/init.d/S40dongle) |

## Aparelhos

| Aparelho | SoC | Painel | Rádio | Status |
|---|---|---|---|---|
| Powkiddy X55 | RK3566 | 720×1280 (rot. 270°) | RTL8821CS integrado | **validado em hardware** |
| Powkiddy X35S | RK3566 | 640×480 | não medido | não validado (DTS externo) |
| Powkiddy X35H | RK3566 | 640×480 | nenhum | parcial |
| RK3566 genérico | RK3566 | 640×480 | dongle USB | bring-up |
| RK3326 genérico | RK3326 | 640×480 | dongle USB | bring-up |

Como gerar a imagem de cada um (incluindo fallback para placas sem DTS
upstream): [docs/DEVICES.md](docs/DEVICES.md).

## Começando

### Build + testes no host

Só precisa de `gcc`/`g++` (C99/C++17) e `python3`. As bibliotecas opcionais
(SDL2, ALSA, libpng, libarchive, OpenSSL) são detectadas por `pkg-config`; sem
elas o código usa os stubs previstos na especificação.

```sh
make            # build/host/bin/{giochi-init,giochi-musicd,giochi-launcher,...}
make test       # unitários, integração (ciclo de vida, USB, Modo
                # Desenvolvedor), bootlogo e forge
make lint       # shell, JSON, defconfigs, links e arquivos versionados
make bootlogo   # regenera o splash a partir de docs/logo.jpg (precisa de PIL)
```

### Imagem para o cartão

```sh
export BUILDROOT_DIR=/caminho/para/buildroot
tools/build-image.sh --check --all      # valida sem compilar
tools/build-image.sh x55                # gera dist/x55/giochi-x55-sdcard.img
tools/build-image.sh --all --jobs 8     # todos os aparelhos
```

Grave com `dd if=dist/x55/giochi-x55-sdcard.img of=/dev/sdX bs=4M conv=fsync`
ou Balena Etcher. O cartão sai com A/B (SquashFS), partição `/user` (ext4) e
`extlinux.conf` apontando para o DTB do aparelho.

> **Bootloader:** o U-Boot mainline não tem defconfig para X55/X35S/X35H.
> A imagem é gerada sem bootloader; use o do vendor
> (`GIOCHI_UBOOT_BIN=... tools/build-image.sh x55`). Detalhes em
> [docs/DEVICES.md](docs/DEVICES.md#bootloader-mainline-não-cobre-estes-aparelhos).

### Inspecionar o aparelho por USB

O console expõe rede no mesmo gadget do MTP. O **Modo Desenvolvedor** (SSH via
USB) vem **desativado**; ligue em Configurações → Sistema → Modo Desenvolvedor.

```sh
sudo ip addr add 10.55.0.1/24 dev usb0    # PC: estático, não há DHCP
ssh root@10.55.0.2                        # console
```

Autenticação por chave pública em `/user/ssh/authorized_keys` ou por senha de
root definida no build (`GIOCHI_ROOT_PASSWORD`). Detalhes em
[docs/USB.md](docs/USB.md).

## Como funciona

```
BootROM → U-Boot (A/B + bootcount) → kernel 6.12 → BusyBox init (/etc/inittab)
        → giochi-init (supervisor) → Gamescope (DRM/KMS) → giochi-launcher
                                        ↘ jogo / app externo
                                        ↘ giochi-musicd (persistente)
                                        ↘ umtprd (MTP) + usb0 (SSH)
```

- **Supervisor** (`giochi-init`): dono do Launcher, jogos, apps e daemons.
  Um único `poll()` sobre self-pipe de sinais, `/run/giochi/control.sock`, a
  FIFO `/run/giochi/transition` e os `event*` do hotkey.
- **Transições:** jogo → `SIGTERM`+`waitpid` no Launcher, exec, e o Launcher é
  reiniciado ao sair; app externo (Ports/Loja) → Launcher suspenso (`SIGSTOP`),
  retomado (`SIGCONT`) ao voltar.
- **UI:** 5 abas (Música · Biblioteca · Home · Loja · Ports), grid estilo
  YouTube, tema escuro. `layout.json` schema 1.6 com fallback compilado.
- **Pacotes:** `.giochi` é tar puro (ADR-11). O Launcher valida `arch`,
  `entrypoint` e `runtime` antes de listar.
- **Update:** A/B atômico, assinatura Ed25519 + SHA-256, rollback por
  `bootcount`. Update remoto depende de Wi-Fi (dongle ou integrado).
- **Memória:** zRAM 512 MB/1 GB com `-p 100`; swap em SD `-p 10` apenas no
  modo A do slot 2, com benchmark random 4K.

## Estrutura

```
src/common/     JSON, log, sockets, SHA-256, Ed25519, hotkey, FIFO
src/init/       giochi-init: supervisor, control.sock, power, device_profile, game
src/launcher/   motor de layout, tema, settings, overlay, evdev, render SDL2
src/musicd/     giochi-musicd (ALSA + música)
src/thumbgen/   thumbnails 152x85 RGBA
src/forge/      giochi-forge (PC): perfil, classificação, empacotamento
assets/         layout, tema, defaults, buttons.map, devices/<id>/, bootlogo/
schemas/        JSON Schema (metadata, forge, settings, layout)
scripts/        init.d/* e helpers (USB gadget)
buildroot/      BR2_EXTERNAL: defconfigs, board, packages
tools/          build-image, gen-defconfigs, bootlogo, pack, sign, verify, QA
tests/          unitários (C/C++), forge (Python) e integração
docs/           arquitetura, hardware, build, aparelhos, QA, handoff
```

## Documentação

| Documento | Conteúdo |
|---|---|
| [docs/architecture-v1.2.md](docs/architecture-v1.2.md) | especificação consolidada (fonte normativa) |
| [docs/architecture-v1.1.md](docs/architecture-v1.1.md) | versão anterior, para histórico |
| [docs/hardware-e-drivers.md](docs/hardware-e-drivers.md) | hardware de teste, GPIO, DTS, correções (F1–F12) |
| [docs/DEVICES.md](docs/DEVICES.md) | aparelhos, DTS, geração de imagem |
| [docs/USB.md](docs/USB.md) | MTP, Mass Storage e **SSH via USB** (10.55.0.2, Modo Desenvolvedor) |
| [docs/BOOTSPLASH.md](docs/BOOTSPLASH.md) | logo/bootsplash: assets, Launcher e ativação do U-Boot |
| [docs/BUILD.md](docs/BUILD.md) | buildroot, kernel, cross-compile, assinatura |
| [docs/QA-CHECKLIST.md](docs/QA-CHECKLIST.md) | checklist por camada e matriz de testes |
| [docs/HANDOFF.md](docs/HANDOFF.md) | estado por sessão, decisões e pendências |
| [Prompt.md](Prompt.md) | orquestração S0–S15 usada para implementar |

## Bootsplash

O logo em [docs/logo.jpg](docs/logo.jpg) vira o splash do sistema:
`make bootlogo` gera `assets/bootlogo/bootlogo.rgba` (desenhado pelo Launcher
nos primeiros frames) e `assets/bootlogo/<aparelho>/splash.bmp` (pré-pronto para
o U-Boot). Suporte, controles e como ativar: [docs/BOOTSPLASH.md](docs/BOOTSPLASH.md).

## Segurança

- Imagens de update exigem **Ed25519 + SHA-256**; binário sem suporte a
  assinatura **recusa** o update (nunca aceita imagem não verificada).
- Credenciais Wi-Fi ficam em `/user/settings.json` em texto plano na v1.2 —
  risco documentado; keyring está na fase 2.
- Chaves privadas nunca entram no repositório (`.gitignore`); só a pública vai
  para `/etc/giochi/pubkey.ed25519`.

## Contribuindo

Leia [CONTRIBUTING.md](CONTRIBUTING.md). Antes de abrir PR: `make lint`,
`make test` e `tools/build-image.sh --check --all`.

## Licença

[MIT](LICENSE). Componentes de terceiros (kernel, U-Boot, Buildroot, Mesa,
SDL2, BusyBox, ALSA) mantêm suas licenças originais. Device trees e patches
que não são upstream pertencem aos seus autores — ver
[buildroot/board/giochi/dts/README](buildroot/board/giochi/dts/README).
