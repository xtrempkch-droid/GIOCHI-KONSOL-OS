# Diagnóstico de boot — quando a tela não acende

Este documento é o procedimento para descobrir **onde** o boot parou quando o
aparelho liga e nada aparece. Ele vale para a v1.2 e não depende de instrumento
além de um PC e um leitor de cartão.

> **Ordem de valor:** o log no cartão é o que responde "chegou a rodar alguma
> coisa?"; o `fbcon` responde "onde o kernel parou"; o serial responde "o que
> veio antes do kernel". Comece pelo primeiro, que é grátis.

---

## 1. O log vai para o cartão, na partição FAT

`S00bootlog` é o **primeiro** script de `rcS`. Ele monta a partição 1 (FAT,
rótulo `GIOCHI_BOOT`) em `/boot` e grava:

| Arquivo (na partição 1) | O que é |
|---|---|
| `giochi-boot.log` | Log do boot **atual**: cmdline, kernel, partições, `dmesg` até o userspace, e toda a saída dos serviços e do Launcher |
| `giochi-boot.1.log` | Log do boot **anterior** (é este que interessa depois de um boot que falhou) |
| `giochi-boot.dmesg` | Cópia crua do `dmesg` do boot atual |
| `giochi-boot.inprogress` | Existe enquanto o boot não chega ao Launcher. Se ainda estiver lá no próximo boot, o anterior **não terminou** — e o `.1.log` diz até onde foi |

### Como coletar

1. Desligue o aparelho **pelo botão de energia** (desligamento normal grava o
   log; puxar o cartão com o sistema ligado pode perder as últimas linhas).
2. Tire o cartão e ponha num PC. Monte a **primeira** partição (FAT32,
   `GIOCHI_BOOT`). No Windows ela aparece como unidade; no Linux:
   `sudo mount /dev/sdX1 /mnt && cat /mnt/giochi-boot.1.log`
3. Se houver `giochi-boot.inprogress`, o boot anterior morreu no meio — as
   últimas linhas do `.1.log` são o ponto exato.

### O que o log NÃO cobre

O log começa quando o `rcS` roda, ou seja, **depois** de o kernel montar a raiz
SquashFS. Se a imagem não boota por causa do bootloader, do DTB, do painel ou
de um pânico de kernel, nada é gravado. Para esses casos, use as seções 2 e 3.

---

## 2. Imagem de diagnóstico: mensagens do kernel na tela

A imagem normal tem `CONFIG_FB=n` (decisão registrada em
`buildroot/board/giochi/linux.fragment`: economiza ~1 MB de kernel e alguns ms
de boot). Sem console de vídeo, **toda** mensagem do kernel fica invisível.

Para gerar uma imagem em que o kernel escreve no painel:

```sh
GIOCHI_DEBUG_BOOT=1 tools/docker-build.sh x55      # ou:
tools/build-image.sh x55 --debug
```

- A saída vai para **`dist/x55-debug/`**, separada de propósito, para não
  gravar o cartão com a imagem errada.
- O que muda: `buildroot/board/giochi/linux-debug.fragment` é anexado à lista
  de fragmentos do defconfig efetivo (a lista é *derivada*, não repetida).
- Você vê: mensagens de driver, montagem da raiz, `rcS`, cada script `S*`, o
  `giochi-init` e o Launcher — até o ponto onde para.

### Texto deitado

O painel da X55 é 720×1280 com rotação 270. Se o texto sair de lado, no console
do U-Boot:

```
setenv giochi_fbcon 1
saveenv
```

`boot.cmd` acrescenta `fbcon=rotate:1` ao `bootargs`. Valores úteis: `0` (sem
rotação), `1` (90°), `3` (270°). Isso **não** altera a rotação do Launcher.

---

## 3. Serial: a única visão antes do kernel

Antes do kernel só existe o BootROM, o TPL/SPL e o U-Boot. Nenhum deles escreve
no cartão; todos escrevem no console serial.

- **Pino/velocidade:** UART2 do SoC, `1500000 8N1` — é o `stdout-path` do DTS e
  o `console=ttyS2,1500000n8` do `boot.cmd`.
- **Hardware:** qualquer adaptador USB-UART de 3,3 V (CH340, CP2102, FT232).
  Ligue GND, RX↔TX e TX↔RX. **Não** ligue o VCC do adaptador no aparelho.
- **Leitura:** `picocom -b 1500000 /dev/ttyUSB0` ou
  `minicom -D /dev/ttyUSB0 -b 1500000`.

Com o serial ligado, o `boot.cmd` imprime marcadores de progresso:

```
GIOCHI: boot.scr em execucao (mmc=1 slot=2 root=/dev/mmcblk1p2)
GIOCHI: dtb dtbs/rk3566-powkiddy-x55.dtb
GIOCHI: Image carregada de mmc 1:1
GIOCHI: booti kernel=0x2080000 fdt=0x1f00000
```

Se você **não** vê o primeiro marcador, o problema está antes do `boot.scr`:
bootloader ausente/errado, `boot.scr` ilegível, ou o U-Boot não chegou a rodar.
Se vê os marcadores e a tela fica preta, o problema é de kernel/painel — e o
log do cartão (seção 1) ou o fbcon (seção 2) mostra onde.

---

## 4. Diagnóstico rápido por sintoma

| Sintoma | Onde olhar |
|---|---|
| Nada no serial, nada na tela | Bootloader: gravar o `u-boot-rockchip.bin` em 32 KiB (`docs/DEVICES.md`). O BootROM procura o idbloader no LBA 64. |
| Serial mostra U-Boot, mas não o marcador do `boot.scr` | O `boot.scr` não foi lido. Confira `mmc` (o cartão é **1**, não 0) e a partição 1. |
| `boot.scr` roda, mas "FALHA ao ler Image" | Partição 1 sem `Image`, ou `giochi_mmc` errado. |
| Kernel inicia e para em `Waiting for root device` | O `root=` não existe: slot vazio ou imagem gravada a partir do offset errado. |
| Boot chega ao userspace e trava | `giochi-boot.log` na partição 1: a última linha do `rcS` antes do silêncio é o culpado. |
| Boot "ok" mas tela preta | O desenho dos widgets do Launcher ainda é esboço (`render.cpp`). Use `giochi-boot.log` para confirmar que o processo está vivo. |
| `/user` não grava nada | `LABEL=GIOCHI_USER` no `fstab` exige `CONFIG_FEATURE_MOUNT_LABEL` no BusyBox (está em `busybox.fragment`). |

---

## 5. Desligar o log

- Depois de gravar, crie um arquivo vazio chamado **`nolog`** na partição 1. O
  `S00bootlog` não escreve mais nada (o boot continua normal).
- Ou remova `/etc/init.d/S00bootlog` da imagem (é um arquivo só; `post-build.sh`
  falha se ele faltar, então tire também da lista lá).

O custo do log em operação normal é uma escrita por linha de log na partição
FAT — irrelevante para diagnóstico e indesejável em produção. A decisão de
desligar por padrão fica para depois de o boot estar estável.
