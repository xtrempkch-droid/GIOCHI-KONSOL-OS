# Changelog

Formato baseado em [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/)
e versionamento [SemVer](https://semver.org/lang/pt-BR/).

## [Não publicado]

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
