# Bootsplash

O logo (`docs/logo.jpg`) já é usado no splash. O suporte está **completo e
ligado** na camada do Launcher, e **pré-pronto (comentado)** nas camadas
U-Boot e kernel, conforme decidido nesta fase.

## Como funciona hoje (v1.2)

```
U-Boot ──(sem splash por padrão)──▶ kernel ──▶ giochi-init ──▶ Launcher
                                                                │
                                        primeiros ~0.75 s: bootlogo.rgba
```

O Launcher desenha `bootlogo.rgba` nos primeiros `GIOCHI_SPLASH_FRAMES` frames
(padrão 45 ≈ 0,75 s a 60 fps) sobre o fundo do tema. O splash some sozinho ao
fim da contagem ou no primeiro botão/tecla.

### Assets gerados

| Arquivo | Tamanho | Consumidor |
|---|---|---|
| `assets/bootlogo/bootlogo.rgba` | 640×349 RGBA (~893 KB) | Launcher |
| `assets/bootlogo/bootlogo.json` | dimensões + formato | Launcher |
| `assets/bootlogo/<aparelho>/splash.bmp` | painel nativo: 640×480 ou 1280×720 | U-Boot (quando ativado) |

Gerar/atualizar:

```sh
make bootlogo          # requer python3-pil
```

O `post-build.sh` instala o `.rgba`/`.json` em `/usr/share/giochi/`; o
`post-image.sh` e o `mkbootimg.sh` copiam `splash.bmp` para a partição A quando
ele existe.

### Controles em runtime

| Variável | Efeito |
|---|---|
| `GIOCHI_NO_SPLASH=1` | não desenha splash |
| `GIOCHI_SPLASH_FRAMES=N` | duração em frames (0 = desligado) |
| `GIOCHI_BOOTLOGO=/caminho.rgba` | usa outro arquivo |
| `GIOCHI_BOOTLOGO_DIR=/dir` | outro diretório base |

O splash é **best-effort**: arquivo ausente, JSON inválido ou tamanho
inconsistente apenas geram log em nível `debug`/`warn` — o Launcher segue
normal. VRAM: 893 KB, dentro do teto de 4 MB da §4.1.6.

## Ativar o splash do U-Boot (futuro)

O código e os assets já existem; falta validar a inicialização de vídeo do
U-Boot no SoC. Para ligar:

1. Em [`buildroot/board/giochi/uboot.fragment`](../buildroot/board/giochi/uboot.fragment),
   descomente o bloco `Bootsplash (PRE-PRONTO)`.
2. Em [`buildroot/board/giochi/boot.cmd`](../buildroot/board/giochi/boot.cmd),
   descomente o `load ... ${splashimage}`.
3. Rode `make bootlogo` e um build novo.

Requisitos e ressalvas:

- o BMP tem de casar com o painel (o gerador usa `GIOCHI_PANEL`/`GIOCHI_ROTATION`
  de cada `devices/*.conf`; X55 sai em 1280×720);
- sem `CONFIG_VIDEO` o U-Boot ignora `splashimage` — não quebra o boot;
- X35S/X35H só ligam o painel no U-Boot com o DTS externo carregado;
- o formato gerado é BMP 24-bit não comprimido, aceito por `CONFIG_VIDEO_BMP_*`.

## Splash do kernel (não usado)

`CONFIG_LOGO_CUSTOM` exigiria compilar uma imagem 80×80 em
`drivers/video/logo/logo_custom_clut224.ppm` — patch local no tree do kernel,
sem ganho sobre U-Boot + Launcher e com custo de boot. O bloco correspondente
está comentado em [`linux.fragment`](../buildroot/board/giochi/linux.fragment)
com a decisão registrada.

## Trocar o logo

```sh
# substitua docs/logo.jpg e regenere
make bootlogo
```

O gerador centraliza o logo com 6% de margem sobre `#0F0F0F` (fundo do tema),
fazendo *aspect-fit* — sem esticar. Para outro fundo, ajuste `BACKGROUND` em
[`tools/make-bootlogo.py`](../tools/make-bootlogo.py).
