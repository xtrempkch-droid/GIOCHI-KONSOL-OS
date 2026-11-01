# `device/` — configuração por aparelho

Um diretório por aparelho, com **tudo** que é daquele aparelho. Antes disso o
mesmo dado vivia em duas árvores (`buildroot/board/giochi/devices/<id>.conf`
para o build e `assets/devices/<id>/` para o runtime), e as duas divergiam — a
mesma armadilha do `devices/*.conf` registrada em
[AGENTS.md §3.20](../AGENTS.md): o mesmo número em dois lugares.

```text
device/
├── README.md            este arquivo
├── <id>/
│   ├── device.conf      identidade, SoC, DTS, painel, U-Boot, GPU (FONTE ÚNICA)
│   ├── theme.json       tema padrão de fábrica (só o `theme_id`)
│   └── layout/          override de layout do painel (opcional)
│       ├── layout.json
│       ├── home.json
│       ├── library.json
│       └── music.json
```

Aparelhos hoje: `x55`, `x35s`, `x35h`, `rk3326-generic`, `rk3566-generic`.

## `device.conf` — o dado do build

Lido por `tools/gen-defconfigs.sh` (gera `buildroot/configs/*_defconfig`) e por
`buildroot/board/giochi/post-build.sh` / `post-image.sh`. Chaves que importam:

| Chave | Uso |
|---|---|
| `GIOCHI_ID`, `GIOCHI_NAME` | identidade; `GIOCHI_ID` casa com o nome da pasta |
| `GIOCHI_SOC`, `GIOCHI_GPU_DRIVER`, `GIOCHI_GL_*` | classificação de engine (`tools/check-engines.py`) |
| `GIOCHI_DTS`, `GIOCHI_DTS_UPSTREAM`, `GIOCHI_DTB_FILE` | DTS/DTB da imagem |
| `GIOCHI_PANEL`, `GIOCHI_ROTATION` | tamanho físico e giro do painel |
| `GIOCHI_LAYOUT` | qual pasta de `layout/` o runtime usa |
| `GIOCHI_UBOOT_*` | U-Boot mainline ou do vendor |

**Nunca edite `buildroot/configs/*_defconfig`**: eles são gerados. Mude o
`device.conf` e rode `make defconfigs`.

## `theme.json` — o tema de fábrica

Só o `theme_id`, que aponta para o catálogo em
[`assets/themes/`](../assets/themes). O menu **Sistema → Tema** alterna entre
todos os temas e grava a escolha em `/user/settings.json` (`theme_id`), que tem
precedência sobre este arquivo. Ver [docs/TEMAS.md](../docs/TEMAS.md).

## `layout/` — o override do painel

O layout base (`assets/*.json`) é desenhado para **640×480**. Aparelho com
painel diferente ganha um override com os números do seu painel. A regra do
perfil é **×1,5 para todo inteiro**, cobrada por
[`tests/test_device_scale.py`](../tests/test_device_scale.py): campo novo que
não seja `round(base × 1,5)` (ou um dos especiais de painel, como `screen.*` e
`columns`) reprova o `make lint`.

O override é instalado **plano** em `/usr/share/giochi/` — o runtime não conhece
`device/`. Quem faz isso é o `post-build.sh`.

## Como adicionar um aparelho

```sh
mkdir -p device/meu-aparelho/layout
cp device/x55/device.conf device/meu-aparelho/device.conf
$EDITOR device/meu-aparelho/device.conf      # ID, SoC, DTS, painel...
make defconfigs                              # gera buildroot/configs/giochi_meu-aparelho_defconfig
make lint                                    # confere coerência e links
tools/build-image.sh meu-aparelho --check    # preflight, sem compilar
```

Se o painel **não** for 640×480, copie o `layout/` da X55 e ajuste; se for,
`GIOCHI_LAYOUT=default` e nenhuma pasta `layout/` é necessária.
