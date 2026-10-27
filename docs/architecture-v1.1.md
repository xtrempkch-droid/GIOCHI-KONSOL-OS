# 📑 DOCUMENTO DE ARQUITETURA TÉCNICA: GIOCHI KONSOL OS — v1.1

> ⚠️ **Documento histórico — não é a especificação vigente.** A versão
> normativa é a [v1.2](./architecture-v1.2.md), que substitui integralmente esta
> e todas as revisões de seção posteriores. Este arquivo é mantido como registro
> do que foi decidido antes dos adendos (SSH via USB, dongles, Proton etc.).
> Ele chegou a ser o `README.md` do repositório; quando o README virou um README
> de verdade, a especificação foi movida para `docs/`.

**Tipo de Documento:** Especificação de Requisitos e Engenharia de Sistemas Embarcados
**Versão:** 1.1 (consolidada — substitui integralmente a v1.0)
**Alvo Principal:** Dispositivos Portáteis ARM (Foco Inicial: Rockchip RK3326 / RK3566)
**Restrições de Hardware:** Sem tela de toque, entrada por botões GPIO/Gamepad genérico, 1 GB a 2 GB de RAM, armazenamento via Single ou Dual MicroSD.

**Changelog v1.0 → v1.1 (resumo):**
- Init system fixado em BusyBox `init` + U-Boot (removida menção a `systemd-boot`).
- Decisão explícita sobre musl-libc e recompilação obrigatória de runtime.
- Layout de partições por slot SD desambiguado (swap **ou** user data).
- Corrigida contradição "elimina Wayland" vs. uso de Gamescope.
- Versão do runtime Godot e API gráfica fixadas por SoC.
- Fluxo de ciclo de vida do processo redefinido com supervisor `giochi-init`.
- `vm.swappiness` corrigido (global + prioridade `swapon -p`).
- Script de RAID Swap reescrito e funcional (sem `bc`, teste 4K random).
- Formato `.giochi` definido sem contradição de compressão.
- Update com assinatura Ed25519 + A/B atômico + rollback.
- Linguagem do Launcher fixada em C++17 + SDL2.
- Áudio fixado em ALSA direto (Pulse/PipeWire opcionais).
- Portabilidade esclarecida (recompilação obrigatória, `arch` no metadata).
- Adicionadas seções **Decisões Fixas (ADR)**, **Fora de Escopo**, **Riscos** e **Orçamento de Boot**.

---

## 0. DECISÕES FIXAS (ADR — Architecture Decision Records)

Estas decisões são **imutáveis** para a v1.x. Alterações exigem nova versão maior.

| ID | Decisão | Justificativa |
|---|---|---|
| ADR-01 | **Bootloader:** U-Boot (Rockchip fork oficial) | Suporte nativo A/B, `bootcount`, `altboot`. |
| ADR-02 | **Init System:** BusyBox `init` via `/etc/inittab` | Boot rápido, footprint mínimo. |
| ADR-03 | **Libc:** musl-libc, **com recompilação obrigatória** de todos os binários de runtime a partir do fonte (Godot export template incluído) | Redução de tamanho/RAM. Se inviável para algum componente, avaliar glibc enxuta **somente** para aquele componente, documentado em ADR de exceção. |
| ADR-04 | **Compositor:** Gamescope (Wayland + backend DRM/KMS, sem XWayland) | Fullscreen único, baixo overhead. |
| ADR-05 | **Driver gráfico:** Mesa 3D com Panfrost (RK3566) ou Lima/Panfrost (RK3326, conforme GPU) | Pilha 100% open source. |
| ADR-06 | **API gráfica:** OpenGL ES 3.0 (mínimo); ES 2.0 como fallback declarado no `metadata.json`. **Vulkan não suportado** no RK3326. | Compatibilidade com Mali-G31/G52. |
| ADR-07 | **Runtime Godot:** Godot 3.5 LTS (RK3326) / Godot 4.x com renderizador **Compatibility (GLES3)** (RK3566) | Evitar exigência de Vulkan. |
| ADR-08 | **Áudio:** ALSA direto (`libasound`). PulseAudio/PipeWire **não** na imagem base. | Footprint; jogos usam ALSA nativo. |
| ADR-09 | **Launcher:** C++17 + SDL2, desalocação explícita (sem GC). | Controle de memória determinístico. |
| ADR-10 | **Supervisor:** `giochi-init` (C puro) como PID pai do Launcher e dos jogos. | Transição de contexto confiável. |
| ADR-11 | **Formato de pacote:** `.giochi` = `tar` puro; metadados opcionalmente em `zstd -1`. `capa.png` e `bin/` **sem recompressão**. | Streaming sem extrair tudo para disco. |
| ADR-12 | **Update:** A/B atômico + assinatura Ed25519 + SHA-256 + rollback via U-Boot `bootcount`. | Integridade e anti-brick. |
| ADR-13 | **Arquitetura:** aarch64 (arm64) como alvo canônico; `metadata.json` declara `arch`; Launcher rejeita pacotes incompatíveis. | Portabilidade explícita, sem emulação. |

---

## 1. CAMADA 1 — CORE DO SISTEMA OPERACIONAL (FIRMWARE & KERNEL)

**Metas mensuráveis:** imagem SquashFS ≤ 150 MB; RAM em repouso (Launcher visível, sem jogo) ≤ 60 MB; tempo até Launcher ≤ 8 s no RK3326.

### 1.1 Toolchain e Bibliotecas Base

- **Compilador:** GCC otimizado para o alvo.
  - RK3326: `-march=armv8-a+crc -mtune=cortex-a35 -O3`
  - RK3566: `-march=armv8-a+crc+crypto -mtune=cortex-a55 -O3`
- **Libc:** musl-libc (ver **ADR-03**). Todos os binários de runtime — incluindo o export template do Godot e o Gamescope — **devem ser recompilados a partir do fonte** dentro do Buildroot. Nenhum binário glibc pré-compilado entra na imagem.
- **Init System:** BusyBox `init` (ver **ADR-02**). Serviços gerenciados por scripts SysV-like em `/etc/init.d/` chamados pelo `inittab`. **Não há systemd.**
- **Bootloader:** U-Boot (ver **ADR-01**), configurado com suporte a A/B e `bootcount`.

### 1.2 Customização do Kernel Linux

Kernel compilado com módulos e drivers redundantes desativados.

**Removidos:**
- Drivers gráficos de placas de PC (NVIDIA/AMD).
- Suporte a mídias ópticas (`CONFIG_BLK_DEV_SR`, `CONFIG_ISO9660_FS`).
- Drivers PCI/PCIe genéricos (exceto os exigidos pelo SoC).
- Drivers de rede com fio de alta capacidade (manter apenas PHY do SoC, se necessário).
- Subsistemas de desktop clássicos (input de mouse/teclado genéricos mantidos apenas para `evdev`).

**Obrigatórios:**
- `CONFIG_TMPFS=y` (RAMDisk)
- `CONFIG_ZRAM=y` (com `CONFIG_ZRAM_DEF_COMP_LZ4` para RK3326)
- `CONFIG_SQUASHFS=y`
- `CONFIG_INPUT_EVDEV=y`
- `CONFIG_JOYSTICK=y` (+ `CONFIG_JOYSTICK_XPAD` se gamepad USB for suportado)
- `CONFIG_SWAP=y`

**Logs:**
- `CONFIG_PRINTK=y` em builds de desenvolvimento.
- Em produção, avaliar `CONFIG_PRINTK=n` **somente após** validação de que nenhum subsistema depende de logs em runtime. `CONFIG_DYNAMIC_DEBUG` desativado em produção.

### 1.3 Arquitetura de Imutabilidade e Layout de Partições

RootFS imutável. **Layout por slot SD é explícito e mutuamente exclusivo** (ver correção da v1.0).

**Slot 1 (obrigatório):**
| Partição | Conteúdo | FS | Modo |
|---|---|---|---|
| A (Boot) | U-Boot, `boot.img` (Kernel + Initramfs), env U-Boot | raw / FAT | rw (bootloader) |
| B1 (System) | SquashFS (sistema base, SDL2, Mesa, Gamescope, Launcher) | SquashFS | ro |
| B2 (System — A/B) | Cópia inativa para update atômico | SquashFS | ro |
| C (User Data) | `/user/saves/`, `/user/jogos/`, configs de rede | ext4 ou F2FS | rw |

**Slot 2 (opcional) — modo exclusivo, escolhido no boot:**
- **Modo A — Swap Dedicado:** partição única contendo `swapfile` para o Parallel RAID Swap (Camada 3.2). **Não** contém `/user/`.
- **Modo B — User Data Estendido:** partição ext4/F2FS montada como `/user2/`. **Não** contém swap.

**Regra de boot:** o script `/etc/init.d/S10storage` detecta o conteúdo do Slot 2 e escolhe o modo. Nunca ambos no mesmo dispositivo.

---

## 2. CAMADA 2 — SUBSISTEMA GRÁFICO E EXECUÇÃO DE JOGOS

### 2.1 Servidor Gráfico e Compositor

- **Compositor:** Gamescope (ver **ADR-04**). Executa diretamente sobre DRM/KMS (backend DRM), **sem** XWayland, **sem** shell Wayland intermediário (GNOME/KDE/Weston ausentes).
- **Fluxo:** Kernel → U-Boot → BusyBox init → `giochi-init` (supervisor) → Gamescope (fullscreen, resolução nativa do display, tipicamente 640×480@60 Hz) → Launcher ou jogo.
- **Drivers:** Mesa 3D com Panfrost (RK3566, Mali-G52) ou Lima/Panfrost (RK3326, conforme GPU).
- **API exposta:** OpenGL ES 3.0 mínimo; ES 2.0 como fallback declarado (ver **ADR-06**). **Vulkan não suportado** no RK3326.

### 2.2 Ciclo de Vida do Processo (Supervisor `giochi-init`)

Fluxo revisado — o Launcher **não se auto-mata** (correção da v1.0).

1. `giochi-init` (C puro, PID pai) inicia o Launcher via `fork()` + `execve()`.
2. Usuário navega no Launcher e seleciona um jogo `.giochi`.
3. O Launcher envia **IPC** ao supervisor via pipe nomeado `/run/giochi/transition` (mensagem: caminho do pacote + `metadata.json`).
4. `giochi-init` envia `SIGTERM` ao Launcher e aguarda `waitpid()`.
5. `giochi-init` faz `fork()` + `execve()` do binário do jogo (extraído em streaming do `.giochi` para `/run/giochi/`).
6. Durante o jogo, `giochi-init` lê `/dev/input/event*` (via `evdev`) e monitora a combinação **Hotkey (BTN_MODE) + Start (BTN_START)** mantida por ≥ 500 ms.
7. Ao detectar a combinação ou o término natural do jogo (`waitpid`), `giochi-init` envia `SIGTERM` ao PID do jogo; se não encerrar em 3 s, `SIGKILL`.
8. `giochi-init` reinicia o Launcher.

**Mapeamento de botões:** canônico em `/etc/giochi/buttons.map`, referenciado pelo `metadata.json` do pacote. O código do Launcher **nunca** acessa pinos de hardware diretamente — apenas `/dev/input/event*`.

---

## 3. CAMADA 3 — ENGENHARIA DE MEMÓRIA

### 3.1 Primeira Linha: zRAM no Host

- **Alocação:** 512 MB de zRAM em sistemas de 1 GB; 1 GB em sistemas de 2 GB.
- **Compressão:** LZ4 (mandatório no RK3326); ZSTD aceitável no RK3566.
- **Prioridade:** zRAM com `swapon -p 100`; swapfiles em SD com `swapon -p 10`. O kernel prefere zRAM e só recorre ao SD sob pressão.
- **`vm.swappiness`:** é **global** (`vm.swappiness=100`). A priorização zRAM-sobre-SD é feita **exclusivamente via prioridade de swap** (`-p`), não via swappiness. (Alternativa documentada: `vm.swappiness=180` + zRAM `-p 100`.)

### 3.2 Segunda Linha: Parallel RAID Swap (Dual Slot SD) — **Modo Opcional**

Ativado **somente** se o Slot 2 estiver no **Modo A (Swap Dedicado)** e o usuário habilitar em `settings`. Aviso de desgaste de SD é exibido.

**Parâmetros fixos:**
- Tamanho do `swapfile`: 256 MB (mínimo) ou 512 MB (recomendado em 2 GB).
- Algoritmo de validação: throughput **random 4K** (representativo de swap), não sequencial 1M.
- Diferença aceitável para RAID: ≤ 10%.

**Script revisado (`/etc/init.d/S30swap`):**

```bash
#!/bin/sh
# Requer: busybox (dd, awk, df), sem bc. Usa awk para aritmética.
SWAP_SIZE_MB=256
SD1=/mnt/sd1/swapfile
SD2=/mnt/sd2/swapfile

# 1. Verifica existência e espaço
for dev in /mnt/sd1 /mnt/sd2; do
    [ -d "$dev" ] || { echo "SKIP: $dev ausente"; continue; }
    free_mb=$(df -m "$dev" | awk 'NR==2 {print $4}')
    [ "$free_mb" -lt $((SWAP_SIZE_MB + 50)) ] && echo "SKIP: $dev sem espaço" && continue
done

# 2. Teste de throughput random 4K
bench() {
    dev="$1"
    start=$(date +%s%N)
    i=0
    while [ $i -lt 50 ]; do
        dd if=/dev/urandom of="$dev/.bench" bs=4k count=1 conv=fsync 2>/dev/null
        i=$((i+1))
    done
    end=$(date +%s%N)
    rm -f "$dev/.bench"
    echo $(( (end - start) / 1000000 ))
}

t1=$(bench /mnt/sd1 2>/dev/null || echo 999999)
t2=$(bench /mnt/sd2 2>/dev/null || echo 999999)

# 3. Compara (menor tempo = mais rápido)
diff_pct=$(awk -v a="$t1" -v b="$t2" 'BEGIN {
    if (a==0 || b==0) { print 100; exit }
    d = (a > b) ? (a-b)/a : (b-a)/b
    printf "%.2f", d*100
}')

if awk -v d="$diff_pct" 'BEGIN { exit !(d <= 10.0) }'; then
    echo "RAID swap ativado (diferença ${diff_pct}%)"
    [ -f "$SD1" ] && swapon -p 10 "$SD1"
    [ -f "$SD2" ] && swapon -p 10 "$SD2"
else
    echo "Swap single (diferença ${diff_pct}%)"
    if [ "$t1" -le "$t2" ]; then
        [ -f "$SD1" ] && swapon -p 10 "$SD1"
    else
        [ -f "$SD2" ] && swapon -p 10 "$SD2"
    fi
fi
```

**Riscos documentados:** swap em SD reduz vida útil do cartão; modo opcional, com aviso ao usuário.

---

## 4. CAMADA 4 — DISTRIBUIÇÃO, ATUALIZAÇÕES E PROTOCOLO `.giochi`

### 4.1 Interface Gráfica do Menu Principal (Giochi Launcher)

- **Linguagem:** C++17 + SDL2 (ver **ADR-09**). Desalocação **explícita** via `SDL_DestroyTexture` + `free()`. Sem GC.
- **Layout:** lista vertical de texto à esquerda (60% da largura); container de imagem estática à direita (40%).
- **Carregamento de ativos:** apenas strings de nomes de jogos em memória; `capa.png` carregada na VRAM **somente** quando o cursor repousa sobre o item; ao mover o cursor, a textura anterior é imediatamente destruída (`SDL_DestroyTexture`).

### 4.2 Estrutura do Pacote `.giochi`

Formato: `tar` puro; metadados opcionalmente em `zstd -1`; `capa.png` e `bin/` **sem recompressão** (ver **ADR-11**). O runtime lê o `tar` em **streaming** para `/run/giochi/`.

```
meujogo.giochi/
├── metadata.json       # título, desenvolvedor, versão, arch, api_grafica, mapeamento de botões
├── capa.png            # 320x320 px, sem recompressão
└── bin/
    └── jogo.arm64      # executável nativo (Godot Export, recompilado contra musl)
```

**Exemplo de `metadata.json`:**
```json
{
  "title": "Meu Jogo",
  "developer": "Studio X",
  "version": "1.0.0",
  "arch": "aarch64",
  "graphics_api": "gles3",
  "graphics_api_fallback": "gles2",
  "buttons_map": "default",
  "entrypoint": "bin/jogo.arm64"
}
```

O Launcher **rejeita** pacotes com `arch` incompatível com o alvo, exibindo mensagem clara.

### 4.3 Sistema de Atualizações (A/B + Assinatura)

- **Sem daemons de escuta** ou cronjobs de rede. Verificação **manual** pelo menu.
- Ao acionar: levanta `wlan0`, faz `HTTP GET` em API REST (JSON de versão), compara versão local, **baixa** a imagem se houver update, **verifica assinatura Ed25519** (chave pública read-only na Partição B) e **hash SHA-256** (publicado no JSON), e **derruba** `wlan0` imediatamente após a resposta.
- **Update atômico A/B:** escreve na partição **inativa** (B2 se B1 ativa, e vice-versa). Marca a nova como ativa via U-Boot env (`bootcount`/`altboot`). Reinicia. Se o boot falhar 3 vezes, U-Boot faz **rollback** automático para a partição anterior.
- **Update por SD:** no boot, `/etc/init.d/S05update` verifica `giochi_update.img` na Partição C. Se presente e assinatura válida, aplica via A/B e **apaga** o arquivo após sucesso. Assinatura inválida → atualização abortada, arquivo preservado para diagnóstico.

---

## 5. REQUISITOS DE PORTABILIDADE (ABSTRAÇÃO MULTI-CHIP)

Regras imutáveis para portar o Giochi OS a outros SoCs (ARM ou x86):

1. **Isolamento de Device Tree (DTB):** toda pinagem de botões e controladores de display fica **exclusivamente** no `.dtb` do SoC alvo. O Launcher **nunca** faz chamadas diretas a pinos; lê apenas `/dev/input/event*`.
2. **APIs abertas exclusivas:** nenhum jogo ou componente usa extensões proprietárias de drivers gráficos ou de áudio. Conformidade obrigatória com **Mesa 3D (OpenGL ES)** e **ALSA** (ver **ADR-08**). PulseAudio/PipeWire são camadas **opcionais** via Buildroot, nunca base.
3. **Arquitetura declarada:** portabilidade significa **recompilar** o SO e os jogos para o alvo. O `.giochi` declara `arch` em `metadata.json`; não há emulação transparente (ver **ADR-13**).

---

## 6. FORA DE ESCOPO (v1.x)

- Vulkan no RK3326 (GPU Mali-G31 não suporta de forma viável).
- Emulação transparente x86/ARM (sem QEMU/Box86 na base).
- Suporte a tela de toque.
- Desktop Wayland completo (GNOME/KDE/Weston).
- PulseAudio/PipeWire na imagem base.
- systemd como init.
- Binários glibc pré-compilados na imagem (exceto ADR de exceção documentada).

---

## 7. RISCOS CONHECIDOS

| Risco | Mitigação |
|---|---|
| Desgaste de SD por swap | Modo opcional, aviso ao usuário, tamanho fixo de swapfile. |
| Incompatibilidade musl × Godot export template | Recompilação obrigatória (ADR-03); ADR de exceção se inviável. |
| Complexidade A/B em 1 GB | Partições B1/B2 pequenas (SquashFS comprimida); validação de assinatura antes de escrever. |
| Gamescope sobre DRM sem shell | Testar backend DRM puro em RK3326/RK3566; fallback documentado se necessário. |
| Rollback falhar | `bootcount` no U-Boot + partição A sempre intacta. |
| Diferença de throughput entre SDs mascarar gargalo | Teste 4K random (não sequencial) + limite de 10%. |

---

## 8. ORÇAMENTO DE BOOT (METAS MENSURÁVEIS)

| Métrica | Meta RK3326 | Meta RK3566 |
|---|---|---|
| Tempo até Launcher visível | ≤ 8 s | ≤ 6 s |
| RAM em repouso (Launcher) | ≤ 60 MB | ≤ 60 MB |
| Tamanho SquashFS (System) | ≤ 150 MB | ≤ 150 MB |
| Tempo de transição Launcher → jogo | ≤ 500 ms | ≤ 300 ms |
| Tempo de transição jogo → Launcher | ≤ 1 s | ≤ 800 ms |

---

## 9. ENCERRAMENTO

Este documento (v1.1) é a especificação conceitual completa e **substitui integralmente a v1.0**. Está pronto para ser interpretado por qualquer equipe de engenharia de sistemas embarcados ou ambiente de IA para gerar artefatos de build (defconfig Buildroot, fragmento de kernel, scripts de init/swap, exemplos de `metadata.json`).

**Próximos artefatos recomendados (não incluídos nesta spec):**
- `buildroot_defconfig` para RK3326 e RK3566.
- Fragmento de `.config` do kernel.
- `giochi-init.c` (supervisor) e `giochi-launcher` (esqueleto SDL2).
- `S05update`, `S10storage`, `S30swap` (scripts de init).
- Esquema de chaves Ed25519 e pipeline de assinatura de firmware.

---

Se desejar, posso agora gerar qualquer um dos **artefatos de build** listados na seção 9 (por exemplo, o `buildroot_defconfig` inicial ou o esqueleto do `giochi-init.c`).