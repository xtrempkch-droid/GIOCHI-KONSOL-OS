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

## 1.5 Primeiro: quem está rodando? (o firmware de fábrica costuma ganhar)

Antes de qualquer coisa, responda esta pergunta: **o sistema que apareceu é o
nosso?** Num RK3566 com o firmware de fábrica no eMMC, o bootloader do eMMC
inicia primeiro — o cartão só é usado se aquele bootloader procurar por ele. O
sintoma é idêntico ao de um boot quebrado: a nossa tela não aparece.

Com o aparelho ligado e conectado por USB ao PC, não há o que adivinhar:

```sh
for d in /sys/bus/usb/devices/*/; do
  [ -f "$d/idVendor" ] || continue
  printf '%s  vendor=%s product=%s  %s / %s\n' "$d" \
    "$(cat "$d/idVendor")" "$(cat "$d/idProduct")" \
    "$(cat "$d/manufacturer" 2>/dev/null)" "$(cat "$d/product" 2>/dev/null)"
done
```

- `manufacturer: ROCKNIX` / `product: x35h Handheld Device` → **é o firmware de
  fábrica**. A nossa imagem não foi usada.
- `1d6b:0104` é o gadget composto genérico do Linux; praticamente todo firmware
  desses aparelhos usa esse par, então ele **não** identifica nada — o que
  identifica são as strings `manufacturer`/`product`.

Se o sistema em execução for o de fábrica, descarte as seções 2 e 3 (o kernel
deles não é o nosso) e vá para a seção 6.

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
- **Leitura:** `tools/serial-capture.py` (deste repositório) — acha o adaptador,
  configura a velocidade e grava num arquivo com carimbo de tempo por linha:

  ```sh
  tools/serial-capture.py --list        # quais adaptadores existem
  tools/serial-capture.py               # 1500000 8N1 -> serial.log
  tools/serial-capture.py -b 115200 -o boot.log
  ```

### 1500000 baud: o adaptador pode ser o culpado

1,5 Mbps está no limite prático de vários CH340/PL2303 (especialmente clones).
Se o texto sair truncado, com caracteres trocados ou "meio legível", **teste o
adaptador antes de acusar a placa**: ligue TX e RX do adaptador nele mesmo
(loopback) e veja se o que você digita volta limpo nessa velocidade. Um CP2102
ou FT232 aceita 1,5 Mbps sem drama.

O kernel continua imprimindo em 115200 mesmo que o U-Boot esteja em 1500000? Não
— a velocidade é a mesma nas duas pontas. Se o adaptador não der conta, a saída
é reduzir a velocidade **na imagem** (U-Boot e kernel) e recompilar.

### Se o `stty` disser "Permission denied"

O nó do adaptador é `root:dialout`. Entre no grupo uma vez (e reentre na
sessão):

```sh
sudo usermod -aG dialout "$USER"     # depois: sair e entrar de novo
```

Alternativa sem `sudo`, se o seu usuário estiver no grupo `docker`: rode a
ferramenta dentro do container com o dispositivo passado adiante —

```sh
docker run --rm -it --device /dev/ttyUSB0 \
    -v "$PWD/tools:/t:ro" -v "$PWD:/out" giochi-builder:1 \
    python3 /t/serial-capture.py -d /dev/ttyUSB0 -b 1500000 -o /out/serial.log
```

Funciona porque o container roda como root e o `--device` cria o nó lá dentro.

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
| Aparece o sistema de fábrica (ROCKNIX/JELOS), não o nosso | Seção 1.5: confira `manufacturer`/`product` no USB. Normalmente é o bootloader do eMMC ganhando do cartão (seção 6) |
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

---

## 6. O firmware de fábrica está ganhando do cartão

Este é o caso mais comum de "gravei a imagem e não apareceu nada".

### Por que

O BootROM do RK3566 procura um bootloader numa ordem fixa (SPI → eMMC → SD).
Um aparelho de fábrica **tem** bootloader no eMMC. Então:

1. o bootloader do eMMC roda (no X35H é o U-Boot do ROCKNIX);
2. ele decide se procura o cartão. Muitos builds fazem isso (varrem `mmc1` e
   depois `mmc0` atrás de `boot.scr`/`extlinux.conf`), mas não todos;
3. se ele achar o nosso `boot.scr` e o boot falhar, ele **segue para o próximo
   alvo** e inicia o eMMC — sem avisar. Você vê o sistema de fábrica.

Um boot que falha no meio do `boot.scr` é indistinguível, a olho nu, de um
cartão ignorado. O que separa os dois é o serial (seção 3) ou o log no cartão
(seção 1) — e o log só existe se o kernel chegou a montar a raiz.

### O que fazer

| Situação | Caminho |
|---|---|
| O bootloader do eMMC varre o cartão | Mantenha o cartão e corrija o `boot.cmd`/DTB (abaixo) |
| Ele não varre | Grave a **nossa** imagem no eMMC (ver abaixo), ou force o modo MaskROM |
| Aparelho sem DTS upstream (X35S/X35H) | A imagem sai **sem** bootloader (`genimage-no-uboot.cfg`) e depende do bootloader de fábrica — que é justamente o do eMMC |

### A pegadinha que já mordeu (medida no serial): `fdtfile` tem `rockchip/`

O U-Boot exporta `fdtfile` na forma **upstream**, `rockchip/<placa>.dtb`
(espelha `arch/arm64/boot/dts/rockchip/`). O que aparecia no console do X35H:

```
GIOCHI: boot.scr em execucao (mmc=1 slot=2 root=/dev/mmcblk1p2)
Failed to load 'dtbs/rockchip/rk...
47516160 bytes read in 4025 ms          <- Image OK (é a nossa)
ERROR: Did not find a cmdline Flattened Device Tree
Could not find a valid device tree
```

Ou seja: **U-Boot, `boot.scr` e `Image` estavam certos**; errava só o caminho do
DTB. A imagem agora grava o blob nas duas formas (`dtbs/<nome>.dtb` e
`dtbs/rockchip/<nome>.dtb`) e o `boot.cmd` ainda tenta `dtbs/board.dtb` e
`board.dtb` como rede de segurança.

### A pegadinha que já mordeu: DTB do aparelho errado

O `boot.cmd` escolhe o DTB assim: tenta `dtbs/${fdtfile}` (o nome que o U-Boot
conhece) e, **se esse arquivo não existir**, usa `board.dtb`. Uma versão
anterior só caía para `board.dtb` quando `fdtfile` estava **vazio** — e
`fdtfile` vem do ambiente do U-Boot de fábrica, então quase nunca está vazio.
Resultado: num aparelho diferente daquele para o qual a imagem foi gerada
(imagem da X55 no X35H), o `load` falhava, o fallback não acontecia, o `booti`
recebia um FDT inválido e o U-Boot ia para o eMMC.

Consequência prática: **gere a imagem do aparelho certo.** O cartão só carrega
o DTB do aparelho indicado no build — não existe imagem "universal" de RK3566.

```sh
tools/build-image.sh --list
tools/docker-build.sh x55                 # X55 (DTS mainline, U-Boot mainline)
tools/docker-build.sh x35h --fallback-dts # X35H (sem DTS/U-Boot upstream)
```

No X35S/X35H o `--fallback-dts` usa um DTS mainline de painel 640×480: serve
para ver se o kernel sobe, **não** garante painel nem controles.

### Melhor caminho para o X35H: pegar o DTB real do aparelho

Se o firmware de fábrica está rodando, ele tem o DTB exato da sua placa na
memória. É o melhor DTS que existe para ela:

```sh
# no aparelho (ROCKNIX/JELOS, ssh root@<ip>)
cat /sys/firmware/fdt > /tmp/x35h.dtb
```

Com o arquivo em mãos, compile a imagem usando o DTB pronto, sem inventar DTS:

```sh
tools/build-image.sh x35h            # com GIOCHI_DTB_PATH=/caminho/x35h.dtb
```

`GIOCHI_DTB_PATH` é copiado para `dtbs/` pelo `post-image.sh` e também vira o
`board.dtb` — o mesmo blob que o fallback do `boot.cmd` usa.

### Instalar no eMMC a partir do cartão

Se o cartão inicia mas o sistema de fábrica insiste em ganhar, o caminho é
colocar a nossa imagem **no eMMC** — e o único lugar seguro para fazer isso é de
dentro do próprio sistema, com o cartão rodando.

O rootfs traz `/usr/sbin/install-image-to-emmc.sh` (fonte em
`tools/install-image-to-emmc.sh`). Ele:

1. recusa alvo que não seja o eMMC (compara o sysfs com `fe310000.mmc`; o cartão
   é `fe2b0000.mmc`);
2. recusa rodar se a raiz estiver **no** disco alvo;
3. confere se a imagem cabe no disco **antes** de escrever (um `dd` maior que o
   disco deixa instalação truncada, que não inicia);
4. exige a palavra `APAGAR` digitada;
5. zera o começo **e o fim** do disco — o firmware de fábrica usa GPT e a GPT de
   backup vive nos últimos setores; sem isso o kernel reclama
   `GPT PMBR size mismatch` e pode "consertar" a tabela por cima da nossa;
6. escreve, faz `sync` e lista as partições resultantes.

```sh
# no console, com o Giochi rodando do cartão
install-image-to-emmc.sh /boot/giochi-x55-sdcard.img.xz
# depois: desligar, REMOVER o cartão SD e ligar
```

A imagem é a **mesma** para cartão e eMMC porque a raiz é achada por
`root=PARTUUID=47494f43-02` (assinatura MBR fixa no `genimage.cfg`), e não por
`/dev/mmcblk1p2`. Sem isso a mesma imagem gravada no eMMC viraria `mmcblk0p2` e
o kernel pararia em `Waiting for root device`.
