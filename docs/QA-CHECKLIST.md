# QA — Giochi Konsol OS v1.2 (S15)

Checklist por camada. Executar no dispositivo (RK3326 e RK3566), salvo onde
indicado "host". Referências: [architecture-v1.2.md](./architecture-v1.2.md) §8
(orçamento) e `Prompt.md` S15.

## Kernel (S1)

| # | Verificação | Comando | Esperado |
|---|---|---|---|
| K1 | zRAM presente | `modprobe zram; ls /sys/block/zram0` | `disksize`, `comp_algorithm` |
| K2 | SquashFS montado ro | `mount \| grep squashfs` | `ro,` na linha |
| K3 | evdev/gamepad | `ls /dev/input/event*` | ≥ 1 dispositivo |
| K4 | USB gadget | `ls /sys/kernel/config/usb_gadget` | `giochi` após S50usb |
| K5 | Módulos de dongle instalados, não carregados | `lsmod \| grep -E '8188eu\|btusb'` | vazio no boot |
| K6 | Panfrost ativo | `dmesg \| grep -i panfrost` | GPU inicializada |

## Init (S3)

| # | Verificação | Comando | Esperado |
|---|---|---|---|
| I1 | Ordem dos scripts | `dmesg \| grep 'start /etc/init.d'` | S05→S10→S20→S30→S40→S50→S99 |
| I2 | Sem systemd | `ls /etc/systemd` | não existe |
| I3 | Slot 2 exclusivo | `cat /run/giochi/slot2_mode` | `A` **ou** `B` (nunca ambos) |
| I4 | Prioridades de swap | `cat /proc/swaps` | zram 100, SD 10 |
| I4b | Tamanho do zRAM (§3.1) | `cat /sys/block/zram0/disksize` | 512 MB (1 GB RAM) / 1 GB (2 GB) |
| I4c | swappiness global | `cat /proc/sys/vm/swappiness` | 100 |
| I4d | RAID swap (§3.2) | `dmesg \| grep -i 'RAID swap\|swap single'` | `diferença ≤ 10%` ou single justificado |
| I5 | Update por SD | soltar `giochi_update.img` e reiniciar | imagem aplicada e apagada; assinatura inválida preserva arquivo |

## Supervisor (S4)

| # | Verificação | Comando | Esperado |
|---|---|---|---|
| S1 | `control.sock` | `printf 'PING\n' \| nc -U /run/giochi/control.sock` | `PONG` |
| S2 | `device_profile.json` no boot | `head -3 /user/.giochi/device_profile.json` | `schema_version 1.0`, SoC correto |
| S3 | Bloco proton | `grep -A2 '"proton"' /user/.giochi/device_profile.json` | `supported: false` + `reason` |
| S4 | Hotkey encerra jogo | Home+Start ≥ 500 ms durante um jogo | jogo termina, Launcher volta na Home |
| S5 | Daemon sobrevive ao jogo | `pidof giochi-musicd` antes/depois | mesmo PID |
| S6 | FIFO de transição | `ls -l /run/giochi/transition` | `prw-rw----` |
| S7 | Launcher morre ao lançar jogo | `pidof giochi-launcher` durante o jogo | vazio |
| S8 | Launcher volta após o jogo | `pidof giochi-launcher` na Home | novo PID |
| S9 | App externo suspende, não mata | `ps -o stat= -p $(pidof giochi-launcher)` durante Ports | `T` (stopped) e **mesmo** PID depois |
| S10 | Pacote extraído | `ls /run/giochi/*/bin/` após iniciar jogo | entrypoint presente |
| S11 | `buttons.map` ativo | `dmesg \| grep buttons.map` | `secao [x35s]` nas X35S/X35H |

## Launcher (S5/S6)

| # | Verificação | Esperado |
|---|---|---|
| L1 | Boot na Home | aba central ativa |
| L2 | L/R com wrap | Música→Biblioteca→Home→Loja→Ports→Música |
| L3 | B na Home | inerte (hint cinza) |
| L4 | B nas demais abas | volta para Home |
| L5 | Home curto (< 500 ms) | vai para Home |
| L6 | Home + Start (≥ 500 ms) | encerra o jogo |
| L7 | Select | abre/fecha Configurações em ≤ 100 ms / ≤ 50 ms |
| L8 | Fallback de layout | `layout.json` malformado → default compilado + log |
| L9 | Pacote rejeitado | `arch != aarch64` ou `proton` sem suporte → mensagem clara |
| L10 | VRAM | ≤ 4 MB na UI (medir com `drm_info`/estimativa do Launcher) |

## Música (S7)

| # | Verificação | Esperado |
|---|---|---|
| M1 | `PLAY`/`PAUSE` | JSON com `status` e `pos` |
| M2 | `NEXT`/`PREV` | faixa muda, `pos` = 0 |
| M3 | `SEEK 30` | `pos` = 30 (limitado à duração) |
| M4 | `/user/music/` vazio | hint de cópia via USB |
| M5 | Arquivo corrompido | pula faixa + log |
| M6 | `dmix` falha | pausa ao jogar, retoma ao voltar |
| M7 | RAM do daemon | ≤ 8 MB; CPU ≤ 3% (RK3326) / ≤ 2% (RK3566) |

## Ports / Loja (S5, §4.7)

| # | Verificação | Esperado |
|---|---|---|
| P1 | Launcher→PortMaster | ≤ 800 ms |
| P2 | PortMaster→Home | ≤ 1 s, Launcher reativado na Home |
| P3 | App ausente | "Ports indisponível" + hint B |

## USB gadget (S10)

| # | Verificação | Esperado |
|---|---|---|
| U1 | MTP padrão | Partição C visível no Windows/Linux (macOS: OpenMTP) |
| U2 | Troca em runtime | `USB_MODE MASS` → `OK usb_mode MASS` |
| U3 | Mass Storage | cartão exposto como bloco; C desmontada |
| U4 | Modo inválido | `USB_MODE XYZ` → `ERR invalid_mode` |
| U5 | Troca em runtime reanexa | `echo MASS` → `ls /sys/kernel/config/usb_gadget/giochi/UDC` | preenchido após a troca |
| U6 | Sem UDC (X35S/X35H) | log `nenhum UDC` e sistema segue | não fatal |
| U7 | MTP lista arquivos | PC abre "Giochi" e vê `games/`, `music/` | lista `/user` |
| U8 | Rede no mesmo gadget | `ip addr` no PC (estático `10.55.0.1/24`) vê o console | `usb0` no console = `10.55.0.2/24` |
| U8b | Modo Desenvolvedor desligado (padrão) | `settings.json` sem `developer` → console sem IP e sem `dropbear` | 0 MB em repouso |
| U8c | Ativar pelo menu | Configurações → Sistema → Modo Desenvolvedor → A | `OK dev_mode ON` no log |
| U9 | SSH por chave | `ssh root@10.55.0.2` com chave em `/user/ssh/authorized_keys` | login |
| U9b | SSH por senha | `GIOCHI_ROOT_PASSWORD` no build → `ssh root@10.55.0.2` pede senha | login |
| U10 | SSH não escuta fora do USB | `netstat -ltn \| grep :22` no console | só `10.55.0.2:22` |
| U11 | Sem chave/senha | não sobe dropbear mesmo com o modo ativo | log explicativo |
| U11b | Desativar o modo | `DEV_MODE OFF` → IP removido e `dropbear` encerrado | volta a 0 MB |
| U12 | Troca MTP→MASS | `pidof umtprd` some; `usb0` continua | rede permanece |
| U13 | Mass Storage desmonta /user | `mountpoint /user` = falso durante MASS | exigência do kernel |

## Dongles (S11)

| # | Cenário | Esperado |
|---|---|---|
| D1 | Nenhum dongle | `wlan0`/`hci0` ausentes, indicador cinza, sistema normal |
| D2 | Wi-Fi suportado | `modprobe` em ≤ 3 s, `wlan0` aparece |
| D3 | Wi-Fi sem rede | `wlan0` sobe, sem conexão, Configurações mostra estado |
| D4 | BT suportado | `hci0` aparece; `bluetoothd` só se habilitado |
| D5 | Chipset desconhecido | log + "Dongle não reconhecido" |
| D6 | Dois dongles Wi-Fi | primeiro usado, segundo ignorado com log |
| D7 | Orçamento | `.ko` ≤ 4 MB, firmware ≤ 2 MB, RAM Wi-Fi ≤ 6 MB, BT ≤ 4 MB |

## Update (S12)

| # | Verificação | Esperado |
|---|---|---|
| X1 | Assinatura válida | `giochi-verify` → `OK`, exit 0 |
| X2 | Imagem adulterada | SHA-256 divergente, exit 1 |
| X3 | Chave errada | assinatura inválida, exit 1 |
| X4 | Sem OpenSSL no binário | exit 3 e update recusado |
| X5 | 3 falhas de boot | rollback para slot anterior (`bootcount`) |

## Bootsplash

| # | Verificação | Comando | Esperado |
|---|---|---|---|
| B1 | Assets instalados | `ls /usr/share/giochi/bootlogo.*` | `.rgba` + `.json` |
| B2 | Splash aparece | ligar o aparelho | logo centralizado por ~0,75 s |
| B3 | Splash some no botão | pressionar A durante o splash | some imediatamente |
| B4 | Desligar splash | `GIOCHI_NO_SPLASH=1 giochi-launcher` | sem splash, UI normal |
| B5 | Ausência é inofensiva | renomear `bootlogo.rgba` | log em debug, boot normal |
| B6 | Splash do U-Boot (futuro) | `ls /boot/splash.bmp` + bloco ativo | logo antes do kernel |

## Memória e boot (S15)

| Métrica | Comando | RK3326 | RK3566 |
|---|---|---|---|
| Boot até Launcher | `tools/bench-boot.sh` | ≤ 8 s | ≤ 6 s |
| RAM em repouso | `free -m` | ≤ 60 MB | ≤ 60 MB |
| SquashFS | `df` / tamanho da imagem | ≤ 150 MB | ≤ 150 MB |
| VRAM do splash | tamanho de `bootlogo.rgba` | 893 KB | 893 KB |
| zRAM + swap | `tools/stress-mem.sh` | sem OOM | sem OOM |
| Daemons | `tools/stress-daemon.sh` | dentro do orçamento | dentro do orçamento |

## Matriz de testes

`RK3326 | RK3566` × `sem dongle | Wi-Fi | BT | ambos` × `slot 1 | slot 1+2A | slot 1+2B` × `MTP | Mass Storage`

Host (sem hardware): `make test` cobre JSON, SHA-256, hotkey, musicd,
transição, layout, metadata, settings e giochi-forge.
