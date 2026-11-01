# DTS de aparelho sem suporte upstream

Esta pasta guarda DTS **completos** (não patches) para aparelhos ou variantes
que o kernel mainline não cobre.

## Uso

```sh
GIOCHI_DTS_PATH=/caminho/placa.dts tools/build-image.sh <aparelho>
# ou, para os que já estão aqui:
tools/build-image.sh x35s                       # usa dts/rk3566-powkiddy-x35s.dts
```

O `build-image.sh` injeta o caminho em `BR2_LINUX_KERNEL_CUSTOM_DTS_PATH` e o
Buildroot **copia o arquivo para `arch/arm64/boot/dts/` sem subdiretório**,
compilando pelo **nome do arquivo**. Consequências práticas:

- o arquivo **tem de se chamar igual** ao DTS que a config espera
  (`BR2_LINUX_KERNEL_INTREE_DTS_NAME`), senão o `make` morre em
  `No rule to make target '...dtb'`;
- ele **substitui** o DTS in-tree: os patches de DTS
  (`patches/linux/0002-…`) **não** se aplicam a ele. Copie o DTS já com as
  correções locais dentro (TRNG ligado, etc.).

## Cartão SD marginal (`rk3566-powkiddy-x55-sd-lento.dts`)

O SDXC de 116 GB usado nesta bancada devolve `-EIO` no tuning do UHS:

```
kernel: mmc1: tuning execution failed: -5
        mmc1: error -5 whilst initialising SD card
U-Boot: <log para depois do dtb, sem chegar em "Image carregada">
```

É o cartão, não a imagem (o `tools/verify-card.py` confirma byte a byte). O
DTS `rk3566-powkiddy-x55-sd-lento.dts` tira o `sd-uhs-sdr104` e limita o
`&sdmmc0` a 50 MHz (high-speed), que todo cartão suporta. Custo medido: o
`Image` de 47 MB leva ~1,9 s em vez de ~0,6 s por boot.

```sh
GIOCHI_DTS_PATH=buildroot/board/giochi/dts/rk3566-powkiddy-x55-sd-lento.dts \
    tools/build-image.sh x55
```

Se o aparelho tem cartão bom, **não use** — o padrão (SDR104) é 3× mais
rápido. Ver `AGENTS.md` §3.65 e `docs/BOOT-DEBUG.md` §7.

> **Por que não é um patch de DTS.** Tentei entregar isso como patch em
> `BR2_LINUX_KERNEL_PATCH` e foi um poço: a config não re-arma o pacote
> `linux`, forçar o re-patch esbarra no `.applied_patches_list` e em árvores
> com os hunks já aplicados (`Reversed ... Skipping patch`), e o
> `linux-headers` usa os **mesmos** patches. Um DTS completo pelo caminho que
> já existe não tem estado escondido.
