# Changelog

Formato baseado em [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/)
e versionamento [SemVer](https://semver.org/lang/pt-BR/).

## [Não publicado]

### Adicionado
- **README reescrito** com galeria visual: `docs/screenshots/*.png` geradas por
  `tools/make-screenshots.py` a partir dos assets reais (`theme.json`,
  `layout.json`, `home.json`, `library.json`, `music.json`) e do splash de
  verdade (`assets/bootlogo/bootlogo.rgba`). `make screenshots` regenera;
  [docs/SCREENSHOTS.md](docs/SCREENSHOTS.md) explica o que as imagens são e o
  que **não** são (o desenho dos widgets no SDL2 ainda é esboço).
- **U-Boot mainline na X55**: `devices/x55.conf` passou para
  `GIOCHI_UBOOT_UPSTREAM=yes` com `powkiddy-x55-rk3566` (existe desde a v2024.07)
  + BL31/DDR do `rkbin`. A imagem da X55 sai **bootável de fábrica**.
- `tools/check-uboot.py`: valida defconfig, símbolos do `uboot.fragment` e os
  blobs de TPL/BL31 declarados por aparelho. Roda em `make lint`.
- `tests/test_screenshots.py` (6 checagens): dimensões, cor de fundo vinda do
  tema, acento presente, letterbox da X55 e splash idêntico ao asset.
- `tools/check-links.py` agora valida também as tags de imagem HTML.

### Corrigido
- **Build das imagens abortava** em `Makefile.legacy` com "You have legacy
  configuration in your .config!" sem dizer qual símbolo era o culpado:
  `BR2_PACKAGE_RFKILL` foi **removido** do Buildroot (o `rfkill` agora vem do
  util-linux). Trocado por `BR2_PACKAGE_UTIL_LINUX_RFKILL`.
- Três símbolos **inexistentes** no Buildroot 2024.02 eram descartados em
  silêncio, deixando recursos fora da imagem sem nenhum aviso:
  `BR2_PACKAGE_LIBMPG123` → `BR2_PACKAGE_MPG123` (o pacote chama-se `mpg123`),
  `BR2_LINUX_KERNEL_INSTALL_INTREE` (nunca existiu; o `Image` já vai para
  `images/`) e `BR2_TARGET_ROOTFS_SQUASHFS4_NO_XATTR` (o Buildroot não expõe
  essa opção).
- `tools/gen-defconfigs.sh` duplicava o bloco de DTS: o `awk` casava com o
  comentário "Placeholders:" do topo do template, gerando
  `override: reassigning to symbol BR2_LINUX_KERNEL_INTREE_DTS_NAME`. Os
  marcadores de bloco agora só casam quando estão sozinhos na linha.
- `tools/check-buildroot-symbols.py` (novo) compara cada `BR2_*` dos
  defconfigs com a árvore do Buildroot e falha com LEGACY/INEXISTENTE. Roda em
  `make lint` (quando `BUILDROOT_DIR` existe), no workflow de imagens e no
  início de cada build em `tools/build-image.sh`.
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
