# Changelog

Formato baseado em [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/)
e versionamento [SemVer](https://semver.org/lang/pt-BR/).

## [Não publicado]

### Adicionado
- **Seis temas, cinco deles novos** ([docs/TEMAS.md](docs/TEMAS.md)): além do
  `dark_default` original, `neon_rose`, `ember_dark`, `mono_minimal`,
  `mono_gamepad` e `ios_glass`, transpostos dos cinco mockups do mantenedor.
  Um tema agora é **cor + métrica**: raio de canto, borda do tile, anel de
  foco, densidade da grade, escala da capa do álbum e modo da barra de dicas.
  - `schemas/theme.schema.json` (novo) e `assets/themes/` com `index.json`.
  - Troca a quente em **Select → Sistema → Tema**, gravada em
    `settings.json` (`theme_id`); `--theme ID` e `GIOCHI_THEME` para testes.
  - A densidade muda a grade de verdade (medido): compacta 6×4, normal 5×3,
    espaçosa 4×3 em 1280×720 — e 4×4 / 4×3 / 3×3 em 640×480. Com o tile fixo
    as três dariam a mesma contagem, então o tile escala junto (88/100/115 %).
  - Galeria: `make tema-galeria` gera `docs/temas/*.png` + contato 2×3.
  - Guarda: `tests/test_themes.py` (19 testes) cobra schema, **contraste
    medido** (WCAG), paridade do índice e efeito real no binário.
- **`device/`**: uma pasta por aparelho com `device.conf`, `theme.json` e
  `layout/`. Antes a mesma identidade vivia em duas árvores
  (`buildroot/board/giochi/devices/` e `assets/devices/`) e as duas divergiam —
  a armadilha do §3.20 do AGENTS.md.
- **`apps/`**: aplicações do PC separadas do núcleo. `apps/forge` (movido de
  `src/forge`) e **`apps/store`** (novo): `catalogo` produz `store.json` e
  `news.json`, `instalar` copia para o cartão conferindo o sha256 **no
  destino**, `listar` mostra o catálogo. Fecha o buraco do `news.json` sem
  produtor: agora a Home mostra a novidade da Loja.
  - Guarda: `tests/test_store.py` (16 testes), inclusive `metadata.json` fora
    do primeiro lugar (ADR-11) e `.giochi` x86_64 recusado com motivo.
- **`apps/launcher`**: o Launcher virou **aplicativo** e saiu de `src/launcher`
  (mesmo critério que levou `apps/forge` para lá). `src/` passou a ser só o
  núcleo compartilhado — bibliotecas comuns, supervisor e daemons. Assim dá
  para mexer na UI sem esbarrar em quem mexe no sistema ou nos jogos.
  - O Makefile compila `apps/launcher/*.cpp` e um carimbo
    (`build/.layout-launcher-apps`) apaga os `.d`/`.o` incrementais que ainda
    citam o caminho antigo — sem ele o `make` aborta na **leitura** do
    Makefile ("Sem regra para processar o alvo 'src/launcher/main.cpp'").
  - O snapshot de fontes do `tools/build-image.sh` passou a copiar `apps/`:
    sem isso o pacote compilaria um Makefile cujos fontes não foram copiados.
  - Guarda: `tools/check-image-contents.sh` falha se `src/launcher` voltar a
    existir, se `apps/launcher` sumir, se o Makefile deixar de compilá-lo ou
    se o snapshot parar de copiar `apps/`. Testada ao contrário (recriar
    `src/launcher` → rc=1).
- `tools/make-theme-gallery.py` e o alvo `make tema-galeria`.

### Corrigido
- **A fonte da UI perdia a METADE DE BAIXO de cada letra** (AGENTS.md §3.72).
  `tools/make-font.py` trocava `w` e `line_height` ao desempacotar uma lista de
  tuplas, e o recorte saía 22x11 em vez de 13x22: só a metade de cima do glifo
  entrava no atlas. Na tela o sintoma era "fonte ruim/borrada", não "fonte
  quebrada" — passou por cinco imagens.
  - A montagem virou `render_face()`, com bloco de largura `max(avanço, tinta)`
    (22 glifos do DejaVu passam 1 px do próprio avanço: `A`, `K`, `T`, `f`, `r`).
  - Verificado: o glifo do atlas agora é byte a byte igual ao desenho direto do
    Pillow. Tinta do texto voltou ao da referência (abas 87 528 → 293 233
    contra 299 228 de `docs/proposta/library-1280x720.png`).
  - Guarda: `tests/test_font.py` olha a **metade de baixo** da linha do `g` —
    olhar "pixels pintados" no arquivo inteiro não pegaria, porque a metade de
    cima continuava lá. Testado ao contrário: 4 falhas.
- **Fonte com um peso só.** A referência negrita título de tile, aba ativa,
  tecla da hint bar e título de painel; o atlas só tinha regular. Agora são
  duas faces por tamanho, e `ui_text_weight(UiTextRole)` (módulo puro) é a
  fonte única de qual peso cada papel usa — o `dump-geometry` publica a lista e
  o `preview-ui.py` lê, como já era com o `UiRole`.
- **Aba medida em negrito nos dois lados**: a ativa é negrita e medir pelo
  regular fazia ela transbordar a célula. A sublinha usa a largura do negrito
  (117 px na referência; o regular dava 104).
- **`hint bar` tinha duas tabelas de pares** (`main.cpp` e
  `tools/dump-geometry.cpp`) e elas divergiram. Agora `ui_hint_text()`, em
  `i18n.cpp`, serve os dois; a Home não anuncia `B`, que é inerte nela (§4.1.2).
- **`build_menu()` e `cycle_language()` estavam chamadas e não definidas** em
  `main.cpp` — o launcher não compilava. Reescritas, com o submenu Sistema
  completo. No caminho, `cycle_language` passou a trocar o idioma a quente,
  gravar em `settings.json` e reconstruir o menu.
- **Preview com as linhas de separação mais finas que o aparelho**: 1 px fixo
  contra `scale_pct/50` (3 px em 1280x720). A espessura agora vem do
  `dump-geometry` (`tokens.divider`).

- **Duas rotações independentes** (AGENTS.md §3.70). A UI girava por
  `ui_present()` com o valor do `device.conf`; o console de vídeo girava por
  `setenv giochi_fbcon N` **à mão no U-Boot**, sem relação com o resto. Elas
  concordarem era coincidência, não desenho — e são duas unidades para o mesmo
  fato (`270 graus` × `rotate:3`).
  - `post-image.sh` substitui `@FBCON_ROTATE@` no `boot.cmd` por
    `GIOCHI_ROTATION/90` e gera `boot.cmd.gerado` (o `boot.cmd` versionado
    serve todos os aparelhos e não é editado). O U-Boot continua podendo
    sobrepor, mas o padrão agora é derivado.
- **Falha de vídeo não é mais tela preta.** O `bootargs` desta imagem tem
  apenas `console=ttyS2`, então o `printk` não vai para a tela e re-bindar o
  `vtcon` sozinho não mostrava nada. Agora o caminho de falha do Launcher
  **escreve direto no `/dev/tty1`**: `A interface grafica nao iniciou`,
  o motivo, e o caminho do `giochi-boot.log`.
- **Launcher lê a rotação do `/etc/giochi/device.conf`**, não do ambiente
  (regra da §3.47): o caminho de inicialização — `rcS`, `S99launcher` ou
  `inittab` — não deve decidir a orientação.

- **UI em pé na X55 e layout quebrado** (AGENTS.md §3.69). O painel é
  **720×1280 em retrato** com `rotation = <270>` no DTB, e o layout é 1280×720.
  O kernel não gira imagem (`rotation` só alimenta `panel-orientation` do
  conector, que o SDL 2.28 não lê) e o `S20graphics` nunca exportava a rotação.
  Pior: a geometria era calculada sobre a saída **física** (720×1280), o que dá
  escala 2,66× e **uma** coluna — daí os tiles gigantes empilhados.
  - `ui_present()` (módulo puro, em `ui_geometry`) decide ângulo, retângulo de
    destino e se precisa de textura intermediária; o `render.cpp` desenha no
    tamanho **lógico** do `layout.json` e apresenta girado com
    `SDL_RenderCopyEx`.
  - O quad de destino usa as dimensões lógicas e fica centrado na saída (o
    `RenderCopyEx` gira em torno do centro do `dstrect`): na X55,
    `dst = (-280, 280, 1280, 720)`, que girado cobre exatamente 720×1280.
  - `ui_present` **recusa** girar quando o framebuffer não é o layout
    transposto — girar sem transposição corta metade da tela.
  - Sentido da rotação é ajustável **no cartão**: `echo 90 > /boot/rotate`
    (partição 1) sobrepõe o `device.conf`. 90 e 270 são ambos paisagem; o que
    difere é a imagem sair de cabeça para baixo, e isso só o aparelho decide.

- **`EGL not initialized` era configuração, não driver ausente** (AGENTS.md
  §3.68). O `S20graphics` forçava `MESA_LOADER_DRIVER_OVERRIDE=panfrost`, mas
  nesta placa o dispositivo DRM é o *display* (`rockchip-drm`) e quem fala com
  ele é o `rockchip_dri.so` (kmsro), que delega a renderização ao panfrost.
  Forçando o driver de render direto, o EGL falha. Além disso a imagem não
  tinha **nenhum** driver de software: sem `swrast`/`kms_swrast`, uma falha do
  panfrost não tinha para onde cair.
  - `S20graphics` decide por medição: tenta o driver do barramento e, se a
    sonda falhar, cai para `kms_swrast` (logando
    `Mesa: AVISO sem aceleracao`). `GIOCHI_MESA_OVERRIDE` continua como escape
    hatch manual.
  - `BR2_PACKAGE_MESA3D_GALLIUM_DRIVER_SWRAST=y` nos dois templates: é a rede
    de segurança. `swrast_dri.so`/`kms_swrast_dri.so` são hardlink da mesma
    `libgallium`, então o SquashFS quase não cresce.
  - `giochi-gpu-probe` passou a listar `/dev/dri` (cards e render nodes) e os
    drivers em `/usr/lib/dri` antes de tentar o EGL.
  - A decisão tem três estados (passou / falhou / **não testado**): sonda
    ausente loga `Mesa: sonda nao executada` em vez de afirmar que passou.
- **Launcher devolve o console quando o vídeo falha**: com o fbcon
  desvinculado pelo `S20graphics`, uma falha do SDL deixava a tela preta sem
  nenhuma pista visível. Agora o caminho de falha re-binda o `vtcon` e sobe o
  `console_loglevel`, então o log do boot reaparece na tela.
- **`splash` é marcador de diagnóstico**: default de 45 → **180 quadros** (3 s),
  `GIOCHI_SPLASH_ALWAYS=1` mantém o splash na tela (teste isolado de "o SDL
  desenha?"), e o log ganhou `render: SDL2 driver=... renderizador=...
  saida=...` e `splash: exibindo N quadros`.
- **Primeiro quadro em painel 640×480 desenhava nada**: a geometria tem default
  640×480, então a comparação "tamanho mudou" dava falso no primeiro frame e
  `tab_count`/`capacity` ficavam zero — justamente no painel do X35S/X35H.

### Adicionado
- **A UI do Launcher desenha** (antes o `frame()` só limpava a tela, o que fazia
  "tela lisa" se confundir com "não subiu"):
  - `src/launcher/ui_geometry.{hpp,cpp}` — tokens de
    [docs/LAYOUT-PROPOSTA.md](docs/LAYOUT-PROPOSTA.md) §3, grade com colunas
    derivadas da largura (4 em 640×480, 5 em 1280×720), paginação pelo foco,
    abas medidas e anel de foco. **Módulo puro, sem SDL**, coberto por
    `tests/test_ui_geometry.cpp` (suíte `ui_geometry`, 298 checagens) que
    reproduz os números medidos da proposta (grade termina em 383 e 576).
  - `assets/font.a8` + `assets/font.json`, gerados por
    [tools/make-font.py](tools/make-font.py): atlas 8 bpp de 102 KB com os
    tamanhos 12/14/18/21 px. Sem `SDL2_ttf`/freetype no rootfs. Contrato cobrado
    por `tests/test_font.py` (suíte `fonte-embutida`, 1173 checagens).
  - `src/launcher/font.{hpp,cpp}` — atlas em uma textura ARGB8888 tingida por
    `SDL_SetTextureColorMod`, UTF-8 com code point de substituição e recorte
    `...` medido em pixels.
  - `src/launcher/render.cpp` — topbar com sublinhado de acento, grade com anel
    de foco por fora do tile, hint bar, painel de Configurações e painel por aba
    (Loja/Ports avisam que o app não existe; Home/Música dizem a origem do dado).
  - `make ui-preview` (`tools/dump-geometry` + `tools/preview-ui.py`) gera
    `docs/proposta/ui-{640x480,1280x720}.png` a partir do **mesmo**
    `ui_geometry.cpp` e do **mesmo** atlas — sem um segundo conjunto de números.
- **Boot completo em hardware (X55)**: `rcS` → `giochi-init` → Launcher em
  **3,9 s** desde `Starting kernel`. Bootloader, DTB, `root=PARTUUID`, SquashFS
  e userspace provados no aparelho (log serial em
  [docs/HANDOFF.md](docs/HANDOFF.md)). A tela continua apagada porque o DRM não
  faz bind do DSI — bloqueio registrado em
  [AGENTS.md](AGENTS.md) §3.44, não confundir com o painel do X35S/X35H (§3.43).
- **`src/common/giochi_deviceconf.{c,h}`** + `tests/test_deviceconf.cpp`
  (suíte `deviceconf`): leitura de `/etc/giochi/device.conf` com a chave casada
  por `strlen` e valor que nunca é truncado. Corrige dois defeitos silenciosos
  medidos no aparelho: `device_profile.json` dizia `(device=rk3326 soc=rk3326)`
  numa X55 (os `-D` de identidade não chegavam ao compilador) e
  `buttons.map: secao [efault]` (a chave tem 19 caracteres, o código pulava 20).
- **`tests/test_primeiro_boot.py`** (suíte `primeiro-boot`): guarda os quatro
  defeitos do primeiro boot real — chamada dupla de serviço no `rcS`, `S10mdev`
  na imagem, árvore de `/user` inexistente e leitura de `device.conf`.
- **`tools/check-dts-panel.py`** + suíte `painel-do-dts`: diz, antes de gastar
  um ciclo de flash, se o DTS escolhido tem painel utilizável. Segue
  `#include` (inclusive `DTS_PATH` out-of-tree) e reconhece o caso de driver
  fora da árvore. Roda no `make lint`; o `build-image.sh --fallback-dts` passou
  a **avisar** quando o fallback não tem painel — antes passava calado.
  Já produziu dois resultados úteis: `rk3326-generic` (fallback `rg351v`)
  **não** tem painel nenhum (aquela imagem nunca acende), enquanto o
  `rg353v` usado no X35S/X35H **tem** (`newvision,nv3051d`, herdado do
  `rg353p` por `#include`).
- **`tools/verify-card.py`** + suíte `verificador-de-cartao`: compara o cartão
  gravado com a imagem byte a byte e separa duas causas com ações opostas —
  "a gravação não terminou" (`dd` sem `conv=fsync`) de "o cartão tem setor
  ruim". Nasceu de um boot real que parou em `No working init found` com a
  imagem íntegra (`unsquashfs` extraiu tudo) e o cartão devolvendo `EIO`.
  Leitura pura, recusa alvo com partição montada.
- **[docs/BOOT-DEBUG.md](docs/BOOT-DEBUG.md) §7**: sintoma `Mounted root` +
  `No working init found` + `I/O error`, ordem de diagnóstico e a nota sobre
  `max-frequency` no `&sdmmc0` para cartão marginal em SDR104.
- **`tools/serial-capture.py`**: lê o console UART2 (`ttyS2`, 1500000 8N1) pelo
  PC, autodetecta o adaptador, configura via `stty` (o `termios` do Python não
  tem constante para 1500000) e grava com carimbo de tempo por linha. Foi com
  ele que o boot deixou de ser "não aparece nada" e virou uma linha do tempo.
- **`tools/install-image-to-emmc.sh`** (instalado em `/usr/sbin` da imagem):
  grava a imagem no eMMC a partir do sistema rodando no SD, recusando alvo que
  não seja o eMMC (`fe310000.mmc`), recusando rodar com a raiz no disco alvo,
  conferindo se a imagem cabe **antes** de escrever e zerando as duas pontas do
  disco (o firmware de fábrica usa GPT e a GPT de backup vive no fim). Adaptado
  do projeto Walkmam ao BusyBox deste rootfs (sem `lsblk`/`wipefs`/`stat`).
- **[docs/BOOT-DEBUG.md](docs/BOOT-DEBUG.md)**: procedimento para quando a tela
  não acende — log na partição FAT do cartão, imagem de diagnóstico com `fbcon`
  e console serial (`ttyS2`, `1500000 8N1`), com tabela sintoma → causa.
- **`scripts/init.d/S00bootlog`**: primeiro script de `rcS`. Grava
  `giochi-boot.log`, `giochi-boot.1.log`, `giochi-boot.dmesg` e o marcador
  `giochi-boot.inprogress` na partição FAT (`/boot`) — legível em qualquer PC.
  Usa `syslogd -O` (escrita sem buffer) + `klogd`, e o `rcS` duplica a saída
  dos serviços com `tee`. Fallback `/user` → `/run`; nunca aborta o boot;
  desliga com `touch nolog`.
- **Modo diagnóstico de imagem**: `tools/build-image.sh <aparelho> --debug`
  (ou `GIOCHI_DEBUG_BOOT=1`) liga `fbcon` via
  `board/giochi/linux-debug.fragment`, fazendo o kernel escrever na tela. A
  árvore de build é reaproveitada e o artefato sai como `...-sdcard-debug.img`.
- `boot.cmd`: marcadores de progresso no serial (`GIOCHI: boot.scr em
  execucao`, `Image carregada`, `booti`) e `giochi_fbcon` para
  `fbcon=rotate:N` no painel 720×1280/270° da X55.
- Suíte `log-de-boot` (`tests/test_boot_log.py`, 21 checagens) e seção 6 em
  `tools/check-image-contents.sh` (scripts de pacote que não podem voltar).
- **Primeira imagem completa gerada neste repositório**:
  `dist/x55/giochi-x55-sdcard.img`, 936 MB (147 MB de blocos reais), com U-Boot
  mainline em 32 KiB, kernel 6.12, DTB do `rockchip`, SquashFS de 29 MB nos dois
  slots A/B e `user.ext4` de 512 MB. Verificado com `fdisk -l`, comparação byte
  a byte do `u-boot-rockchip.bin` e `mdir` no `boot.vfat`. **Não foi bootada.**
- `tools/check-device-table.sh` (em `make lint`): valida o formato do
  `device_table.txt` antes de qualquer compilação.
- Três guardas novas em `tests/test_image_layout.py`: coerência entre
  `partition-table-type` e o tipo de cada partição (nos **dois** cfgs), proibição
  de `%PARTUUID%` literal e exigência de `exit 0` no fim do `post-image.sh`.
- Quatro armadilhas novas no [AGENTS.md](AGENTS.md) (§3.25 a §3.28) e a entrada
  (16) no registro de agentes.
- **[docs/APPS.md](docs/APPS.md)**: inventário dos componentes da v1.2 separado
  por onde rodam (console × PC) e por tipo, com estado real de cada um e as
  lacunas — `giochi-store` e `portmaster` ainda não existem, `news.json` não tem
  produtor, `giochi-thumbgen` não extrai imagem do pacote e `giochi-forge` não
  detecta/transfere ainda.
- Item 8 no [roadmap](docs/roadmap.md): apps externos que a spec pede e faltam,
  com o que decidir na UI enquanto isso.
- **`AGENTS.md`**: direcionamentos para agentes de IA — mapa do repositório,
  as 12 armadilhas silenciosas já resolvidas (com o sintoma, a causa e a guarda
  de cada uma), o que **não** foi verificado, os portões de verificação e o
  protocolo de registro por sessão.
- **[docs/roadmap.md](docs/roadmap.md)**: ordem de trabalho por risco × impacto,
  com critério de aceite por item, o que depende de hardware (🧪) e o que está
  bloqueado por terceiros (⛔). Inclui a aritmética da escala de layout na X55
  (×2 não cabe na vertical; ×1,5 deixa margem para 5 colunas).
- `tools/check-links.py` valida também **âncoras** (`#secao`), não só o arquivo:
  link para âncora inexistente levava o leitor ao topo da página sem erro.
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
- **`rcS` chamava cada serviço duas vezes** (`"$script" start || "$script"`):
  o fallback rodava o script sem argumento e o `case` imprimia
  `Usage: /etc/init.d/S10mdev {start|stop|restart}` no boot da X55, escondendo
  a falha real. Agora é uma chamada só, com o erro registrado.
- **`/user/music` e `/user/games` não existiam** na imagem: os diretórios do
  SquashFS ficam escondidos sob o `user.ext4` vazio, e o Launcher abria com
  `0 pacotes`. `S10storage` semeia a árvore a cada boot (com teste de escrita
  real; `mkdir -p` de diretório existente devolve 0 mesmo com a partição fora
  do ar).
- **`S10mdev` saiu da imagem**: sem `modprobe` no rootfs ele só gerava erro;
  `/dev` é `devtmpfs` e quem carrega módulo sob demanda é o
  `giochi-dongle-hotplug`.
- **A tela do X35S/X35H tem uma causa conhecida e ela não é o build.** O DTS
  oficial da ROCKNIX troca o painel para `compatible = "rocknix,generic-dsi"`,
  driver **fora da árvore** do mainline (fonte única, 724 linhas, GPL-2.0).
  Com o DTS certo e sem esse driver o painel não liga — e o boot está correto.
  O mesmo DTS usa `&joypad` (também fora da árvore). Registrado em
  [docs/DEVICES.md](docs/DEVICES.md) e no [AGENTS.md](AGENTS.md) §3.43.
  O mantenedor decidiu (2026-10-31) **adiar** a revisão de licenciamento: o
  driver não entra por ora, e a lacuna fica registrada como conhecida e adiada
  (não como defeito). `tools/check-dts-panel.py` impede que a falta dele seja
  confundida com problema de bootloader; o trabalho técnico de quando entrar
  está anotado nos `.conf` de X35S/X35H.
- **Primeiro boot validado em hardware.** O console serial da X55 mostrou,
  com a imagem corrigida, a cadeia inteira funcionando:
  `boot.scr em execucao (root=PARTUUID=47494f43-02)` →
  `dtb dtbs/rockchip/rk3566-powkiddy-x55.dtb` → `Image carregada` →
  `Starting kernel` → `Mounted root (squashfs)`. Todas as correções do bloco
  anterior estão provadas no aparelho, não só na configuração. O que ainda
  falha é leitura de cartão (`I/O error` em `mmcblk1`), não build — ver o
  verificador de cartão acima.
- **`SIGPIPE` matava o `giochi-init`.** `giochi_sock_write_all` usava `write()`;
  se um cliente fechasse antes de ler a resposta inteira, o processo morria com
  `rc=141` (128+SIGPIPE) sem log nenhum. No aparelho quem fala com o
  `control.sock` é o Launcher — fechar o app no momento errado derrubaria o
  supervisor da sessão. Agora as escritas usam `send(MSG_NOSIGNAL)` e o init
  ignora `SIGPIPE`. Regressão: cenário 6b da `devmode_test.sh` (cliente conecta,
  manda `PING` e fecha sem ler; outro cliente tem de receber `PONG`).
- **Teste `devmode` intermitente, por duas causas distintas**: a corrida
  `bind()`/`listen()` do socket (cliente do teste passou a reconectar) e o
  `SIGPIPE` acima (bug real do servidor). Foi um wrapper que registra o `$?` do
  processo que revelou o `rc=141`.
- **A causa exata do "não aparece nada", medida no console serial do X35H.**
  O serial provou que **o nosso U-Boot e o nosso `boot.scr` rodam**: o `Image`
  de 47 516 160 B é lido e o `booti` começa. A falha era só o device tree:
  o U-Boot exporta `fdtfile` na forma upstream (`rockchip/<placa>.dtb`) e o
  cartão tinha o arquivo **plano** em `dtbs/` →
  `Failed to load 'dtbs/rockchip/rk...'` → `ERROR: Did not find a cmdline
  Flattened Device Tree` → o U-Boot seguia para o eMMC e subia o ROCKNIX.
  Agora o DTB é gravado nas duas formas, o `mkbootimg.sh` cria a pasta no FAT
  (o glob `dtbs/*.dtb` não entra em subdiretório) e o `boot.cmd` tenta
  `dtbs/${fdtfile}`, `dtbs/board.dtb` e `board.dtb`.
- **`extlinux.conf` era gerado só se faltasse** (`if [ ! -f ]`) e vivia em
  `BINARIES_DIR`, que sobrevive ao build incremental: continuou com
  `root=/dev/mmcblk1p2` depois de o `boot.cmd` já usar PARTUUID. Agora é
  regerado sempre, como o `boot.scr`.
- **Raiz por `root=PARTUUID`, não por `/dev/mmcblk1p2`.** A mesma imagem gravada
  no eMMC vira `mmcblk0p2` (e no 2º slot de cartão, `mmcblk2p2`), e o kernel
  pararia em `Waiting for root device`. A assinatura MBR passou a ser **fixa**
  (`disk-signature = 0x47494f43` no `genimage.cfg`), o que dá
  `PARTUUID=47494f43-02` (slot A) e `-03` (slot B) — verificado no `.img`
  (bytes 0x1b8 e `blkid`: `ID_PART_TABLE_UUID=47494f43`).
- **`saveenv` podia gravar no eMMC do aparelho de terceiro** (ver abaixo).
- **"Não apareceu nada": o aparelho estava bootando o firmware de fábrica.**
  Diagnóstico sem abrir o aparelho, pelos descritores USB do console ligado ao
  PC: `manufacturer=ROCKNIX, product=x35h Handheld Device` — ou seja, o
  bootloader do **eMMC** ganhava do cartão. `1d6b:0104` (gadget composto
  genérico) não identifica nada; as strings `manufacturer`/`product` sim.
  Documentado em `docs/BOOT-DEBUG.md` §1.5 e §6.
- **Fallback do DTB estava condicionado a `fdtfile` vazio.** O `boot.cmd` só
  usava `board.dtb` quando `fdtfile` **não** estava definido — mas `fdtfile`
  vem do ambiente do U-Boot de fábrica e quase nunca está vazio. Num aparelho
  diferente daquele do build (imagem da X55 no X35H) o `load dtbs/<placa>.dtb`
  falhava, o fallback não acontecia, o `fdt_addr_r` ficava com lixo, o `booti`
  falhava e o U-Boot seguia para o eMMC. Agora o fallback reage ao **resultado
  do load** (`giochi_fdt_ok`).
- **`saveenv` podia gravar no eMMC do aparelho de terceiro.** O nosso
  `boot.scr` também roda sob o U-Boot de fábrica (que varre o cartão atrás de
  `boot.scr`); ali `saveenv` escreve no eMMC do usuário. Agora o script detecta
  quem o carregou (`giochi_mmc`, definido pelo nosso `BOOTCOMMAND` antes do
  `source`) e, sob bootloader de fábrica, roda **sem estado**.
- `tools/docker-build.sh` passou a repassar `--fallback-dts`, `--debug`,
  `--vendor-uboot` e `--check` ao `build-image.sh` (antes morria em "opção
  desconhecida", e a única saída era sair do container).
- **Log de boot na sdcard e a caçada ao "boot sem nada na tela"**
  (`docs/BOOT-DEBUG.md`). Quatro defeitos apareceram; três impediam o boot ou o
  próprio log:
  - **U-Boot carregava o `boot.scr` de `mmc 0` (eMMC)**, não do cartão. Em
    `rk3566-powkiddy-x55.dts` o SD é `mmc1` e a X55 não tem eMMC soldado: o
    `load` falhava, o kernel nunca iniciava e a tela ficava preta sem uma linha
    de log. `BOOTCOMMAND` agora sonda `1` e `0`, e `uboot.env` foi para `1:1`
    (em `0:1` o `saveenv` falhava e o rollback A/B não gravava).
  - **`LABEL=GIOCHI_USER` no `fstab` não funcionava**: faltava
    `CONFIG_FEATURE_MOUNT_LABEL` no BusyBox (medido no `.config` do 1.36.1).
    Sem ela o `mount` trata a string como nome de arquivo, o `mount -a` falha e
    `/user` — a única partição gravável — nunca é montada.
  - **`boot.scr` de 27/10 numa imagem de 31/10**: o artefato é gerado nas
    etapas de build do `host-uboot-tools`, que não é reconstruído quando o
    `boot.cmd` muda. Agora o `post-image.sh` regenera sempre com o `mkimage`.
  - **Scripts de init de pacote brigando com os nossos**: `S01syslogd`/
    `S02klogd` subiam um segundo `syslogd` sem `-O` e roubavam `/dev/log` (o log
    no cartão parava de crescer sem erro); `S50dropbear` subia **SSH em todas as
    interfaces, por padrão**, anulando o adendo 4.6.1. Os três são removidos no
    `post-build.sh`, com o motivo ao lado de cada um.
- **Quatro defeitos que derrubavam o build no ÚLTIMO passo**, todos com mensagem
  que não aponta o culpado, todos agora com guarda no lint:
  - `device_table.txt` usava `g` (de "grupo") no campo de *tipo de arquivo*;
    o tipo válido é `d`. O `makedevs` morria em
    `Unsupported file type g` → `squashfs.mk`. Guarda:
    `tools/check-device-table.sh`.
  - Os dois `genimage*.cfg` declaram tabela MBR (`dos`) e as partições usavam
    `partition-type-uuid`, que só existe em GPT: o `genimage` abortava com
    `'partition-type-uuid' is only valid for gpt and hybrid`. Trocado pelo byte
    hexadecimal (`0x0c` FAT32 LBA, `0x83` Linux) e removido o `gpt-location`,
    que também só vale para GPT.
  - O `extlinux.conf` saía com `root=PARTUUID=%PARTUUID%` **literal**: essa
    expansão não existe no genimage nem em nenhum script do repositório, então
    o kernel não montaria o rootfs. Agora usa o mesmo
    `/dev/mmcblk<N>p2` do `boot.cmd`.
  - O `post-image.sh` terminava num `[ -f "$f" ] && echo ...` dentro de um
    `for`: o status 1 do `&&` que não executa virava o exit code do script e o
    build acusava `target-post-image Error 1` **com a imagem pronta**. Agora
    usa `if` e termina com `exit 0`.
  Os três últimos são cobertos por `tests/test_image_layout.py` (12 checagens,
  testadas ao contrário: reintroduzir cada bug faz a suíte falhar).
- **`giochi-init` não compilava** (`'rk3566' undeclared`,
  `%s expects char* but has double`): os defines de texto
  (`-DGIOCHI_SOC='"rk3566"'`) estavam dentro de `EXTRA_CFLAGS="..."` e as aspas
  atravessavam **duas** camadas de shell, então o compilador recebia o
  *símbolo* em vez da string. As aspas passaram para o `Makefile` da raiz
  (`DEFS += -DGIOCHI_SOC='"$(GIOCHI_SOC)"'`) e os `.mk` passam valores crus.
  `tools/check-image-contents.sh` proíbe `-DGIOCHI_SOC=` nos `.mk`.
- **`docker-build.sh` ignorava variáveis do host**: só `BUILDROOT_DIR`,
  `BR2_DL_DIR` e `JOBS` chegavam ao container, então `GIOCHI_NO_COMPRESS=1` era
  descartada em silêncio (o build comprimia mesmo assim). Agora `-e` para
  `GIOCHI_NO_COMPRESS` e `GIOCHI_MIN_FREE_GB`.
- **`giochi-games` falhava ao instalar** porque `install-games.sh` recebia o
  manifesto com caminho relativo; o `.mk` agora passa `$(@D)/`.
- **Cópia recursiva de 66 GB: a causa real de o build travar e o CI "morrer sem
  mensagem".** Os pacotes locais (`giochi-assets`, `giochi-init`,
  `giochi-launcher`, `giochi-musicd`, `giochi-tools`, `giochi-games`) usavam
  `SITE = $(BR2_EXTERNAL_GIOCHI_PATH)/..` com `SITE_METHOD = local`. O rsync do
  Buildroot copiava a raiz do repositório — que contém `dist/`, onde o próprio
  build está sendo escrito — para dentro de si mesma, recursivamente (medido:
  **66 GB** em `giochi-assets-1.2`). O build travava em `Syncing from source
  dir` e o disco acabava sem mensagem útil.
  Agora `tools/build-image.sh` monta um snapshot só das fontes (~9 MB) em
  `dist/<id>/pkg-src` e exporta `GIOCHI_PKGSRC`; todo `.mk` de pacote local usa
  `SITE = $(if $(GIOCHI_PKGSRC),$(GIOCHI_PKGSRC),<raiz>)`.
  `tools/check-image-contents.sh` (em `make lint`) recusa `SITE_METHOD = local`
  sem `GIOCHI_PKGSRC` e exige o `export` no `build-image.sh`.
- **CRLF no índice**: `AGENTS.md` e `device/x55/layout/library.json` estavam
  commitados com CRLF apesar do `* text=auto eol=lf`, então a edição seguinte
  saía como diff de 2 mil linhas. Normalizados para LF, e
  `tools/check-tracked.sh` passou a falhar em `i/crlf` (via
  `git ls-files --eol`, portátil — `$'\r'` é bashism e dava falso positivo com
  `sh`).
- `tools/docker-build.sh` não exportava `BUILDROOT_DIR` para dentro do
  container, então falhava antes de compilar; e o Dockerfile não tinha
  `xz-utils`, que o `post-image.sh` usa para comprimir o `.img`.
- **A imagem não cabia no GitHub**: o `sdcard.img` tinha **968 MB** e era
  publicado cru. Agora o `post-image.sh` gera o artefato comprimido com
  **`xz -9` e janela de 192 MiB**, e o `images.yml` publica só o `.xz` +
  `.sha256`. Medido no layout real: **936 MB → 27,8 MB**. A janela grande não é
  acaso — as partições A/B guardam o **mesmo** SquashFS e, com a janela padrão
  do `xz -9` (64 MiB), a segunda cópia, 160 MB adiante, é incompressível
  (55,1 MB contra 27,8 MB). O `.img` cru continua em `dist/`;
  `GIOCHI_NO_COMPRESS=1` desliga.
- **O `genimage` abortava no último passo depois de 1–3 h de build**: o
  `genimage.cfg` declarava **8 MiB** para a região raw do
  `u-boot-rockchip.bin`, mas o artefato medido tem **8,97 MiB** (o `u-boot.itb`
  começa em 6 MiB). Região agora com 16 MiB, partição de boot de 128 → **80
  MB** começando em 24 MiB, e o `post-image.sh` **confere o tamanho de cada
  artefato contra o cfg antes de chamar o `genimage`**, dizendo quantos MB cada
  um ocupa. `tests/test_image_layout.py` pega a mesma regressão em segundos.
- **Tamanho de partição em dois lugares**: a partição C vinha de
  `USER_SIZE_MB` (script) e as outras do `genimage.cfg`. Mudar um sem o outro
  criava `user.ext4` maior que a partição. O cfg agora é **fonte única**
  (`part_size()`/`to_mb()` no `post-image.sh`), com teste que falha se o script
  voltar a ter um default próprio.
- **Rootfs 34,9 → 28,0 MB sem tirar funcionalidade**: `SQUASHFS_BS_256K` +
  `SQUASHFS_EXTREME_COMP` (zstd nível 22) nos dois templates, e o
  `post-build.sh` remove os catálogos gettext (`usr/share/locale`, 9,5 MB crus)
  que nenhuma peça do console usa. O que o `target-finalize` do Buildroot já
  remove (`usr/include`, docs, `man`, `*.a`, `strip` de binários/.ko) **não** é
  repetido no `post-build.sh` — conferido no `Makefile` do Buildroot 2024.02.
- **O build de imagens morria no meio, sem mensagem**: o runner `ubuntu-24.04`
  tem ~14 GB livres e o build da X55 usa ~16–18 GB. O log simplesmente terminava
  numa linha de compilação (sem `No space left`, porque o próprio log não podia
  mais ser gravado) e os passos de limpeza nem rodavam. O `images.yml` agora
  libera ~20–25 GB removendo SDKs pré-instalados antes de compilar, deixou de
  usar ccache (que copia cada objeto) e o `build-image.sh` recusa começar com
  menos de `GIOCHI_MIN_FREE_GB` (padrão 15) livres, informando o espaço medido.
- **U-Boot não compilava em máquina limpa**: `make tools` compila
  `mkeficapsule`, que inclui `uuid/uuid.h` e `gnutls/gnutls.h`. Passava no CI só
  porque a imagem do runner tem `uuid-dev`/`gnutls-dev`; numa imagem limpa
  falhava. Declarados `BR2_TARGET_UBOOT_NEEDS_UTIL_LINUX` e
  `BR2_TARGET_UBOOT_NEEDS_GNUTLS` (o Buildroot então constrói `host-util-linux`
  e `host-gnutls` e passa os headers em `HOST_CFLAGS`).
- `tools/check-tracked.sh` reprovava depois do primeiro build real: o git
  **cita** caminhos não-ASCII (`"…/Prøve.flac"`), e o `case` não reconhecia o
  prefixo `dist/*`. Corrigido com `core.quotePath=false` e exclusão por
  pathspec — de ~800 mil arquivos varridos (minutos) para ~1 s.
- **Build de imagens morria no kernel**: `BR2_LINUX_KERNEL_USE_DEFCONFIG=y` com
  `BR2_LINUX_KERNEL_DEFCONFIG="defconfig"` fazia o Buildroot rodar
  `make defconfig_defconfig` (ele acrescenta o sufixo ao valor), e o kernel
  respondia *"Can't find default configuration
  arch/arm64/configs/defconfig_defconfig"*. Trocado por
  `BR2_LINUX_KERNEL_USE_ARCH_DEFAULT_CONFIG=y`, que é a opção documentada para
  arquiteturas com um único defconfig (ARM64).
- **Nada gerava o `boot.scr`** que o `mkbootimg.sh` exige para montar a partição
  A — o build morreria no `post-image` depois de compilar tudo. Adicionados
  `BR2_PACKAGE_HOST_UBOOT_TOOLS_BOOT_SCRIPT` (com `boot.cmd` como fonte),
  `BR2_PACKAGE_HOST_MTOOLS` e `BR2_PACKAGE_HOST_DOSFSTOOLS`; os dois últimos
  também removem a dependência silenciosa de `apt install dosfstools mtools` na
  máquina de build (o Buildroot põe `$(HOST_DIR)/bin` no `PATH`).
- `post-image.sh`: ausência de `boot.vfat` passou de aviso a **erro fatal**, com
  as dicas do que configurar — antes o `genimage` falhava depois com
  "cannot find image boot.vfat", sem apontar a causa.
- `tools/check-buildroot-symbols.py` valida também **valores**, não só a
  existência do símbolo (escolha de config do kernel, sufixo `_defconfig`
  duplicado), e roda em `make lint` mesmo sem `BUILDROOT_DIR`.
- **Ferramenta de PC instalada na imagem**: `giochi-tools` copiava
  `giochi-pack.sh` para `/usr/bin` do rootfs. É script de autor/publicador e,
  no console, falhava em toda invocação — o `tar` do BusyBox não aceita
  `--format=ustar`, `--owner/--group` nem `-r`, e não há `jq` na imagem. A
  imagem agora leva só o que roda no aparelho (`giochi-verify`,
  `giochi-extract`, `giochi-thumbgen`); `tools/check-image-contents.sh` (novo,
  em `make lint`) barra a regressão nos dois sentidos, e `giochi-pack.sh`
  recusa rodar com tar não-GNU com mensagem explícita.
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
- Build multi-aparelho: `device/*/device.conf` como fonte única
  e defconfigs gerados por `tools/gen-defconfigs.sh` (X55, X35S, X35H,
  RK3566 genérico, RK3326 genérico).
- `tools/build-image.sh` com preflight (`--check`), build por aparelho, `--all`,
  DTS externo (`GIOCHI_DTS_PATH`/`GIOCHI_DTB_PATH`) e fallback de teste
  (`--fallback-dts`).
- Layout próprio da X55 (1280×720 após rotação) em `device/x55/layout/`.
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
