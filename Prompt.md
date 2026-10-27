# 🎯 Prompt Mestre para IA de 2022 — Giochi Konsol OS v1.2

> **Como usar:** Este é um prompt de **orquestração por sessões**. A IA de 2022 tem janela curta — **nunca peça tudo de uma vez**. Cada bloco `S<N>` abaixo é uma sessão independente. Cole sempre o **Contexto Mínimo Persistente** no topo de cada sessão. Ao final de cada sessão, a IA emite um **HANDOFF** que você cola na sessão seguinte.

---

## 📌 CONTEXTO MÍNIMO PERSISTENTE (colar no topo de TODA sessão)

```
Você é engenheiro sênior de sistemas embarcados implementando o
"Giochi Konsol OS v1.2" (spec anexa). Responda apenas o artefato pedido
na sessão atual.

REGRAS IMUTÁVEIS (ADR) — implemente, não questione:
- ADR-01 U-Boot (Rockchip) A/B + bootcount + altboot.
- ADR-02 BusyBox init via /etc/inittab. SEM systemd.
- ADR-03 musl-libc; TODOS os binários recompilados do fonte no Buildroot.
- ADR-04 Gamescope (Wayland+DRM/KMS, sem XWayland, sem shell).
- ADR-05 Mesa + Panfrost (RK3566) / Lima+Panfrost (RK3326).
- ADR-06 OpenGL ES 3.0 (ES 2.0 fallback). SEM Vulkan no RK3326.
- ADR-07 Godot 3.5 LTS (RK3326) / Godot 4.x Compatibility-GLES3 (RK3566).
- ADR-08 ALSA direto. Pulse/PipeWire FORA da base.
- ADR-09 Launcher C++17 + SDL2, desalocação explícita, SEM GC.
- ADR-10 Supervisor giochi-init (C puro): pai de Launcher, jogo e daemons.
- ADR-11 .giochi = tar puro; metadata opcional zstd -1; capa.png/bin sem recompressão.
- ADR-12 Update A/B + Ed25519 + SHA-256 + rollback via bootcount.
- ADR-13 aarch64 canônico; metadata.json declara "arch".
- ADR-14 Rede/BT OPCIONAIS via dongle USB (Wi-Fi/BT). Sistema funciona sem dongle.

NOVO EM v1.2 (crítico):
- 5 abas, ordem visual: Música → Biblioteca → Home(centro) → Loja → Ports.
  L/R troca aba (wrap). Boot sempre na Home.
- B = "sobe um nível" (na Home é inerte).
- Home curto (<500ms) = vai para Home; Hotkey+Start ≥500ms = encerra jogo.
- Select abre/fecha overlay de Configurações (interno, não é app).
- Grid 4x3 estilo YouTube, tema Netflix dark (#0F0F0F/#E50914 fallback).
- Daemon giochi-musicd persistente (ALSA + socket /run/giochi/music.sock).
- Ports = APP:portmaster externo; Loja = APP:giochi-store.
- device_profile.json gerado no boot por giochi-init e exposto via USB.
- giochi-forge (PC) classifica Compatível/Parcial/Incompatível.
- USB com PC: MTP (padrão) ou Mass Storage (alternativo), via configfs.
- Proton: stub arquitetural DESATIVADO (#ifdef GIOCHI_ENABLE_PROTON=n).

ALVOS: RK3326 (A35, Mali-G31, 1-2GB) e RK3566 (A55, Mali-G52, 2GB).
METAS: SquashFS ≤150MB, RAM repouso ≤60MB, boot ≤8s/≤6s,
VRAM UI ≤4MB, .ko dongles ≤4MB, firmware ≤2MB.

FORMATO DE RESPOSTA:
- Entregue SÓ o artefato da sessão.
- Código completo e compilável (musl-gcc / BusyBox ash). Sem "..." vago.
- Decisão não coberta: use "# DECISÃO: <opção conservadora> — <1 linha>".
- No fim: bloco "=== HANDOFF S<N> → S<N+1> ===" com (arquivos gerados,
  decisões, pendências, resumo ≤10 linhas para colar na próxima sessão).

ANTI-ALUCINAÇÃO:
- NUNCA use: systemd, glibc pré-compilado, Pulse/PipeWire na base,
  Vulkan no RK3326, XWayland, shell Wayland. Se faltar dado, decida
  conservador e marque # DECISÃO.
- Não repita a spec; referencie por ID (ADR-NN, §N.N).
- Se >400 linhas, divida em parte 1/2 e 2/2 na MESMA resposta.
```

---

## 🗂️ MAPA DE SESSÕES v1.2 (ordem recomendada)

| # | Sessão | Artefato | Depende |
|---|---|---|---|
| S0 | Buildroot base + árvore `board/giochi/` | defconfigs RK3326/RK3566 | — |
| S1 | Kernel (inclui USB gadget + módulos dongle) | `linux.fragment` | S0 |
| S2 | U-Boot + layout A/B + `boot.cmd` | `uboot.config`, `boot.cmd`, `mkbootimg.sh` | S0,S1 |
| S3 | Init scripts base (`inittab`, `S05update`, `S10storage`, `S30swap`, `S40dongle`, `S99launcher`) | scripts ash | S1,S2 |
| S4 | Supervisor `giochi-init` (Launcher/jogo/daemons + `control.sock` + `device_profile.json`) | `giochi-init.c` | S3 |
| S5 | Launcher — motor de layout + grid + abas + tema | `giochi-launcher` (C++17/SDL2) | S4 |
| S6 | Overlay de Configurações | `settings.cpp`/`settings.json` schema | S5 |
| S7 | Aba Música + `giochi-musicd` | `giochi-musicd.c` + `music.json` | S4,S5 |
| S8 | Gamescope + Mesa/Panfrost | `S20graphics` | S1,S5 |
| S9 | Formato `.giochi` + thumbgen + schema | `giochi-pack.sh`, `giochi-thumbgen.c`, JSON schema | S5 |
| S10 | USB gadget (MTP/Mass Storage) | `S50usb` + regras configfs | S1,S4 |
| S11 | Dongles USB (Wi-Fi/BT) — drivers + UI | `S40dongle`, `mdev.conf`, `wpa`/`bluez` stubs | S1,S4,S6 |
| S12 | Update Ed25519 + A/B (final) | `giochi-sign.sh`, `giochi-verify.c`, `S05update` final | S2,S3 |
| S13 | `giochi-forge` (PC) — detecção + classificação + empacotamento | CLI Python/C++ | S9 |
| S14 | Stub Proton (`#ifdef GIOCHI_ENABLE_PROTON`) + schema | patch + docs | S4,S13 |
| S15 | QA / boot budget / matriz de testes | checklist + scripts | todas |

---

## 📦 PROMPTS POR SESSÃO (v1.2)

### 🔹 S0 — Buildroot base

```
[Sessão S0: Buildroot base v1.2]
Gere:
1. `buildroot_defconfig` RK3326 (aarch64, Cortex-A35, musl, BusyBox init,
   BR2_PACKAGE_GIOCHI_PROTON=n, BR2_PACKAGE_GIOCHI_MUSIC=y).
2. `buildroot_defconfig` RK3566 (Cortex-A55, mesmos flags, + crypto).
3. Árvore `board/giochi/` (post-build.sh, post-image.sh, rootfs_overlay/
   com /etc/inittab e /etc/init.d/ vazios, /etc/giochi/ vazio,
   /lib/firmware/ vazio).
4. Lista EXATA de BR2_PACKAGE_* a habilitar: SDL2, SDL2_image, mesa3d,
   alsa-lib, libmpg123, libFLAC, libvorbis, zstd, tar, squashfs, u-boot,
   linux, wpa_supplicant, iw, bluez-utils (opcional), libarchive,
   jq (só host), mdev.
5. `post-image.sh` que monta A/B (SquashFS) + partição C (ext4).
6. `post-build.sh` que copia theme.json/defaults.json + módulos .ko de dongle
   para /lib/modules/ (sem carregá-los no boot).

NÃO gere kernel/U-Boot/scripts de init. Apenas ganchos e defconfig.
```

---

### 🔹 S1 — Kernel + dongles

```
[Sessão S1: Kernel Linux v1.2]
Fragmento `board/giochi/linux.fragment`:
- Obrigatórios: TMPFS, ZRAM (LZ4 default RK3326; ZSTD aceitável RK3566),
  SQUASHFS, INPUT_EVDEV, JOYSTICK (+XPAD), SWAP, PRINTK(dev)/n(prod),
  DRM_PANFROST=y, DRM_LIMA=y (RK3326).
- USB gadget (§4.6): USB_GADGET=y, USB_CONFIGFS=y, USB_F_MTP=y,
  USB_F_MASS_STORAGE=y, USB_LIBCOMPOSITE=y.
- Módulos dongle (§1.4) como =m (não carregam no boot):
  8188eu, rtl8192cu, 8812au, mt7601u, ath9k_htc, btusb.
- Removidos: NVIDIA/AMD, BLK_DEV_SR, ISO9660_FS, PCI/PCIe genéricos,
  rede cabeada alta capacidade, subsistemas desktop clássicos.
- Firmwares em /lib/firmware/ (lista: rtl8761bu_fw.bin, BCM20702A1-*.hcd,
  rtl8188eufw.bin, rtl8192cufw.bin, mt7601u.bin, ar9271.fw, ath9k_htc/*).

Entregue: fragment + tabela diferenças RK3326/RK3566 + comando de merge.
```

---

### 🔹 S2 — U-Boot + A/B

```
[Sessão S2: U-Boot A/B]
1. `uboot.config`: bootcount, altboot, env em FAT partição A.
2. `boot.cmd`: lê bootcount; >3 → rollback para slot anterior; carrega
   boot.img do slot ativo.
3. `mkbootimg.sh`: gera boot.img por slot.
4. Layout de partições (tabela) para SD 8GB e 16GB:
   A(boot), B1, B2, C(user), [Slot 2: swap OU user2 — exclusivo].
5. Regra de detecção Slot 2 (referência ao S10storage/S3).
```

---

### 🔹 S3 — Init scripts

```
[Sessão S3: Scripts de init BusyBox ash v1.2]
Cada arquivo completo, testável, idempotente, `logger -t`:

1. `/etc/inittab`: ordem S05 → S10 → S20 → S30 → S40dongle → S50usb → S99launcher.
2. `S05update`: detecta giochi_update.img na C → `giochi-verify` (stub) →
   aplica A/B (dd+fsync) → flip via fw_setenv → apaga em sucesso.
3. `S10storage`: Slot 2 → modo A (swapfile) OU modo B (/user2 ext4). Nunca ambos.
4. `S30swap`: exatamente o script da §3.2 (awk, sem bc, 4K random, ≤10%,
   SWAP_MODE=A|B, `swapon -p 10`).
5. `S40dongle`: (a) garante módulos .ko em /lib/modules; (b) regras em
   /etc/mdev.conf chamam `modprobe` por VID:PID (tabela embutida);
   (c) se `wlan0` aparecer e wifi_enabled=true, sobe wpa_supplicant+udhcpc;
   (d) se `hci0` aparecer e bluetooth_enabled=true, inicia bluetoothd.
6. `S50usb`: monta gadget configfs conforme settings.usb_mode (MTP|MASS).
7. `S99launcher`: exec giochi-init.

Regras: POSIX sh, exit codes, timeout em espera de interface (≤3s).
```

---

### 🔹 S4 — `giochi-init`

```
[Sessão S4: giochi-init.c v1.2]
C99, musl, sem deps (libc + <linux/input.h> + POSIX). Gerencia 3 tipos:
Launcher (SIGTERM+waitpid ao lançar jogo), Jogo (fork/exec, hotkey BTN_MODE+
BTN_START ≥500ms → SIGTERM→SIGKILL após 3s), Daemon persistente
(giochi-musicd, wpa_supplicant, bluetoothd — NÃO mata ao lançar jogo).

Implemente:
1. Loop principal + self-pipe SIGCHLD (sem race).
2. FIFO `/run/giochi/transition` (struct binária versionada: cmd, path,
   metadata JSON inline).
3. Socket texto `/run/giochi/control.sock`: POWEROFF, REBOOT, SUSPEND,
   USB_MODE MTP|MASS, SET_THEME <id>, WIFI_UP, WIFI_DOWN, BT_UP, BT_DOWN.
4. Gerar `/user/.giochi/device_profile.json` no boot a partir de
   /proc/cpuinfo, /sys/class/drm, device tree, versão OS. Schema §4.5.2
   (bloco `proton` com supported=false, reason por SoC).
5. Iniciar giochi-musicd no boot e reativar ao voltar do jogo.
6. Hotkey via /dev/input/event* (evdev).

Entregue: giochi-init.c + Makefile (musl-gcc -O2, -static se viável) +
struct do FIFO + tabela de estados do control.sock.
```

---

### 🔹 S5 — Launcher (grid + abas + tema)

```
[Sessão S5: giochi-launcher C++17 + SDL2 v1.2]
Implemente:
1. Motor de layout que lê `layout.json` (schema 1.6, §4.1.3) + layouts por
   aba (home/library/music) + `theme.json`. Fallback compilado garantido.
2. 5 abas na ordem music→library→home(centro)→store→ports. Boot na home.
   L/R (BTN_TL/BTN_TR) troca com wrap + fade 150ms. Aba ativa sublinhada.
3. Grid 4x3 estilo YouTube (tile 152x85 + título 20px, scroll vertical,
   overscan 1 linha). Topbar 40px + hint bar 30px persistentes.
4. Botões: A ativa, B sobe um nível (Home=inerte), Home curto (<500ms)=Home,
   Select=abre overlay Config (delega S6), Hotkey+Start repassado ao supervisor.
5. VRAM ≤4MB: texturas por aba ≤24, eviction LRU, capa.png 320x320 nunca
   no grid (só thumb 152x85 pré-gerada — delega S9), Música ≤1MB.
6. Envia mensagem ao FIFO /run/giochi/transition ao lançar .giochi.
7. Rejeita pacote com arch≠aarch64 ou runtime=proton com supported=false.
8. Parser JSON mínimo (sem deps externas) e loader de buttons.map.

Entregue: main.cpp + layout.hpp + render.cpp + metadata.hpp + Makefile.
```

---

### 🔹 S6 — Configurações (overlay)

```
[Sessão S6: Overlay de Configurações v1.2]
Overlay modal INTERNO (não app externo), resposta ≤100ms abrir / ≤50ms
fechar, RAM ≤3MB, VRAM ≤2MB.

1. Menu 300px esquerda + painel 300px direita, header 40px, hint 30px.
2. Itens (§4.9.3): Aparência, Sistema (idioma, fuso, brilho, suspensão,
   modo USB), Som, Loja, Rede (Wi-Fi/BT via dongle), Sobre, Desligar.
3. Componentes whitelist: settings_menu, toggle, slider, dropdown,
   button_action, info_list.
4. Persistência `/user/settings.json` (schema §4.9.5) com fallback
   `defaults.json` compilado; corrompido → .bak + defaults.
5. Ações via `/run/giochi/control.sock` (protocolo texto do S4).
6. Rede: sem dongle → "Nenhum adaptador"; com dongle → scan iw, conectar
   wpa_supplicant, status IP/sinal, esquecer. BT: ativar hci0 (pareamento
   fase 2).

Entregue: settings.cpp/hpp + defaults.json + settings.json schema +
handlers de control.sock.
```

---

### 🔹 S7 — Aba Música + `giochi-musicd`

```
[Sessão S7: Música v1.2]
1. `giochi-musicd.c` (C, musl): varre /user/music/ (MP3/FLAC/OGG) via
   libmpg123/libFLAC/libvorbis, decodifica para ALSA (dmix, 48kHz),
   expõe socket texto `/run/giochi/music.sock` com comandos PLAY, PAUSE,
   NEXT, PREV, SEEK <seg>, STATUS, QUEUE (respostas JSON §4.8.4).
   RAM ≤8MB, CPU ≤3% (RK3326) / ≤2% (RK3566).
2. `music.json` (layout Apple Music adaptado 640x480, §4.8.2): now_playing
   160px (capa 120x120), progress 20px, controls 50px, track_list restante,
   hint_bar 30px. Componentes: album_art, progress_bar, transport_controls,
   track_list. VRAM ≤1MB.
3. Cliente no Launcher (≤1MB RAM): A=play/pause, X=next, Y=prev,
   D-pad↑↓ playlist, A na playlist toca faixa, B volta, L/R troca aba.
4. Persistência da fila em `/user/.cache/music_queue.json`.
5. Fallbacks §4.8.6 (daemon ausente, socket off, dmix falha, pasta vazia,
   arquivo corrompido → pula).

Entregue: giochi-musicd.c + Makefile + music.json + client.cpp (Launcher).
```

---

### 🔹 S8 — Gamescope + Mesa

```
[Sessão S8: Gamescope + Panfrost]
1. `S20graphics`: env vars por SoC
   (RK3566: MESA_LOADER_DRIVER_OVERRIDE=panfrost;
    RK3326: detecta GPU → lima|panfrost).
   Sobe Gamescope backend DRM puro, fullscreen, resolução KMS detectada
   (fallback 640x480@60), sem XWayland, sem shell, executando Launcher.
2. Comando Gamescope canônico comentado (flags: --backend drm --fullscreen
   --embedded).
3. Verificação de sanidade: comandos `drm_info`/`modetest` + saída esperada
   confirmando Panfrost/Lima.
4. Fallback se DRM puro falhar no RK3326: documentar plano B (ex.: backend
   headless + KMS direto no Launcher, a validar).
```

---

### 🔹 S9 — Formato `.giochi` + thumbgen

```
[Sessão S9: .giochi + thumbnails v1.2]
1. Especificação do container: tar puro; membros na ordem
   metadata.json (zstd -1 opcional) → capa.png (320x320) → thumb.png
   (320x180, OPCIONAL) → bin/. Offsets, tamanho máx de metadata.
2. JSON Schema draft-07 para metadata.json cobrindo: title, developer,
   version, arch, runtime (godot|retroarch|proton), graphics_api,
   graphics_api_fallback, buttons_map, entrypoint.
3. `giochi-pack.sh`: valida schema via jq → tar. Rejeita sem capa.png.
   Aceita runtime=proton no schema (Launcher rejeita em runtime).
4. `giochi-thumbgen.c`: gera /user/.cache/thumbs/<sha256>.rgba
   (152x85 RGBA ≈51KB). Se thumb.png existe, usa; senão center-crop 16:9
   do capa.png.
5. `giochi-extract.c`: extrai APENAS bin/ para /run/giochi/<sha>/ via
   libarchive em streaming; valida SHA-256 do membro.
6. Casos de erro: tar truncado, hash inválido, arch incompatível, runtime
   proton sem suporte.
```

---

### 🔹 S10 — USB gadget (MTP / Mass Storage)

```
[Sessão S10: USB gadget §4.6]
1. `S50usb` (init): configura configfs conforme settings.usb_mode:
   - MTP: monta f_mtp, expõe partição C, sobe daemon MTP (≤2MB RAM).
   - MASS: monta f_mass_storage no cartão SD (exige desmontagem da C).
2. Tabela de configfs (comandos mkdir/echo/ln) para cada modo.
3. Troca de modo em runtime via control.sock USB_MODE (chamado pelo S6).
4. Segurança: .giochi transferido é validado pelo Launcher (Ed25519+arch).
5. Notas Windows/Linux/macOS (§4.6 tabela).
```

---

### 🔹 S11 — Dongles USB (Wi-Fi/BT)

```
[Sessão S11: Dongles USB v1.2 §1.4/§4.11]
1. `/etc/mdev.conf` + script auxiliar: ao inserir dongle, lê VID:PID e faz
   modprobe do módulo (tabela embutida: 8188eu, rtl8192cu, 8812au, mt7601u,
   ath9k_htc, btusb). Chipset desconhecido → log + "Dongle não reconhecido".
2. `S40dongle` (referência cruzada com S3): se wlan0 e wifi_enabled=true →
   wpa_supplicant + udhcpc; se hci0 e bluetooth_enabled=true → bluetoothd.
   Sem dongle: custo zero de RAM.
3. `network_indicator` na topbar (Wi-Fi/BT cinza quando ausente).
4. Tabela de fallback §1.4 (nenhum dongle, não suportado, Wi-Fi sem rede,
   BT inserido, dois Wi-Fi → primeiro usado).
5. Orçamento: .ko ≤4MB, firmware ≤2MB, RAM Wi-Fi ≤6MB, BT ≤4MB,
   ambos ≤10MB, detecção ≤3s.
```

---

### 🔹 S12 — Update Ed25519 + A/B (final)

```
[Sessão S12: Update v1.2]
1. `giochi-sign.sh`: assina imagem (Ed25519 via openssl pkeyutl, justifique)
   gerando .sig + manifesto JSON (versão + SHA-256).
2. `giochi-verify.c`: binário estático musl; valida assinatura + SHA-256
   contra `/etc/giochi/pubkey.ed25519` (ro em B1/B2).
3. `S05update` final (substitui stub do S3): fluxo arquivo→verify→dd+fsync
   slot inativo→fw_setenv flip→apaga→log. Assinatura inválida → abort,
   preserva para diagnóstico.
4. Geração de chaves (comandos `openssl genpkey -algorithm ed25519`).
5. Matriz de estados (arquivo presente? assinatura ok? slot ativo?) → ação.
```

---

### 🔹 S13 — `giochi-forge` (PC)

```
[Sessão S13: giochi-forge v1.2]
App de PC (CLI Python 3 ou C++ — escolha Python para portabilidade PC).
1. Lê `device_profile.json` exposto via MTP/Mass Storage (S10).
2. Compara com `forge.json` do projeto Godot (ou inferência conservadora
   do project.godot).
3. Classifica Compatível/Parcial/Incompatível conforme tabela §4.5.3
   (pior caso; lista TODOS os requisitos não atendidos).
4. Compila/transfere apenas Compatíveis (Parciais com aviso explícito).
5. Empacota .giochi via giochi-pack.sh (S9) + assina com giochi-sign (S12).
6. Fallback sem USB: perfil manual (RK3326 1/2GB, RK3566 2GB), importar
   device_profile.json, ou modo avançado (sem filtro, com aviso).
7. Independente de USB para empacotar (saída em pasta local).
```

---

### 🔹 S14 — Stub Proton

```
[Sessão S14: Proton stub v1.2 §4.10]
1. Patch em giochi-init e Launcher com `#ifdef GIOCHI_ENABLE_PROTON`
   (default `n`). Custo zero na v1.2.
2. `device_profile.json` já contém bloco `proton` (S4) — validar schema.
3. Launcher rejeita runtime=proton se supported=false com mensagem clara.
4. giochi-forge classifica Incompatível (S13 já trata).
5. Documentar critérios de ativação futura: flag y + SoC com Vulkan ≥1.1
   maduro + RAM ≥4GB + CPU ≥ Cortex-A76 + perfil supported=true.
```

---

### 🔹 S15 — QA / boot budget

```
[Sessão S15: QA v1.2]
1. Checklist Markdown por camada (Kernel, Init, Supervisor, Launcher, 5 abas,
   Música, Configurações, Ports, USB gadget, Dongles, Update, Memória, Boot)
   com comando + valor esperado.
2. `bench-boot.sh`: mede tempo até Launcher visível, RAM repouso
   (`free -m`), tamanho SquashFS, VRAM via `/sys/class/drm/card0/...`
   (ou estimativa pelo Launcher).
3. `stress-mem.sh`: valida zRAM (-p 100) + swap SD (-p 10) com pressão
   controlada; valida prioridade.
4. `stress-daemon.sh`: mede RAM/CPU de giochi-musicd, wpa_supplicant,
   bluetoothd; valida sobrevivência a lançamento de jogo.
5. Matriz de testes: RK3326 vs RK3566 × {sem dongle, Wi-Fi, BT, ambos} ×
   {slot 1, slot 1+2A, slot 1+2B} × {MTP, Mass Storage}.
```

---

## 🔁 REGRA DE ENCADEAMENTO

Ao final de **cada** sessão a IA deve emitir:

```
=== HANDOFF S<N> → S<N+1> ===
Artefatos gerados: <lista>
Decisões (# DECISÃO): <bullets>
Pendências: <bullets>
Colar no topo da próxima sessão:
<resumo ≤10 linhas com: arquivos-chave criados, interfaces expostas
 (FIFO/socket/struct), decisões que afetam a próxima sessão>
```

O usuário cola esse **HANDOFF** logo após o **Contexto Mínimo Persistente** e o prompt da sessão seguinte. Assim a IA de 2022 mantém coerência sem o documento inteiro.

---

## ⚠️ INSTRUÇÕES ANTI-ALUCINAÇÃO (reforço, colar após o Contexto)

```
- Referencie por ID (ADR-NN, §N.N) — nunca reescreva a spec.
- Se o artefato exceder ~400 linhas, divida em parte 1/2 e 2/2 na MESMA
  resposta, com cabeçalho claro.
- Nunca invente campos de metadata.json, settings.json, layout.json ou
  device_profile.json além dos documentados (§4.1.3, §4.5.2, §4.9.5).
- Shell deve rodar em BusyBox ash; C deve compilar com musl-gcc -std=c99.
- Se um requisito for ambíguo, escolha a opção mais conservadora e marque
  "# DECISÃO: <justificativa em 1 linha>".
```

---

**Fim do prompt mestre v1.2.** Use na ordem **S0 → S15**. Cada sessão cabe em janela de 2022 (~4k–8k tokens) e o handoff garante continuidade entre elas.