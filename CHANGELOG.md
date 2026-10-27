# Changelog

Formato baseado em [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/)
e versionamento [SemVer](https://semver.org/lang/pt-BR/).

## [Não publicado]

### Corrigido
- `scripts/init.d/*` (9 scripts) e `rootfs_overlay/etc/init.d/{rcS,rcK}` nunca
  foram versionados: o padrão `*.d` do `.gitignore` casa também com **nomes de
  diretório**, e `init.d/` era engolido. O build local funcionava e o checkout
  do CI saía sem a sequência de boot (falhava em `make lint` com link
  quebrado). Sem esses arquivos a imagem compilaria e não iniciaria.
- `tools/check-tracked.sh` (novo, roda em `make lint`) falha quando um arquivo
  do projeto está no `.gitignore` e confirma que os 11 scripts de init estão
  versionados.
- `post-build.sh`: script de init ausente agora é erro fatal, em vez de skip
  silencioso que gerava imagem sem boot.
- `tools/check-links.py` passa a **descobrir** os arquivos Markdown em vez de
  usar lista fixa: a especificação normativa (`docs/architecture-*.md`) e o
  `hardware-e-drivers.md` não estavam sendo verificados. 10 → 15 arquivos.
- Referências que ainda tratavam o README como especificação (`README §4.10` em
  `docs/HANDOFF.md`) e contagens de suítes desatualizadas em README, BUILD,
  HANDOFF e template de PR — números fixos em documentação desatualizam a cada
  suíte nova, então passaram a ser descritos em vez de contados.

### Adicionado
- Build multi-aparelho: `buildroot/board/giochi/devices/*.conf` como fonte única
  e defconfigs gerados por `tools/gen-defconfigs.sh` (X55, X35S, X35H,
  RK3566 genérico, RK3326 genérico).
- `tools/build-image.sh` com preflight (`--check`), build por aparelho, `--all`,
  DTS externo (`GIOCHI_DTS_PATH`/`GIOCHI_DTB_PATH`) e fallback de teste
  (`--fallback-dts`).
- Layout próprio da X55 (1280×720 após rotação) em `assets/devices/x55/`.
- `docs/DEVICES.md` com matriz de aparelhos e receita de DTS externo.
- CI (`.github/workflows/ci.yml`) e workflow de imagens
  (`.github/workflows/images.yml`).
- Adendo §4.6.1 — **Modo Desenvolvedor (SSH via USB)**:
  - `developer.ssh_usb_enabled` em `settings.json` e bloco `developer` em
    `defaults.json`/`schemas/settings.schema.json`; `developer.ssh_usb` no
    `device_profile.json` reflete o estado atual.
  - Configurações → Sistema → **Modo Desenvolvedor** (desligado por padrão) e
    comando `DEV_MODE ON|OFF` em `control.sock`.
  - Console `10.55.0.2/24` e PC `10.55.0.1/24` **estático** (não há DHCP no
    console; `udhcpd` removido do rootfs).
  - `S55usbnet`/`S62sshd` saem sem carregar nada quando o modo está desligado
    (0 MB em repouso); dropbear escuta **só** em `10.55.0.2:22`, com
    autenticação por senha **ou** chave.
  - `/etc/dropbear` passa a ser symlink para `/user/.ssh/dropbear` (a raiz é
    SquashFS somente leitura).
- `tests/integration/devmode_test.sh` (19 checagens) cobrindo o contrato do
  adendo 4.6.1 nos scripts de init.

## [1.2.0] — 2026-10

### Adicionado
- Especificação consolidada v1.2 (5 abas, Ports, Música, Configurações, dongles
  USB, pré-suporte a Proton) em `docs/architecture-v1.2.md`.
- `giochi-init` (ADR-10): supervisor com self-pipe de sinais, `control.sock`,
  FIFO `transition`, hotkey Home+Start e geração de `device_profile.json`.
- Ciclo de vida completo: `SIGTERM`+`waitpid` do Launcher antes do jogo e
  reinício ao sair; suspensão (`SIGSTOP`/`SIGCONT`) para apps externos.
- `giochi-launcher` (ADR-09, C++17 + SDL2): layout schema 1.6 com fallback,
  tema, `settings.json`, overlay de Configurações, `/etc/giochi/buttons.map`,
  biblioteca com validação de `arch`/`entrypoint`/`runtime`.
- `giochi-musicd`: varredura de MP3/FLAC/OGG e protocolo §4.8.4
  (`PLAY`, `PAUSE`, `NEXT`, `PREV`, `SEEK`, `STATUS`, `QUEUE`).
- `.giochi` (ADR-11): `giochi-pack.sh`, `giochi-extract` (ustar embutido +
  libarchive) e `giochi-thumbgen`.
- Update A/B (ADR-12): `giochi-sign.sh`, `giochi-keys.sh` e `giochi-verify`
  (Ed25519 + SHA-256), com recusa explícita quando o binário não tem suporte.
- `giochi-forge` (S13): perfil do dispositivo, classificação
  Compatível/Parcial/Incompatível (§4.5.3) e empacotamento/assinatura.
- Scripts de init `S05update`, `S10storage`, `S20graphics`, `S30swap`,
  `S40dongle`, `S50usb`, `S99launcher`, além de `giochi-usb-mode` e
  `giochi-dongle-hotplug`.
- Kernel 6.12 com fragmentos (base, ZSTD/RK3566, Powkiddy) e
  `assert-kernel-config.sh`.
- 11 suítes de teste, incluindo integração de processo real.

### Corrigido
- Auto-restart do supervisor ressuscitava o Launcher **durante** o jogo.
- Launcher permanecia suspenso após sair de um app externo.
- `giochi-extract` recusava todo pacote em hosts/builds sem libarchive.

[1.2.0]: https://github.com/xtrempkch-droid/GIOCHI-KONSOL-OS/releases/tag/v1.2.0
