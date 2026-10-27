# 📦 Lista Consolidada de Aplicativos do Giochi Konsol OS (v1.2)

Abaixo estão todos os apps definidos na especificação, separados por **onde rodam** (dispositivo vs. PC) e por **tipo** (binário, daemon, layout, script). A lista reflete a v1.2 consolidada.

---

## 🖥️ Aplicativos que rodam **no dispositivo** (console)

### 1. `giochi-init` — Supervisor do sistema
- **Tipo:** Daemon em C puro (PID pai do userland, após BusyBox `init`).
- **Função:** Gerencia o ciclo de vida de todos os processos (Launcher, jogos, apps externos, daemons persistentes). Implementa o socket de controle (`/run/giochi/control.sock`), gera o `device_profile.json` no boot, monitora botões (`evdev`) para Hotkey+Start, e executa ações de energia/rede/USB.
- **Seção da spec:** 2.2, 4.5.1, 4.6, 4.9.4.

### 2. `giochi-launcher` — Interface principal (Launcher)
- **Tipo:** Binário C++17 + SDL2.
- **Função:** Renderiza as 5 abas (Música, Biblioteca, Home, Loja, Ports), o grid estilo YouTube, o tema dark, o overlay de Configurações e o layout da aba Música. Interpreta `layout.json`, `theme.json`, `home.json`, `library.json`, `music.json`. Não tem GC; desalocação explícita.
- **Seção da spec:** 4.1, 4.4, 4.9.
- **Subcomponentes internos (não são apps separados):**
  - Motor de layout (parse de JSON + validação + fallback).
  - Cache LRU de thumbnails (≤ 24 texturas, ≤ 4 MB VRAM).
  - Cliente do socket do `giochi-musicd` (`/run/giochi/music.sock`).
  - Cliente do socket de controle (`/run/giochi/control.sock`).
  - Overlay de Configurações (Select).

### 3. `giochi-musicd` — Daemon de música
- **Tipo:** Daemon em C puro (ALSA + libmpg123 + libFLAC + libvorbis).
- **Função:** Decodifica e reproduz MP3/FLAC/OGG de `/user/music/`; expõe socket `/run/giochi/music.sock` para controle; sobrevive ao lançamento de jogos (daemon persistente); coexiste com áudio de jogos via ALSA `dmix`.
- **Seção da spec:** 4.8.

### 4. `giochi-store` — App da Loja
- **Tipo:** App externo (binário próprio, lançado via `giochi-init` como `APP:giochi-store`).
- **Função:** Catálogo curado de pacotes `.giochi` assinados; download e instalação em `/user/jogos/`; verificação de assinatura Ed25519 + `arch`. Fase 2: integração com API do itch.io (metadados + downloads autenticados). Fase 3: integração com Godot Asset Library (listagem/download de projetos, sem execução direta).
- **Seção da spec:** 4.1.3, 4.5 (contexto), ADR-11, ADR-12.
- **Orçamento:** RAM ≤ 40 MB; VRAM ≤ 8 MB.

### 5. `portmaster` — App de Ports (PortMaster)
- **Tipo:** App externo (lançado via `giochi-init` como `APP:portmaster`).
- **Função:** Download/instalação de ports de jogos de PC (wrappers ARM64) em `/user/ports/`. Não distribui arquivos originais dos jogos (usuário fornece). Interface própria; herda `theme.json`.
- **Seção da spec:** 4.7.
- **Orçamento:** RAM ≤ 30 MB; VRAM ≤ 4 MB.

### 6. `giochi-thumbgen` — Gerador de thumbnails
- **Tipo:** Utilitário em C (executado no install de um `.giochi`).
- **Função:** Gera `/user/.cache/thumbs/<hash>.rgba` (152×85 RGBA, ~51 KB) a partir de `thumb.png` (se existir) ou center-crop 16:9 do `capa.png`. Fallback: placeholder compilado.
- **Seção da spec:** 4.2 (pré-geração de thumbnails).

### 7. `dropbear` — Servidor SSH (Modo Desenvolvedor)
- **Tipo:** Daemon SSH leve (Dropbear).
- **Função:** Escuta **apenas** em `usb0` (IP `10.55.0.2/24`). Ativado manualmente via Configurações → Sistema → Modo Desenvolvedor. Desativado por padrão. Usado pelo `giochi-forge` para transferência/depuração via SSH sobre USB.
- **Seção da spec:** 4.6.1 (adendo), 4.5.4 (integração com `giochi-forge`).

### 8. Scripts de init (`/etc/init.d/`)
- **Tipo:** Shell scripts (BusyBox `sh`).
- **Função:**
  - `S05update` — verifica `giochi_update.img` na Partição C no boot; aplica A/B se assinatura válida.
  - `S10storage` — detecta o modo do Slot 2 (A = swap dedicado / B = user data estendido).
  - `S30swap` — configura zRAM + Parallel RAID Swap (com `SWAP_MODE`).
  - `S40dongle` — detecta dongles USB Wi-Fi/BT e faz `modprobe` sob demanda.
- **Seção da spec:** 1.3, 3.1, 3.2, 4.3, 1.4.

### 9. RetroArch (fase 2)
- **Tipo:** App externo (frontend libretro) — **não implementado na v1.2**, previsto para fase 2.
- **Função:** Emulação via libretro cores; lançado por `giochi-init`; pacotes `.giochi` com `runtime: "retroarch"` (fase 2).
- **Seção da spec:** 4.2 (`runtime`), ADR-06, ADR-07 (contexto), discussão de emulação.
- **Status:** fora do escopo v1.2; reservado.

### 10. Proton (stub, desativado)
- **Tipo:** Stub arquitetural (`#ifdef GIOCHI_ENABLE_PROTON`, padrão `n`).
- **Função:** Nenhuma na v1.2. Custo zero. Ativação futura em SoC com Vulkan ≥ 1.1 maduro + RAM ≥ 4 GB + CPU ≥ Cortex-A76.
- **Seção da spec:** 4.10.

---

## 💻 Aplicativos que rodam **no PC**

### 11. `giochi-forge` — App de compilação e transferência
- **Tipo:** Aplicativo desktop (CLI ou GUI leve), multiplataforma (Windows/Linux/macOS).
- **Função:**
  - Detectar o console via USB (MTP/Mass Storage) e ler `device_profile.json`.
  - Comparar requisitos do jogo (`forge.json` recomendado, ou inferência do `project.godot`) com o perfil do dispositivo.
  - Classificar: **Compatível** / **Parcial** (aviso) / **Incompatível** (bloqueia).
  - Compilar projeto Godot → Linux ARM64 (export template) → empacotar `.giochi` → assinar (Ed25519).
  - Transferir o `.giochi` para o console via MTP/Mass Storage **ou** via SSH sobre USB (`scp` para `/user/jogos/`), usando o cliente SSH do sistema (OpenSSH no Linux/macOS; `plink`/`pscp` no Windows como fallback).
  - Gerar/logar mensagens claras de compatibilidade (todos os requisitos não atendidos listados).
  - Fallback sem USB: perfil manual (RK3326 1/2 GB, RK3566 2 GB), importar `device_profile.json`, ou modo avançado (sem filtro, com aviso).
- **Seção da spec:** 4.5, 4.5.1, 4.5.2, 4.5.3, 4.5.4.
- **Não embute biblioteca SSH:** usa cliente do sistema (mantém binário leve).

---

## 🧩 Artefatos de configuração (não são apps, mas fazem parte da entrega)

| Artefato | Onde fica | Função |
|---|---|---|
| `layout.json` (schema 1.6) | SquashFS (`/etc/giochi/layouts/default/`) | Layout principal (abas, ordem visual, botões) |
| `home.json` | SquashFS | Layout da aba Home |
| `library.json` | SquashFS | Layout da aba Biblioteca (grid 4×3) |
| `music.json` | SquashFS | Layout da aba Música (Apple Music adaptado) |
| `theme.json` | SquashFS | Tema dark (cores, fontes, espaçamentos) |
| `defaults.json` | SquashFS | Configurações padrão (fallback de `settings.json`) |
| `settings.json` | `/user/` (rw) | Configurações do usuário (persistidas) |
| `device_profile.json` | `/user/.giochi/` (rw, gerado no boot) | Perfil do dispositivo lido pelo `giochi-forge` |
| `buttons.map` | SquashFS (`/etc/giochi/`) | Mapeamento canônico de botões |
| `news.json` | `/user/.cache/` | Cache de novidades da Home (atualizado pela Loja) |
| `forge.json` | Raiz do projeto Godot (opcional) | Requisitos declarativos do jogo (lido pelo `giochi-forge`) |
| `metadata.json` | Dentro do `.giochi` | Metadados do pacote (título, `arch`, `runtime`, etc.) |

---

## 📊 Resumo por localização

### No dispositivo (console)
| # | App | Tipo | Seção |
|---|---|---|---|
| 1 | `giochi-init` | Daemon (C) | 2.2, 4.5.1, 4.6, 4.9.4 |
| 2 | `giochi-launcher` | Binário (C++17 + SDL2) | 4.1, 4.4, 4.9 |
| 3 | `giochi-musicd` | Daemon (C + ALSA) | 4.8 |
| 4 | `giochi-store` | App externo | 4.1.3, ADR-11/12 |
| 5 | `portmaster` | App externo | 4.7 |
| 6 | `giochi-thumbgen` | Utilitário (C) | 4.2 |
| 7 | `dropbear` | Daemon SSH (opcional) | 4.6.1, 4.5.4 |
| 8 | Scripts `S05/S10/S30/S40` | Shell (BusyBox) | 1.3, 3.1, 3.2, 4.3, 1.4 |
| 9 | RetroArch (fase 2) | App externo (reservado) | 4.2 (contexto) |
| 10 | Proton (stub) | `#ifdef` desativado | 4.10 |

### No PC
| # | App | Tipo | Seção |
|---|---|---|---|
| 11 | `giochi-forge` | Desktop (CLI/GUI, multiplataforma) | 4.5 |

### Configuração (compartilhada)
- Layouts JSON, tema, `settings.json`, `device_profile.json`, `forge.json`, `metadata.json`, `buttons.map`, `news.json`.

---

## 🔗 Como os apps se relacionam (fluxo resumido)

1. **Boot:** Kernel → U-Boot → BusyBox `init` → `giochi-init` → Gamescope → `giochi-launcher` (Home).
2. **Daemons persistentes:** `giochi-init` inicia `giochi-musicd`; `wpa_supplicant`/`bluetoothd` só sobem se dongle presente; `dropbear` só sobe se Modo Desenvolvedor ativo.
3. **Usuário navega:** Launcher (5 abas) → seleciona jogo `.giochi` → `giochi-init` mata Launcher, lança jogo → ao sair, Launcher reinicia.
4. **Apps externos:** Loja (`giochi-store`) e Ports (`portmaster`) são lançados pelo `giochi-init`; Launcher fica suspenso; B/Home encerram e reativam Launcher na Home.
5. **PC:** `giochi-forge` detecta dispositivo (via MTP/Mass Storage ou SSH sobre USB), lê `device_profile.json`, classifica compatibilidade, compila Godot → `.giochi` assinado → transfere (MTP/Mass Storage ou `scp`).
6. **Update:** `S05update` verifica `giochi_update.img` no boot; A/B + assinatura + rollback.
7. **Dongles:** `S40dongle` faz `modprobe` sob demanda; Configurações → Rede gerencia `wlan0`/`hci0`.

---
