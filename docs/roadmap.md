# Roadmap

Ordem por **risco × impacto**, não por facilidade. O que depende de teste no
aparelho está marcado com 🧪; o que está bloqueado por terceiros, com ⛔.

Estado por sessão (o que já foi feito e por quê): [HANDOFF.md](./HANDOFF.md).
Armadilhas que não devem ser repetidas: [../AGENTS.md](../AGENTS.md).
Especificação normativa: [architecture-v1.2.md](./architecture-v1.2.md).

## Situação em uma tela

| # | Frente | Estado | Bloqueia |
|---|---|---|---|
| 1 | Imagem bootável no aparelho 🧪 | **nunca testada em hardware** | tudo |
| 2 | Desenho dos widgets no Launcher | esboço (`TODO(v1.2)`) | uso real da UI |
| 3 | Layout × painel (640×480 vs 720×1280) | layout é esticado | UI na X55 |
| 4 | Áudio: decode + mux do RK817 | pendente | aba Música |
| 5 | Gamepad (`rocknix-joypad`) e painel (X35S/X35H) ⛔ | não empacotado | essas duas placas |
| 6 | USB na X35S/X35H 🧪 | não ancora (`ep0out`) | SSH/MTP nelas |
| 7 | Update A/B de ponta a ponta 🧪 | contrato pronto, flip não testado | entrega de update |
| 8 | Fase 2: keyring, chave exclusiva, Proton | fora de escopo v1.2 | — |

---

## 1. Colocar uma imagem para bootar no aparelho 🧪

**É a prioridade zero.** Toda a configuração está validada contra as árvores
reais (Buildroot 2024.02.13, U-Boot v2024.10, rkbin no commit pinado), mas
nenhuma imagem deste repositório foi gravada e ligada.

- [ ] `tools/build-image.sh x55` no CI (workflow **Images**, input `x55`).
- [ ] Gravar `dist/x55/giochi-x55-sdcard.img` e ligar a X55.
- [ ] Coletar: tela liga? serial do U-Boot? `dmesg`? o Launcher sobe?
- [ ] Registrar o resultado em [HANDOFF.md](./HANDOFF.md) com o log colado.

**Aceite:** a X55 liga, mostra o splash, chega ao Launcher e responde ao
`control.sock`. Se falhar, o log do console serial (1500000 8n1) é o próximo
dado — não presuma causa.

Sinais de alerta conhecidos e onde olhar: `docs/hardware-e-drivers.md` (F1–F12),
`docs/DEVICES.md` (bootloader e DTS), `AGENTS.md` §3.8 (BL31/TPL).

## 2. Launcher: desenhar os widgets

O motor está pronto (layout schema 1.6, tema, `settings.json`, overlay de 2
níveis, biblioteca, `buttons.map`), mas `src/launcher/render.cpp` só pinta o
fundo do tema e o bootsplash. Todo o resto depende disso para ser usável.

- [ ] Topbar (40 px) com abas, indicadores e clock — posições vêm de
      `assets/home.json` (`regions.top_bar`).
- [ ] Grid da biblioteca: tile 152×85 + título 20, 4×3, scroll vertical com
      overscan de 1 linha, scrollbar de 4 px.
- [ ] Aba Música: now playing 120×120, barra de progresso, controles, lista.
- [ ] Overlay de Configurações (nível 0 e submenu Sistema) com o estado atual
      dos itens (ex.: `Modo USB   MTP`, `Modo Desenvolvedor   desligado`).
- [ ] `hint_bar` (30 px) com os botões contextuais por aba.
- [ ] Medir VRAM real e comparar com o teto de **4 MB** (§8 da spec) e com os
      `vram_budget_kb` declarados por aba (`home` 800, `music` 1024,
      `library` 4096).

**Aceite:** as 5 abas navegam com os botões mapeados, sem warning de build, com
VRAM medida (não estimada) dentro do orçamento.

> Depois disso, `docs/screenshots/*.png` deve ser **substituído por captura do
> aparelho** (`fbgrab`/DRM). Hoje são renders do layout — ver
> [SCREENSHOTS.md](./SCREENSHOTS.md).

## 3. Layout × painel: rotação e escala

`assets/layout.json` declara **640×480** (§4.1.3), mas a X55 tem painel
**720×1280 com `rotation = <270>`**, ou seja, área lógica **1280×720** (§F8 de
`hardware-e-drivers.md`). Hoje o backend apenas estica a composição.

Não existe escala uniforme que transforme 640×480 em 1280×720: é ×2 na
horizontal e ×1,5 na vertical. Esticar é o que o backend faz hoje e é o que
distorce. A aritmética, com as métricas declaradas (`topbar 40`, `hint 30`,
`tile 152×85`, `título 20`, `gap 8`, respiro 14):

| Escala | Grid 4×3 precisa (altura) | Disponível em 720 | 4 colunas | 5 colunas |
|---|---|---|---|---|
| ×1,0 | 345 | 650 | 632 | 792 |
| **×1,5** | **518** | **615** | **948** | **1188** |
| ×2,0 | 690 | 580 ❌ | 1264 | 1584 |

Ou seja: **×2 não cabe** na vertical (só caberia com 2 linhas em vez de 3) e
×1,5 deixa 166 px de margem de cada lado — espaço suficiente para uma **quinta
coluna** (1188 dos 1280). Decisão a tomar, com uma destas saídas:

- [ ] **(a) Layout nativo por painel** — `assets/devices/x55/layout.json` com
      métricas próprias de 16:9 (topbar 60, hint 45, tile 228×128, título 30,
      canto 6, 5 colunas). É a que preserva qualidade de texto e aproveita a
      tela; exige um segundo conjunto de números para manter.
- [ ] **(b) Escala ×1,5 no render** — um só layout, métricas multiplicadas e
      arredondadas para inteiro (sem meio-pixel: `topbar 60`, `hint 45`,
      `tile 228×128`, `corner 6`). Simples; perde a chance das 5 colunas.
- [ ] **(c) Letterbox** — manter 4:3 centralizado (é o que
      `tools/make-screenshots.py` faz para gerar `x55-home.png`). Honesto,
      desperdiça 33% da largura.

Requisitos em qualquer caso: nada de esticar, texto legível no painel de 5",
métricas inteiras e `hint_bar`/`topbar` proporcionais.

**Aceite:** na X55 a UI ocupa a tela sem distorção e com texto legível; nos
aparelhos 640×480 nada muda. Regravar `docs/screenshots/x55-home.png` com o
resultado real (ou captura do aparelho).

## 4. Áudio: decodificar e não ficar mudo no alto-falante

- [ ] `giochi-musicd`: decodificação PCM real (mpg123/FLAC/Vorbis) → ALSA
      `dmix`, mantendo o protocolo §4.8.4 (`PLAY`, `PAUSE`, `NEXT`, `PREV`,
      `SEEK`, `STATUS`, `QUEUE`).
- [ ] RK817: aplicar `Playback Mux = HP` na inicialização (§3.3 de
      `hardware-e-drivers.md`); em `SPK` o alto-falante fica mudo e o jack
      continua funcionando. O ponto está marcado em
      [audio_alsa.c](../src/musicd/audio_alsa.c).
- [ ] Não matar o daemon ao lançar jogo (§4.8: ele é persistente).
- [ ] Medir RAM e CPU contra `cpu_budget_pct` (3% no RK3326, 2% no RK3566).

**Aceite:** tocar MP3/FLAC/OGG do `/user/music` com áudio no alto-falante e no
jack, sobrevivendo a abrir e fechar um jogo.

## 5. Hardware que falta empacotar ⛔ 🧪

Sem estes componentes a X35S/X35H **não têm painel nem gamepad** e a X55 fica
sem os analógicos. Nenhum é upstream.

| Componente | Para quê | Situação |
|---|---|---|
| `rocknix-joypad` | gamepad e analógicos nas 3 placas | não empacotado |
| `panel-generic-dsi` | painel de X35S/X35H | não empacotado |
| DTS `rk3566-powkiddy-x35s` | painel/controles de X35S/X35H | não está no repo (terceiros) |

- [ ] Trazer o driver e o `panel-generic-dsi` como pacote do `BR2_EXTERNAL`,
      respeitando a licença e citando a origem no arquivo.
- [ ] Documentar em `docs/DEVICES.md` como fornecer o DTS externo
      (`GIOCHI_DTS_PATH`) — hoje esses aparelhos só geram **imagem de teste**
      com `--fallback-dts`.
- [ ] ⛔ Sem esses três itens, X35S/X35H ficam em "bring-up", não em suporte.

## 6. USB na X35S/X35H 🧪

`dwc3 ... failed to enable ep0out`: o gadget aborta no controlador e o UDC fica
`not attached`. Os DTS oficiais comparados são idênticos ao da X55, o que aponta
para hardware (F12) e não para software.

- [ ] Testar com outro cabo com fios de dados confirmados e, se possível, outra
      porta/placa.
- [ ] Comparar `usb@fcc00000` / `usb2phy0_otg` do DTB carregado no aparelho com
      os da X55 (o preflight já imprime os nós).
- [ ] Confirmar a degradação no aparelho: o `post-build.sh` cria
      `/etc/giochi/usb_gadget_disabled` quando `GIOCHI_USB_GADGET=no`, e
      `S50usb`/`S55usbnet`/`S62sshd` saem com **uma** linha de log cada — não
      pode haver erro repetido a cada boot.

**Aceite:** ou o gadget ancora, ou a degradação é limpa e documentada como
limitação de hardware.

## 7. Update A/B de ponta a ponta 🧪

Contrato de assinatura está pronto e verificado (Ed25519 + SHA-256, com recusa
explícita se o binário não tiver suporte). Falta o ciclo real.

- [ ] Fixar o nome da variável de ambiente do U-Boot para o flip de slot
      (`S05update` chama `fw_setenv giochi_next_slot` — o nome precisa ser
      validado contra o layout de env real: `CONFIG_ENV_IS_IN_FAT`,
      `uboot.env` em `0:1`).
- [ ] Testar: instalar imagem assinada → `bootcount` incrementa → slot troca →
      imagem nova sobe; e o caminho de rollback (`bootcount > 3`).
- [ ] Testar recusa: imagem adulterada **e** chave errada (os dois casos).
- [ ] Update remoto por Wi-Fi continua **fora de escopo da v1.2** (§4.3): só
      SD/arquivo.

**Aceite:** update A→B e B→A em aparelho, com rollback automático quando a
imagem nova não sobe.

## 8. Pendências menores (não bloqueiam nada)

- [ ] `giochi-thumbgen`: extrair `thumb.png`/`capa.png` do tar com libarchive
      (hoje só gera placeholder quando o pacote não traz thumbnail).
- [ ] `device_id`: derivar do serial do device tree
      (`/proc/device-tree/serial-number`) em vez do sufixo fixo `-001`.
- [ ] `S30swap`: validar o benchmark random 4K e o corte de 10% num cartão real
      (a aritmética é `awk`, sem `bc`).
- [ ] `S10storage`: confirmar a derivação de partições por `/proc/cmdline` em
      cartão de slot duplo (SD = `mmcblk1`).
- [ ] Dongles: UI de rede (hoje só hotplug e scripts).
- [ ] `tools/check-uboot.py`: comparar também a versão do U-Boot declarada com
      a mínima que tem o defconfig (hoje só valida existência).

## 9. Fase 2 (fora de escopo da v1.2)

Registrado para não se perder; **não implemente sem decisão explícita**.

- Keyring para credenciais de Wi-Fi (hoje texto plano em `/user/settings.json`,
  risco documentado no README).
- Autenticação **exclusiva** por chave no Modo Desenvolvedor (§4.6.1 diz
  "fase 2"); túnel reverso e SSH sobre Wi-Fi seguem fora.
- Proton: porta aberta e desativada (§4.10). Critérios de ativação em
  [architecture-v1.2.md §4.10](./architecture-v1.2.md); `BR2_PACKAGE_GIOCHI_PROTON=n`
  e `metadata.cpp`/`classify.py` já recusam `runtime=proton` quando desligado.
- Automação `libusb` do `giochi-forge`; ADB; WebUSB.

---

## Como usar este arquivo

- Trabalhe **de cima para baixo**: os itens 1–3 são o caminho crítico.
- Ao fechar um item, marque as caixas **e** registre a evidência (log, medição,
  captura) na entrada correspondente de [HANDOFF.md](./HANDOFF.md).
- Item que depende de aparelho e não tem aparelho à mão: não marque como feito,
  marque como *bloqueado por hardware* e diga o que falta medir.
- Descobriu que uma afirmação deste roadmap está errada? Corrija aqui e diga na
  entrada do HANDOFF qual era a afirmação antiga.
