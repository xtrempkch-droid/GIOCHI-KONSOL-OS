# 📑 DOCUMENTO DE ARQUITETURA TÉCNICA: GIOCHI KONSOL OS — v1.2

**Tipo:** Especificação de Requisitos e Engenharia de Sistemas Embarcados
**Versão:** 1.2 (consolidada — substitui integralmente a v1.1 e todas as revisões de seção posteriores)
**Alvo:** Dispositivos portáteis ARM (foco: Rockchip RK3326 / RK3566)
**Restrições:** Sem tela de toque; entrada por botões GPIO/Gamepad genérico; 1–2 GB RAM; armazenamento via Single ou Dual MicroSD; **rede e Bluetooth opcionais via dongle USB**.

---

## Changelog v1.1 → v1.2

| # | Mudança | Seção |
|---|---|---|
| 1 | **Layout em grid estilo YouTube + tema Netflix dark** (substitui lista vertical) | 4.1 |
| 2 | **Abas com L/R**, ordem visual **Música → Biblioteca → Home → Loja → Ports**, Home no centro, boot na Home | 4.1 |
| 3 | **Botão B** com semântica estrita de "subir um nível" | 4.1 |
| 4 | **Botão Home** com janela de 500 ms (toque curto vs. Hotkey+Start) | 4.1 |
| 5 | **Quarta aba: Ports** (`APP:portmaster`) | 4.7 |
| 6 | **Quinta aba: Música** (`music.json` + daemon `giochi-musicd`) | 4.8 |
| 7 | **Configurações via Select** (overlay modal) | 4.9 |
| 8 | **Pré-suporte a Proton** (stub desativado, custo zero) | 4.10 |
| 9 | **`giochi-forge`** (app PC) com detecção de dispositivo e filtragem de compatibilidade | 4.5 |
| 10 | **Conexão USB** com o PC (MTP padrão + Mass Storage alternativo) | 4.6 |
| 11 | **Suporte a dongle USB Wi-Fi e Bluetooth** (novo) | 1.4, 4.9, 4.11 |
| 12 | **`device_profile.json`** exposto ao `giochi-forge` | 4.5.1 |
| 13 | **ADR-14:** rede/BT via dongle USB como caminho oficial | ADR |

---

## 0. DECISÕES FIXAS (ADR — Architecture Decision Records)

| ID | Decisão | Justificativa |
|---|---|---|
| ADR-01 | **Bootloader:** U-Boot (Rockchip fork) | A/B, `bootcount`, `altboot`. |
| ADR-02 | **Init:** BusyBox `init` via `/etc/inittab` | Boot rápido, footprint mínimo. |
| ADR-03 | **Libc:** musl-libc, recompilação obrigatória de todos os binários de runtime a partir do fonte | Tamanho/RAM. Exceções documentadas em ADR próprio. |
| ADR-04 | **Compositor:** Gamescope (Wayland + backend DRM/KMS, sem XWayland) | Fullscreen único, baixo overhead. |
| ADR-05 | **GPU:** Mesa 3D com Panfrost (RK3566) ou Lima/Panfrost (RK3326) | Pilha 100% open source. |
| ADR-06 | **API gráfica:** OpenGL ES 3.0 (mín.); ES 2.0 fallback. **Vulkan não suportado** no RK3326. | Compatibilidade Mali-G31/G52. |
| ADR-07 | **Runtime Godot:** 3.5 LTS (RK3326) / 4.x Compatibility-GLES3 (RK3566) | Evitar exigência de Vulkan. |
| ADR-08 | **Áudio:** ALSA direto (`libasound`). Pulse/PipeWire fora da base. | Footprint; jogos usam ALSA. |
| ADR-09 | **Launcher:** C++17 + SDL2, desalocação explícita (sem GC) | Memória determinística. |
| ADR-10 | **Supervisor:** `giochi-init` (C puro), pai do Launcher, jogos, apps e daemons. | Transição confiável. |
| ADR-11 | **Pacote:** `.giochi` = `tar` puro; metadados opcionalmente `zstd -1`; `capa.png`/`bin/` sem recompressão. | Streaming sem extrair tudo. |
| ADR-12 | **Update:** A/B atômico + Ed25519 + SHA-256 + rollback via U-Boot `bootcount`. | Integridade e anti-brick. |
| ADR-13 | **Arquitetura:** aarch64 canônico; `metadata.json` declara `arch`; Launcher rejeita incompatível. | Portabilidade explícita. |
| **ADR-14** | **Rede e Bluetooth:** **opcionais via dongle USB** (Wi-Fi e/ou BT). SoCs sem rádio integrado usam dongle. Nenhum dongle é obrigatório para o sistema funcionar. | Cobertura de hardware; custo zero quando ausente. |

---

## 1. CAMADA 1 — CORE DO SISTEMA OPERACIONAL

**Metas:** SquashFS ≤ 150 MB; RAM em repouso ≤ 60 MB; boot até Launcher ≤ 8 s (RK3326) / ≤ 6 s (RK3566).

### 1.1 Toolchain e Bibliotecas Base

- **Compilador:** GCC otimizado.
  - RK3326: `-march=armv8-a+crc -mtune=cortex-a35 -O3`
  - RK3566: `-march=armv8-a+crc+crypto -mtune=cortex-a55 -O3`
- **Libc:** musl-libc (ADR-03). Todos os binários de runtime recompilados no Buildroot.
- **Init:** BusyBox `init` (ADR-02). Scripts SysV-like em `/etc/init.d/`. Sem systemd.
- **Bootloader:** U-Boot (ADR-01) com A/B e `bootcount`.

### 1.2 Customização do Kernel Linux

**Removidos:** drivers NVIDIA/AMD; mídias ópticas; PCI/PCIe genéricos (exceto SoC); rede com fio de alta capacidade; subsistemas de desktop clássicos.

**Obrigatórios:**
- `CONFIG_TMPFS=y`, `CONFIG_ZRAM=y` (LZ4 no RK3326), `CONFIG_SQUASHFS=y`
- `CONFIG_INPUT_EVDEV=y`, `CONFIG_JOYSTICK=y` (+ `CONFIG_JOYSTICK_XPAD`)
- `CONFIG_SWAP=y`
- **Novos (v1.2):** `CONFIG_USB_GADGET=y`, `CONFIG_USB_CONFIGFS=y`, `CONFIG_USB_F_MTP=y`, `CONFIG_USB_F_MASS_STORAGE=y` (Seção 4.6)
- **Novos (v1.2):** módulos de dongle USB sob demanda (Seção 1.4)

**Logs:** `CONFIG_PRINTK=y` em dev; `n` em produção após validação.

### 1.3 Layout de Partições (imutabilidade)

**Slot 1 (obrigatório):**

| Partição | Conteúdo | FS | Modo |
|---|---|---|---|
| A | U-Boot, `boot.img`, env U-Boot | raw/FAT | rw (bootloader) |
| B1 | SquashFS (sistema base) | SquashFS | ro |
| B2 | Cópia inativa (A/B) | SquashFS | ro |
| C | `/user/` (saves, jogos, músicas, ports, settings) | ext4/F2FS | rw |

**Slot 2 (opcional, modo exclusivo escolhido no boot):**
- **Modo A — Swap Dedicado:** partição `swapfile` (RAID swap com Slot 1). Sem `/user/`.
- **Modo B — User Data Estendido:** `/user2/`. Sem swap.

**Regra:** `/etc/init.d/S10storage` detecta o conteúdo do Slot 2 e escolhe o modo. Nunca ambos.

### 1.4 🆕 Suporte a Dongle USB (Wi-Fi e Bluetooth)

**Princípio (ADR-14):** o sistema funciona **sem** rede/BT. Quando um dongle USB é inserido, os drivers correspondentes são carregados sob demanda (`udev`/`mdev` + `modprobe`), e as interfaces aparecem (`wlan0`, `hci0`). Nenhum dongle é obrigatório para boot, jogos, Launcher, Música ou Ports.

**Drivers incluídos na SquashFS (módulos `.ko` compilados, mas não carregados no boot):**

| Categoria | Chipsets suportados (mínimo v1.2) | Módulo |
|---|---|---|
| Wi-Fi USB | Realtek RTL8188EU/EUS, RTL8192CU, RTL8812AU (comum em dongles genéricos) | `8188eu`, `rtl8192cu`, `8812au` |
| Wi-Fi USB | MediaTek MT7601U | `mt7601u` |
| Wi-Fi USB | Atheros AR9271 | `ath9k_htc` |
| Bluetooth USB | CSR8510 (dongle BT 4.0 clássico) | `btusb` |
| Bluetooth USB | Realtek RTL8761B (BT 5.0) | `btusb` + firmware `rtl8761bu_fw.bin` |
| Bluetooth USB | Broadcom BCM20702 | `btusb` + firmware `BCM20702A1-*.hcd` |

**Firmware:** os arquivos `.bin`/`.hcd` correspondentes ficam em `/lib/firmware/` na SquashFS (custo estimado: ~1,5 MB).

**Carregamento sob demanda:**
- `mdev` (BusyBox) ou `udev` leve detecta o `VID:PID` do dongle na inserção.
- Regras em `/etc/mdev.conf` (ou `/etc/udev/rules.d/`) fazem `modprobe` do módulo correspondente.
- Se o módulo não existir para aquele `VID:PID`, o dongle é ignorado com log; o sistema continua funcionando.

**Userland:**
- **Wi-Fi:** `wpa_supplicant` + `udhcpc` (BusyBox) + `iw` para scan/conexão. Gerenciado pelo overlay de Configurações (Seção 4.9, item "Rede").
- **Bluetooth:** `bluez` mínimo (`bluetoothd` + `bluetoothctl` opcional) **somente se** um dongle BT for detectado. Sem dongle, `bluetoothd` não é iniciado (custo zero de RAM).
- **Persistência:** configurações de rede/BT em `/user/settings.json` (Seção 4.9.5). Ao reconectar o mesmo dongle, as credenciais são reaplicadas.

**Orçamento (adendo à Seção 8):**

| Métrica | Meta |
|---|---|
| Tamanho dos módulos `.ko` na SquashFS | ≤ 4 MB |
| Tamanho dos firmwares em `/lib/firmware/` | ≤ 2 MB |
| RAM — `wpa_supplicant` + `udhcpc` (Wi-Fi ativo) | ≤ 6 MB |
| RAM — `bluetoothd` (BT ativo) | ≤ 4 MB |
| RAM — com ambos os dongles ativos | ≤ 10 MB (adicional ao repouso) |
| Tempo de detecção + `modprobe` na inserção | ≤ 3 s |

**Fallback:**

| Cenário | Comportamento |
|---|---|
| Nenhum dongle inserido | `wlan0`/`hci0` ausentes; Configurações exibe "Nenhum adaptador de rede detectado"; sistema funciona normalmente |
| Dongle com chipset não suportado | Ignorado com log; Configurações exibe "Dongle não reconhecido" |
| Dongle Wi-Fi inserido, sem rede configurada | `wlan0` sobe, mas sem conexão; usuário configura via Configurações |
| Dongle BT inserido | `hci0` sobe; pareamento via Configurações (fase 2) |
| Dois dongles Wi-Fi simultâneos | Apenas o primeiro é usado; o segundo é ignorado com log |

**Fora de escopo v1.2:** pareamento BT completo via UI (fase 2); suporte a dongles 5G/4G (modem USB); Wi-Fi 6E; Bluetooth LE Audio.

---

## 2. CAMADA 2 — SUBSISTEMA GRÁFICO E EXECUÇÃO

### 2.1 Compositor e Renderização

- **Compositor:** Gamescope (ADR-04), backend DRM/KMS, fullscreen, sem XWayland.
- **Fluxo:** Kernel → U-Boot → BusyBox init → `giochi-init` → Gamescope → Launcher/jogo/app.
- **Drivers:** Mesa + Panfrost/Lima. **API:** OpenGL ES 3.0 (mín.), ES 2.0 fallback. **Vulkan não suportado** no RK3326.

### 2.2 Ciclo de Vida e Supervisor `giochi-init`

O `giochi-init` gerencia **três tipos de processo**:

| Tipo | Exemplos | Comportamento ao lançar jogo | Comportamento ao sair |
|---|---|---|---|
| **Launcher** | `giochi-launcher` | Recebe `SIGTERM`; aguardado com `waitpid` | Reiniciado após o jogo |
| **Jogo** | `bin/jogo.arm64`, RetroArch, port | `fork`+`execve`; acesso exclusivo à CPU/RAM | `waitpid` detecta fim; Launcher reiniciado |
| **Daemon persistente** | `giochi-musicd`, `wpa_supplicant`, `bluetoothd` | **Não** é morto; continua rodando | Permanece até desligar/suspender |

**Fluxo de transição Launcher → jogo:**
1. Launcher envia IPC a `giochi-init` via `/run/giochi/transition` (caminho do pacote + `metadata.json`).
2. `giochi-init` envia `SIGTERM` ao Launcher; aguarda `waitpid`.
3. `giochi-init` faz `fork`+`execve` do binário do jogo.
4. `giochi-init` lê `/dev/input/event*` e monitora **Hotkey (BTN_MODE) + Start (BTN_START)** por ≥ 500 ms.
5. Ao detectar a combinação ou fim natural, `SIGTERM` ao jogo; `SIGKILL` em 3 s se necessário.
6. `giochi-init` reinicia o Launcher.

**Mapeamento de botões:** `/etc/giochi/buttons.map`; referenciado pelo `metadata.json`. O Launcher só lê `/dev/input/event*`.

**Socket de controle:** `/run/giochi/control.sock` (texto simples) para ações de energia, tema, modo USB, rede (Seção 4.9.4).

**Geração do `device_profile.json`:** no boot, `giochi-init` gera `/user/.giochi/device_profile.json` (Seção 4.5.1.2) a partir de `/proc/cpuinfo`, `/sys/class/drm`, device tree e versão do sistema.

---

## 3. CAMADA 3 — ENGENHARIA DE MEMÓRIA

### 3.1 zRAM

- 512 MB (1 GB RAM) / 1 GB (2 GB RAM); LZ4 no RK3326, ZSTD aceitável no RK3566.
- Prioridade: zRAM `swapon -p 100`; swapfiles em SD `-p 10`.
- `vm.swappiness=100` (global); priorização via `-p`.

### 3.2 Parallel RAID Swap (opcional, Modo A do Slot 2)

Ativado somente se Slot 2 = Modo A e usuário habilitar em Configurações. Aviso de desgaste.

- Swapfile: 256 MB (mín.) / 512 MB (recomendado em 2 GB).
- Validação: throughput **random 4K**, diferença ≤ 10%.
- Script `/etc/init.d/S30swap` (mesmo da v1.1, com `SWAP_MODE` A/B).

**Riscos:** desgaste de SD; modo opcional com aviso.

---

## 4. CAMADA 4 — DISTRIBUIÇÃO, INTERFACE E APPS

### 4.1 Interface Gráfica — 5 Abas + Configurações

#### 4.1.1 Navegação e botões

| Entrada | Ação |
|---|---|
| **L (BTN_TL)** / **R (BTN_TR)** | Aba anterior / próxima (ordem visual, wrap-around) |
| **D-pad** | Move foco dentro da aba |
| **A** | Ativa item focado |
| **B** | Sobe um nível (ver 4.1.2) |
| **Home (BTN_MODE)** — toque curto (<500 ms) | Vai para aba Home |
| **Hotkey (BTN_MODE) + Start (BTN_START)** ≥ 500 ms | Encerra jogo (via `giochi-init`) |
| **Select (BTN_SELECT)** | Abre/fecha Configurações (Seção 4.9) |
| **Y** | Reservado (fase 2) |

#### 4.1.2 Ordem visual das abas

| Posição | Aba | Tipo |
|---|---|---|
| 1 (esquerda) | **Música** | Layout `music.json` + daemon |
| 2 | **Biblioteca** | Layout `library.json` |
| 3 (centro) | **Home** | Layout `home.json` |
| 4 | **Loja** | App externo `APP:giochi-store` |
| 5 (direita) | **Ports** | App externo `APP:portmaster` |

- **Boot:** sempre na **Home** (`tabs.default = "home"`).
- **Botão Home:** vai para a Home (centro).
- **B na Home:** inerte (hint cinza).
- **B nas demais abas:** volta para Home (ou um nível, dentro de apps).

#### 4.1.3 `layout.json` principal — schema 1.6

```json
{
  "schema_version": "1.6",
  "theme_id": "dark_default",
  "screen": { "width": 640, "height": 480, "orientation": "landscape" },
  "tabs": {
    "order": ["music", "library", "home", "store", "ports"],
    "default": "home",
    "visual_center": "home",
    "bar": { "position": "topbar_left", "font_size": 14, "active_style": "accent_underline" },
    "controls": { "prev": "BTN_TL", "next": "BTN_TR" },
    "switch_transition": "fade_150ms",
    "layouts": {
      "music":   "music.json",
      "library": "library.json",
      "home":    "home.json",
      "store":   "APP:giochi-store",
      "ports":   "APP:portmaster"
    }
  },
  "persistent_regions": ["topbar", "hint_bar"],
  "buttons": {
    "back": "BTN_B",
    "home": "BTN_MODE",
    "home_short_press_ms": 500,
    "settings": "BTN_SELECT",
    "exit_game_combo": ["BTN_MODE", "BTN_START"],
    "exit_game_combo_hold_ms": 500
  }
}
```

**Validação:** `order` contém todas as abas de `layouts`; `visual_center` e `default` ∈ `order`. Inválidos → fallback (`order[len/2]`, `"home"` se presente).

#### 4.1.4 Layout default — Grid estilo YouTube + tema Netflix dark

- **Grid 4×3** de tiles (152×85 + título 20px), scroll vertical, overscan de 1 linha.
- **Topbar** (40px): logo, abas, indicadores (bateria, rede, relógio).
- **Hint bar** (30px): dicas contextuais de botões.
- **Navegação:** D-pad move foco; scroll suave; preview do título completo no hint bar após 300 ms de foco.

#### 4.1.5 Componentes (whitelist schema 1.6)

`grid`, `top_bar`, `tab_bar`, `hint_bar`, `scrollbar`, `vertical_list`, `image_container`, `text_label`, `battery_indicator`, `network_indicator`, `clock`, `hero_tile`, `news_list`, `horizontal_list`, `album_art`, `progress_bar`, `transport_controls`, `track_list`, `settings_menu`, `toggle`, `slider`, `dropdown`, `button_action`, `info_list`.

Tipos desconhecidos: ignorados com log.

#### 4.1.6 Política de VRAM

| Regra | Valor |
|---|---|
| Teto global de VRAM de UI | **4 MB** |
| Texturas por aba (grid) | ≤ 24 (152×85 RGBA ≈ 51 KB) |
| Retenção de aba anterior | Sim, se couber; senão LRU |
| `capa.png` 320×320 durante navegação | **Nunca** (só no detalhe, fase futura) |
| Aba Música (capa 120×120) | ≤ 1 MB |
| Configurações (overlay) | ≤ 2 MB |

#### 4.1.7 Home — conteúdo

Três blocos: **Continuar** (hero 112×63), **Novidades** (texto de `/user/.cache/news.json`, fallback dicas compiladas), **Recentes** (5 thumbs 80×45). Pior caso ~800 KB VRAM.

#### 4.1.8 Tema escuro (`theme.json`)

Separado, referenciado por `theme_id`. Paleta base Material/YouTube dark; **acento a definir** (fallback `#E50914`). Cores: `background #0F0F0F`, `surface #1E1E1E`, `surface_elevated #2A2A2A`, `text_primary #E5E5E5`, `text_secondary #A0A0A0`, `hint_bar_bg #181818`, `divider #333333`.

#### 4.1.9 Fallback (layout)

JSON ausente/malformado → default compilado; `schema_version` MAJOR desconhecida → default + log; tipo desconhecido → ignora; thumbnail falha → placeholder; VRAM > 4 MB → eviction LRU.

### 4.2 Protocolo `.giochi`

**Formato:** `tar` puro; metadados opcionalmente `zstd -1`; `capa.png` e `bin/` sem recompressão. Runtime lê em streaming para `/run/giochi/`.

```
meujogo.giochi/
├── metadata.json
├── capa.png          # 320x320, obrigatório
├── thumb.png         # 320x180 landscape, OPCIONAL (novo v1.2)
└── bin/jogo.arm64
```

**`metadata.json` (exemplo):**

```json
{
  "title": "Meu Jogo",
  "developer": "Studio X",
  "version": "1.0.0",
  "arch": "aarch64",
  "runtime": "godot",
  "graphics_api": "gles3",
  "graphics_api_fallback": "gles2",
  "buttons_map": "default",
  "entrypoint": "bin/jogo.arm64"
}
```

- `runtime`: `"godot"` (padrão), `"retroarch"` (fase 2), `"proton"` (aceito no schema, **rejeitado** se `device.proton.supported = false`).
- Launcher **rejeita** `arch` incompatível e `runtime: "proton"` não suportado, com mensagem clara.
- Pacotes antigos (sem `thumb.png`) continuam funcionando (thumbnail gerado do `capa.png`).

**Pré-geração de thumbnails:** no install, `giochi-thumbgen` gera `/user/.cache/thumbs/<hash>.rgba` (152×85 RGBA, ~51 KB). Se `thumb.png` existir, usa; senão center-crop 16:9 do `capa.png`.

### 4.3 Atualizações (A/B + assinatura)

- Sem daemons de rede em background. Verificação **manual** pelo menu.
- Fluxo: `wlan0` sobe (se dongle Wi-Fi presente) → `HTTP GET` JSON de versão → baixa imagem → verifica **Ed25519** + **SHA-256** → escreve na partição **inativa** (B2) → marca ativa no U-Boot → reinicia → rollback automático se 3 falhas de boot.
- **Update por SD:** `S05update` verifica `giochi_update.img` na Partição C no boot; assinatura válida → aplica A/B → apaga após sucesso.
- **Sem dongle Wi-Fi:** update remoto indisponível; update por SD continua funcionando.

### 4.4 Giochi Launcher — motor de layout

- C++17 + SDL2 (ADR-09). Desalocação explícita.
- Motor de layout interpreta `layout.json` + layouts por aba + `theme.json`.
- Fallback compilado garantido.
- Configurações como overlay interno (Seção 4.9).

### 4.5 `giochi-forge` (app PC) + filtragem de compatibilidade

#### 4.5.1 Detecção do dispositivo e classificação

1. Usuário conecta o console via USB (MTP/Mass Storage — Seção 4.6).
2. `giochi-forge` lê `/user/.giochi/device_profile.json` exposto pelo console.
3. Compara requisitos do jogo (`forge.json` recomendado, ou inferência conservadora do `project.godot`).
4. Classifica: **Compatível** / **Parcial** (aviso) / **Incompatível** (bloqueia).
5. Só compila/transfere Compatíveis (e Parciais com aviso).

#### 4.5.2 `device_profile.json` (exemplo)

```json
{
  "schema_version": "1.0",
  "device_id": "giochi-rk3326-001",
  "soc": "rk3326",
  "cpu": { "arch": "aarch64", "cores": 4, "model": "Cortex-A35", "max_mhz": 1500 },
  "gpu": { "model": "Mali-G31", "driver": "panfrost", "gles_max": "3.2" },
  "ram_mb": 1024,
  "vram_mb": 64,
  "storage": { "system_mb": 150, "user_free_mb": 12000 },
  "os": { "name": "Giochi Konsol OS", "version": "1.2", "kernel": "6.1" },
  "graphics_api": { "gles3": true, "gles2": true, "vulkan": false },
  "audio": { "alsa": true, "dmix": true, "sample_rate": 48000 },
  "input": { "evdev": true, "gamepad": true, "touchscreen": false },
  "display": { "width": 640, "height": 480, "refresh_hz": 60 },
  "engine": { "godot_min": "3.5", "godot_max": "4.3", "renderer": "compatibility" },
  "network": { "wifi_usb": true, "bluetooth_usb": true, "integrated": false },
  "proton": { "supported": false, "reason": "vulkan_absent", "min_ram_mb": 4096, "min_vulkan": "1.1", "fex_required": true }
}
```

**Regenerado a cada boot** por `giochi-init`. `network.wifi_usb`/`bluetooth_usb` refletem os drivers incluídos na build (não a presença do dongle no momento).

#### 4.5.3 Regras de classificação

| Requisito | Perfil | Resultado |
|---|---|---|
| `arch` ⊇ aarch64 | `cpu.arch` | OK |
| `engine.godot` ∈ [`godot_min`,`godot_max`] | engine | OK/Incompatível |
| `graphics_api.min = gles3` | `gles3 = true` | OK |
| `graphics_api.min = vulkan` | `vulkan = false` | **Incompatível** |
| `ram_min_mb ≤ ram_mb` | ram | OK/Parcial se > 80% |
| `vram_min_mb ≤ vram_mb` | vram | OK/Parcial se próximo |
| `display.min_* ≤ display.*` | display | OK/Incompatível |
| `touchscreen_required = true` | `touchscreen = false` | **Incompatível** |
| `runtime = "proton"` | `proton.supported = false` | **Incompatível** |

**Classificação final = pior caso.** Mensagens listam **todos** os requisitos não atendidos.

**Fallback sem USB:** perfil manual (RK3326 1/2 GB, RK3566 2 GB), importar `device_profile.json`, ou modo avançado (sem filtro, com aviso).

### 4.6 Conexão USB com o PC

| Modo | Windows | Linux | macOS | Complexidade dispositivo | Complexidade PC |
|---|---|---|---|---|---|
| **MTP** (padrão) | Nativo | Nativo | Requer OpenMTP/Android File Transfer | Baixa (kernel gadget + daemon) | Nenhuma (arrastar e soltar) |
| **Mass Storage** (alternativo) | Nativo | Nativo | Nativo | Baixa (kernel gadget) | Nenhuma |

- **MTP:** `f_mtp` no kernel + daemon MTP no userland (≤ 2 MB RAM). Partição C exposta. Acesso simultâneo; seguro contra corrupção.
- **Mass Storage:** cartão exposto como bloco; exige desmontagem durante conexão. Funciona nativamente nos três SOs.
- **Seleção de modo:** via Configurações (Seção 4.9) → `control.sock` → `giochi-init` reconfigura `configfs`.
- **`giochi-forge` independente de USB:** gera `.giochi` em pasta local; usuário copia via MTP/Mass Storage. Automação via `libusb` fica para fase futura.
- **Segurança:** `.giochi` transferido é validado pelo Launcher (Ed25519 + `arch`) antes de listar.

**Fora de escopo v1.2:** automação `libusb`; ADB; WebUSB.

### 4.7 Quarta aba — Ports (PortMaster)

- **Tipo:** app externo `APP:portmaster`, lançado por `giochi-init`.
- **Função:** download/instalação de ports de jogos de PC (wrappers ARM64) em `/user/ports/`. Arquivos originais fornecidos pelo usuário (não distribuídos).
- **Ciclo:** Launcher suspenso (não morto); PortMaster fullscreen; B/Home encerram; `giochi-init` reativa Launcher na Home.
- **Orçamento:** RAM ≤ 30 MB; VRAM ≤ 4 MB; Launcher→PortMaster ≤ 800 ms; PortMaster→Home ≤ 1 s.
- **Fallback:** app ausente/crash → "Ports indisponível" + hint B; `waitpid` reativa Launcher.

### 4.8 Quinta aba — Música (`giochi-musicd` + layout Apple Music)

#### 4.8.1 Arquitetura

| Componente | Responsabilidade | Processo |
|---|---|---|
| `giochi-musicd` | Decodifica/reproduz MP3/FLAC/OGG via ALSA; varre `/user/music/` | Daemon persistente (C) |
| Aba Música (`music.json`) | UI de controle; envia comandos via socket | Layout interno do Launcher |
| `giochi-init` | Inicia daemon no boot; **não o mata** ao lançar jogos | Supervisor |

**Fluxo:** Launcher ↔ `/run/giochi/music.sock` ↔ `giochi-musicd` → ALSA (`dmix`) → áudio.

**Background:** daemon sobrevive a jogos; música continua via `dmix`. Se `dmix` falhar, daemon pausa ao iniciar jogo e retoma ao voltar.

#### 4.8.2 Layout (Apple Music adaptado, 640×480)

- **now_playing** (topo, 160px): capa 120×120 + título/artista/álbum.
- **progress** (20px): barra + tempo.
- **controls** (50px): ⏮ ⏯ ⏭.
- **playlist** (restante): track_list com scroll.
- **hint_bar** (30px, persistente).

**VRAM:** capa 120×120 (~56 KB) + ícones + fonte ≈ 700 KB. Abaixo do teto.

#### 4.8.3 Componentes novos

`album_art`, `progress_bar`, `transport_controls`, `track_list`.

#### 4.8.4 Protocolo do socket (`/run/giochi/music.sock`)

| Comando | Ação | Resposta |
|---|---|---|
| `PLAY` | Inicia/retoma | `{"status":"playing","track":"...","pos":123}` |
| `PAUSE` | Pausa | `{"status":"paused","pos":123}` |
| `NEXT` / `PREV` | Faixa seguinte/anterior | `{"status":"playing","track":"..."}` |
| `SEEK <seg>` | Pula para segundo | `{"status":"...","pos":...}` |
| `STATUS` | Estado atual | `{"status":"...","track":"...","pos":...,"dur":...,"queue":[...]}` |
| `QUEUE` | Lista fila | `{"queue":[{"title":"...","artist":"...","dur":...}]}` |

**Controles:** A = play/pause; X = próxima; Y = anterior; D-pad ↑↓ na playlist; A na playlist toca faixa; B volta um nível; L/R troca de aba.

#### 4.8.5 Orçamento

| Métrica | Meta |
|---|---|
| RAM `giochi-musicd` | ≤ 8 MB |
| RAM cliente (Launcher) | ≤ 1 MB |
| CPU background | ≤ 3% (RK3326) / ≤ 2% (RK3566) |
| VRAM aba Música | ≤ 1 MB |
| Home → Música | ≤ 200 ms |

#### 4.8.6 Fallback

Daemon ausente/crash → "Reprodutor indisponível"; socket indisponível → "Desconectado" + retry 2 s; `dmix` falha → pausa/retoma; `/user/music/` vazio → hint de cópia via USB; arquivo corrompido → pula faixa + log.

### 4.9 Configurações (overlay via Select)

#### 4.9.1 Acionamento

| Entrada | Ação |
|---|---|
| **Select** (toque curto) | Abre/fecha overlay sobre a aba atual |
| **Select** durante jogo | Ignorado (jogo pode consumir) |
| **B** no overlay | Fecha, retorna à aba anterior (estado preservado) |
| **Home** no overlay | Fecha, vai para Home |

**Tipo:** overlay modal interno do Launcher (não é app externo). Resposta ~50 ms. Launcher **não** é morto.

#### 4.9.2 Layout (640×480)

Menu vertical à esquerda (300px) + painel de detalhe à direita (300px). Header 40px, hint bar 30px. D-pad navega; A entra; B volta/fecha.

#### 4.9.3 Itens do menu (v1.2)

| Item | Sub-opções |
|---|---|
| **Aparência** | Tema (dark), cor de acento (A/B/C/D), escala de fonte/UI |
| **Sistema** | Idioma (pt-BR/en-US), fuso, brilho, suspensão, modo USB (MTP/Mass Storage) |
| **Som** | Volume master/jogo/música, saída (ALSA/dmix), mute |
| **Loja** | Região, fonte (catálogo curado / itch.io — fase 2), limpar cache |
| **Rede** | 🆕 Wi-Fi (ativar `wlan0` se dongle presente, conectar, status, esquecer), 🆕 Bluetooth (ativar `hci0`, pareamento — fase 2), status de dongles |
| **Sobre** | Versão OS/kernel, SoC, RAM, armazenamento livre, uptime, nº de série |
| **Desligar** | Desligar, reiniciar, suspender |

**Rede (v1.2):**
- Se nenhum dongle Wi-Fi: "Nenhum adaptador Wi-Fi detectado. Conecte um dongle USB."
- Se dongle presente: lista de redes (scan via `iw`), conectar (via `wpa_supplicant`), status (IP, sinal), esquecer.
- Bluetooth: se dongle BT presente, ativar `hci0`; pareamento completo na fase 2.

#### 4.9.4 Ações via `control.sock`

| Ação | Mecanismo |
|---|---|
| Desligar/Reiniciar/Suspender | `giochi-init` encerra daemons e chama `poweroff`/`reboot`/`echo mem > /sys/power/state` |
| Trocar modo USB | `giochi-init` reconfigura `configfs` |
| Aplicar tema | Launcher recarrega `theme.json` em runtime |
| Trocar idioma | Recarrega strings; reboot recomendado para apps externos |
| 🆕 Ativar/desativar Wi-Fi | `giochi-init` sobe/derruba `wlan0` (`ip link`, `wpa_supplicant`) |
| 🆕 Ativar/desativar BT | `giochi-init` inicia/para `bluetoothd` |

**Protocolo:** texto simples via `/run/giochi/control.sock` (`POWEROFF`, `REBOOT`, `SUSPEND`, `USB_MODE MTP|MASS`, `SET_THEME <id>`, `WIFI_UP`, `WIFI_DOWN`, `BT_UP`, `BT_DOWN`).

#### 4.9.5 `settings.json` (persistência)

`/user/settings.json` (rw). Fallback: `defaults.json` compilado. Corrompido → renomeia para `.bak`.

```json
{
  "schema_version": "1.1",
  "appearance": { "theme_id": "dark_default", "accent_color": "#E50914", "font_scale": 1.0, "ui_scale": 1.0 },
  "system": { "language": "pt-BR", "timezone": "America/Sao_Paulo", "brightness": 80, "suspend_timeout_s": 300, "usb_mode": "mtp" },
  "sound": { "master_volume": 70, "game_volume": 80, "music_volume": 60, "muted": false },
  "store": { "region": "BR", "source": "curated" },
  "network": {
    "wifi_enabled": false,
    "wifi_last_ssid": null,
    "wifi_credentials": [],
    "bluetooth_enabled": false,
    "bluetooth_paired": []
  }
}
```

**Credenciais Wi-Fi:** armazenadas em `/user/settings.json` (Partição C, rw). Não há keyring criptografado na v1.2; documentar como risco (fase 2: keyring).

#### 4.9.6 Componentes novos

`settings_menu`, `toggle`, `slider`, `dropdown`, `button_action`, `info_list`.

#### 4.9.7 Orçamento

| Métrica | Meta |
|---|---|
| RAM overlay | ≤ 3 MB |
| VRAM overlay | ≤ 2 MB |
| Abrir/fechar | ≤ 100 ms / ≤ 50 ms |
| Desligar (Select → poweroff) | ≤ 3 s |

#### 4.9.8 Fallback

`settings.json` corrompido → `defaults.json`; `control.sock` indisponível → "Indisponível"; ação falha → mensagem + sistema ligado; tema inválido → mantém; idioma sem tradução → en-US.

### 4.10 Pré-suporte a Proton (porta aberta, desativada)

- **Não implementado na v1.2.** Stub arquitetural com `#ifdef GIOCHI_ENABLE_PROTON` (padrão `n`).
- `device_profile.json` tem bloco `proton` sempre presente, `supported = false` nos SoCs atuais (`reason`: `vulkan_absent` no RK3326, `vulkan_immature` no RK3566, `ram_insufficient` se < 4 GB).
- `.giochi` com `runtime: "proton"` é aceito no schema, **rejeitado** pelo Launcher se `proton.supported = false`.
- `giochi-forge` classifica como Incompatível.
- **Custo na v1.2: zero** (não compilado).
- **Ativação futura:** flag `y` + SoC com Vulkan ≥ 1.1 maduro + RAM ≥ 4 GB + CPU single-core ≥ Cortex-A76 + perfil com `supported = true`.

### 4.11 🆕 Dongles USB — UI e comportamento

- **Detecção:** `mdev`/`udev` + `modprobe` ao inserir; `wlan0`/`hci0` aparecem.
- **Configurações → Rede:** lista dongles detectados, estado, conectar/desconectar.
- **Indicador na topbar:** `network_indicator` reflete `wlan0` (Wi-Fi) e `hci0` (BT) quando presentes.
- **Sem dongle:** indicador cinza; Configurações exibe mensagem.
- **Múltiplos dongles Wi-Fi:** primeiro usado; demais ignorados com log.
- **Dongle não suportado:** ignorado com log; Configurações exibe "Dongle não reconhecido".
- **Persistência:** credenciais Wi-Fi em `settings.json`; ao reconectar o mesmo dongle, reaplica.

**Fora de escopo v1.2:** pareamento BT completo via UI; modem 4G/5G; Wi-Fi 6E; BLE Audio; keyring criptografado.

---

## 5. PORTABILIDADE

1. **DTB isolado:** pinagem e display no `.dtb` do SoC; Launcher só lê `/dev/input/event*`.
2. **APIs abertas:** Mesa (GLES) + ALSA. Pulse/PipeWire opcionais via Buildroot, nunca base.
3. **Arquitetura declarada:** `arch` no `metadata.json`; sem emulação transparente.
4. **Rede/BT:** via dongle USB (ADR-14). Nenhum SoC depende de rádio integrado.
5. **Aceleradores (NPU/VPU):** opcionais, específicos do SoC; o sistema base não depende deles.

---

## 6. FORA DE ESCOPO (v1.2)

- Vulkan no RK3326; Proton implementado; emulação x86/ARM transparente; tela de toque; desktop Wayland completo; Pulse/PipeWire na base; systemd; binários glibc pré-compilados (exceto ADR de exceção); pareamento BT completo via UI; modem 4G/5G; Wi-Fi 6E; keyring criptografado.

---

## 7. RISCOS

| Risco | Mitigação |
|---|---|
| Desgaste de SD por swap | Modo opcional + aviso + tamanho fixo |
| musl × Godot export template | Recompilação obrigatória (ADR-03) |
| A/B em 1 GB | Partições pequenas + assinatura antes de escrever |
| Gamescope sobre DRM sem shell | Testar backend DRM puro; fallback documentado |
| Rollback falhar | `bootcount` + partição A intacta |
| 🆕 Dongle não suportado | Lista de chipsets documentada; log; sistema segue sem rede |
| 🆕 Credenciais Wi-Fi em texto plano | Documentar; fase 2: keyring |
| 🆕 `dmix` falhar com dongle BT | Pausa da música ao iniciar jogo; retoma ao voltar |
| 🆕 Dois dongles Wi-Fi | Primeiro usado; segundo ignorado com log |

---

## 8. ORÇAMENTO DE BOOT (METAS)

| Métrica | RK3326 | RK3566 |
|---|---|---|
| Boot até Launcher | ≤ 8 s | ≤ 6 s |
| RAM repouso — Launcher | ≤ 60 MB | ≤ 60 MB |
| RAM repouso — Loja | ≤ 40 MB | ≤ 40 MB |
| RAM repouso — PortMaster | ≤ 30 MB | ≤ 30 MB |
| RAM — `giochi-musicd` | ≤ 8 MB | ≤ 8 MB |
| RAM — Wi-Fi ativo (dongle) | ≤ 6 MB | ≤ 6 MB |
| RAM — BT ativo (dongle) | ≤ 4 MB | ≤ 4 MB |
| RAM — ambos dongles ativos | ≤ 10 MB (adicional) | ≤ 10 MB (adicional) |
| VRAM UI — Launcher | ≤ 4 MB | ≤ 4 MB |
| VRAM — Loja | ≤ 8 MB | ≤ 8 MB |
| VRAM — PortMaster | ≤ 4 MB | ≤ 4 MB |
| VRAM — Música | ≤ 1 MB | ≤ 1 MB |
| VRAM — Configurações | ≤ 2 MB | ≤ 2 MB |
| SquashFS (System) | ≤ 150 MB | ≤ 150 MB |
| Módulos `.ko` (dongles) | ≤ 4 MB | ≤ 4 MB |
| Firmwares `/lib/firmware/` | ≤ 2 MB | ≤ 2 MB |
| Launcher → jogo | ≤ 500 ms | ≤ 300 ms |
| Jogo → Launcher | ≤ 1 s | ≤ 800 ms |
| Launcher → Loja | ≤ 800 ms | ≤ 800 ms |
| Launcher → PortMaster | ≤ 800 ms | ≤ 800 ms |
| Home → Música | ≤ 200 ms | ≤ 200 ms |
| Abrir Configurações | ≤ 100 ms | ≤ 100 ms |
| Detecção + `modprobe` dongle | ≤ 3 s | ≤ 3 s |

---

## 9. ENCERRAMENTO

Este documento (v1.2) é a especificação consolidada e **substitui integralmente a v1.1 e todas as revisões de seção posteriores**. Está pronto para gerar artefatos de build (defconfig Buildroot, fragmento de kernel, scripts de init, `giochi-init.c`, `giochi-musicd.c`, `giochi-forge`, layouts JSON, `device_profile.json`).

**Artefatos recomendados (não incluídos nesta spec):**
- `buildroot_defconfig` (RK3326/RK3566) com `BR2_PACKAGE_GIOCHI_PROTON=n`.
- Fragmento de `.config` do kernel (incluindo USB gadget + módulos de dongle).
- `giochi-init.c` (supervisor: Launcher, jogo, daemons, `control.sock`, `device_profile.json`).
- `giochi-musicd.c` (ALSA + libmpg123/libFLAC + socket).
- `giochi-launcher` (esqueleto SDL2 + motor de layout + overlay de Configurações).
- `giochi-forge` (CLI/GUI: detecção, classificação, compilação, assinatura).
- `S05update`, `S10storage`, `S30swap`, `S40dongle` (scripts de init).
- `layout.json` (schema 1.6) + `home.json`, `library.json`, `music.json`, `theme.json`, `defaults.json`.
- Esquema de chaves Ed25519 + pipeline de assinatura.

**Próximos passos sugeridos (quando decidir avançar para código):**
1. Definir a cor de acento do tema (A/B/C/D) → `theme.json` final.
2. Gerar `library.json` completo (grid 4×3) e `music.json` completo.
3. Esqueleto do `giochi-init.c` (máquina de estados de botões + `control.sock` + `device_profile.json`).
4. Esqueleto do `giochi-musicd.c` (ALSA + socket).
5. Script `S40dongle` (detecção + `modprobe`).
6. `buildroot_defconfig` inicial.

Por ora, a definição arquitetural da **v1.2** está consolidada: 5 abas, Configurações via Select, Ports, Música, `giochi-forge`, USB (MTP/Mass Storage), **dongles USB Wi-Fi/BT**, e Proton como porta aberta.