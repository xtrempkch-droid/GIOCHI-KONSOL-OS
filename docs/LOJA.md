# Loja de jogos: fontes avaliadas e contrato do `.giochi`

Este documento **substitui** três notas separadas que existiam aqui
(`SUPERTUX.md`, `ITCHIO.md`, `STORE-SOURCES.md`). As fontes foram avaliadas e a
decisão está tomada; o que fica registrado são os números que sustentam a
decisão, para ninguém refazer a pesquisa.

**Decisão (2026-10-31):** a Loja **não** é um catálogo de terceiros. Ela lê uma
pasta local (o projeto baixado do GitHub, ou vindo por USB) e produz o
**`.giochi` localmente**.

---

## 1. Por que não usar um catálogo de terceiros

Toda fonte cai numa de três classes, e só as duas primeiras servem:

| Classe | Exemplo | Serve? |
|---|---|---|
| Binário **aarch64** pronto | PortMaster, Alpine | **sim** |
| Projeto/fonte para recompilar | repositório Godot | sim, com trabalho |
| Catálogo **sem arquitetura** | itch.io | **não** |

O caso do itch.io fecha a questão: o modelo de dados oficial dele
(`go-itchio`, a lib do `butler` e do app oficial) declara arquitetura como um
enum de **três** valores — `all`, `386`, `amd64`. **Não existe `arm64`.** Logo
não há como filtrar o que roda em aarch64 antes de baixar; e a oferta ARM64
descobrível é de **~18 jogos** num catálogo de 1,7 milhão. Além disso, "grátis"
no itch.io é custo zero, não licença livre: os ToS autorizam distribuir o
conteúdo apenas *"as permitted through the functionality of the Service"*, e
*"Publishers retain all ownership rights"*.

## 2. Os números que decidem (medidos)

Alvo: **aarch64 + musl**, 700 MB de orçamento de UI.

| Fonte | Oferta | Licença | Tamanho típico |
|---|---|---|---|
| **PortMaster** | ≥488 ports; **430 MB de `.aarch64`** (binário pronto) | MIT (ferramenta), **por port** | só o do port |
| **Alpine aarch64** | 24.049 pacotes, **linkados a musl** | por pacote | 3–12 MB |
| **TIC-80** | engine arm64 de 4,9 MB + cartuchos | **MIT** | **cartucho 18–100 KB** |
| Solarus, Minetest | **já no Buildroot** | GPL-2.0 / LGPL | 5–9 MB |
| SuperTux / SuperTuxKart | — | GPL-3.0 | **244 MB** / **717 MB** de dados |

O Alpine é o achado que importa: usa **musl**, a mesma libc do ADR-03, e por
isso serve tanto de binário quanto de **receita** (`APKBUILD` = o mapa do que o
Buildroot precisa). Oferta permissiva útil: `easyrpg-player` 3,4 MB (MIT),
`love` 3,8 MB (Zlib), `retroarch` 11,5 MB + 42 cores (40 permissivos).

O TIC-80 é o melhor uso de armazenamento que existe: **cem jogos ≈ 10 MB**. É o
oposto do SuperTux, onde **244 MB de dados acompanham 5 MB de binário** — o
problema é dado, não código.

## 3. O contrato do `.giochi` (o que a Loja produz)

O pacote já está definido e o ferramental existe:

```console
$ apps/forge/giochi-forge build --project DIR --out jogo.giochi --key chave.pem
```

Layout exigido (`tools/giochi-pack.sh`, ADR-11):

```
metadata.json  ->  capa.png  ->  thumb.png (opcional)  ->  bin/
```

`schemas/metadata.schema.json` obriga, entre outros:

| Campo | Regra |
|---|---|
| `arch` | `aarch64` (**único** valor; ADR-13 proíbe emulação) |
| `runtime` | `godot` \| `retroarch` \| `proton` |
| `entrypoint` | `bin/<algo>`, sem `..` |
| `version` | semver |

Assinatura Ed25519 + SHA-256 (ADR-01); o `giochi-verify` do console confere.

## 4. O comando `store` (o que já funciona)

A Loja só pode **produzir** o `.giochi` no PC — o rootfs do console não tem
toolchain (nenhum `BR2_PACKAGE_GCC/MAKE/CMAKE`) e tem 1–2 GB de RAM. O
`giochi-forge store` lê uma pasta de projetos baixados e empacota o que já dá:

```console
$ apps/forge/giochi-forge store --src ~/baixados --out ./pacotes \
      --key chave.pem --device rk3566-2gb --install /media/eu/GIOCHI_USER/games

dispositivo: manual-rk3566-2gb (2048 MB)
  gerado  Jogo Bom        pacote            jogo-bom.giochi  10 KB
  pulado  jogo-godot      projeto Godot     precisa export aarch64; o template
                                            oficial e glibc e o rootfs e musl
  pulado  jogo-fonte      fonte             precisa cross-compile para aarch64/musl
  pulado  jogo-quebrado   pacote incompleto falta capa.png, bin
  erro    jogo-x86        pacote            arch 'x86_64' nao e aarch64 (ADR-13)

  5 pasta(s): 1 gerado(s), 3 pulado(s), 1 com erro
```

O que o relatório **sempre** diz é o motivo da recusa — nunca um "falhou"
genérico. Cada pasta recebe um `kind`:

| `kind` | Empacota? | O que precisa |
|---|---|---|
| `pacote` (`metadata.json` + `capa.png` + `bin/`) | **sim** | nada |
| `pacote-incompleto` | não | os arquivos que faltam (nomes no relatório) |
| `projeto-godot` | não | export aarch64 com template musl (ADR-03) |
| `fonte` | não | cross-compile para aarch64/musl |
| `desconhecido` | não | um build system reconhecível |

Regras que valem no `store`:

- `arch` diferente de `aarch64` é **erro**, não aviso (ADR-13);
- com `--device`, um `forge.json` que não cabe no aparelho **barra** o pacote;
- `--dry-run` relata sem escrever nada;
- se a assinatura falhar, o pacote é **descartado** — um `.giochi` sem assinatura
  não passa no `giochi-verify` do console, e deixá-lo no diretório é convite a
  gravar engano;
- não desce dois níveis de pasta (um zip do GitHub extrai como `<repo>-main/`;
  descer mais acharia "projeto" dentro de `examples/` de outro projeto).

## 5. O que falta para a Loja existir

1. **`runtime: "native"`** no schema — hoje um binário aarch64 compilado do
   fonte não tem como ser declarado. Afeta `schemas/metadata.schema.json`,
   `assets/default-games.json` e `src/launcher/metadata.cpp`.
2. **O app `giochi-store`** no aparelho: hoje `APP:giochi-store` responde
   `ERR launch_failed` (ver [APPS.md](APPS.md)). O `store` do §4 roda no PC e
   produz os pacotes; falta o lado do console — lista o que está em
   `/user/games`, mostra capa/título e lança. **Não compila** (não há
   toolchain), só instala e organiza.
3. **Empacotar no aparelho**: `giochi-pack.sh` exige GNU tar (`--format=ustar`,
   `--owner/--group`, `-r`), que o BusyBox não tem — por isso ele é ferramenta
   de PC (AGENTS.md §3.12). Uma Loja que empacota **no console** precisa de
   empacotador próprio, sem essas opções.
4. **O template do Godot em musl** (ADR-03): o template oficial aarch64 é ELF64
   `e_machine=0xB7`, mas **glibc** (`GLIBC_2.17…2.28`), e o rootfs é musl.
   Recompilar contra musl destrava este caso **e** os três jogos padrão.

## 6. Como reconferir

```sh
# o enum sem ARM (a prova de por que o itch.io nao serve)
curl -s https://raw.githubusercontent.com/itchio/go-itchio/master/types.go | grep -A8 "type Architectures string"

# Alpine aarch64: licenca e tamanho instalado por pacote (campos P:/L:/I:/D:)
curl -sLO https://dl-cdn.alpinelinux.org/alpine/v3.20/community/aarch64/APKINDEX.tar.gz

# PortMaster: quantos ports e quanto e binario aarch64 (arvore ~20 MB: SALVE EM ARQUIVO)
curl -sL "https://api.github.com/repos/PortsMaster/PortMaster-New/git/trees/main?recursive=1" -o pm.json

# Buildroot tem o pacote? (sondar por CAMINHO -- listar package/ trunca em 1000)
curl -s -o /dev/null -w '%{http_code}\n' \
  https://api.github.com/repos/buildroot/buildroot/contents/package/solarus?ref=2024.02.x
```

Armadilhas de medição que custaram tempo e estão no AGENTS.md: API de conteúdo
do GitHub trunca em 1000 entradas (§3.49); API do itch.io devolve **HTTP 200**
para erro de autenticação (§3.55); e o `APKINDEX` usa `<campo>:<valor>`, não
`chave=valor` — com o separador errado o parse devolve **zero pacotes sem
erro**.
