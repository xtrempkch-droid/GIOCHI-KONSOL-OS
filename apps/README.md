# `apps/` — os aplicativos do projeto

Aqui moram as **aplicações**: cada uma tem o seu próprio ponto de entrada e
pode ser mexida sem tocar no resto. `src/` é o núcleo compartilhado (as
bibliotecas comuns, o supervisor e os daemons do sistema).

O critério é simples: **se o diretório tem um `main`, é app e mora aqui**. Isso
vale tanto para o que roda no aparelho quanto para o que roda no PC do
mantenedor.

```text
apps/
├── launcher/       giochi-launcher  (C++17 + SDL2)  a UI do console
│                                        layout, tema, overlay, evdev, render
├── forge/          giochi-forge     (Python)  perfil, classificação §4.5.3,
│                                              empacotamento e assinatura
│   └── giochi-forge                atalho: apps/forge/giochi-forge check ...
└── store/          giochi-store     (Python)  catálogo da Loja + news.json +
                                               instalação no cartão
    └── giochi-store                atalho: apps/store/giochi-store catalogo ...
```

## Console × PC

| App | Roda em | Vai para a imagem? |
|---|---|---|
| `launcher` | console | **sim** — é a UI; o pacote `giochi-launcher` compila `apps/launcher/*.cpp` |
| `forge` | PC | não — depende de Python 3 e GNU tar, que não existem no aparelho |
| `store` | PC | não — idem |

A guarda é `tools/check-image-contents.sh` (no `make lint`), que falha nos dois
sentidos: app de PC instalado no rootfs e binário obrigatório do console
faltando (AGENTS.md §3.12).

## Por que o Launcher está aqui

O Launcher é o app da interface do console — não é núcleo do sistema. Tê-lo em
`apps/` é o que permite trabalhar nele **sem** esbarrar em quem mexe no sistema
(`src/`) ou nos jogos (`Jogos/`): são frentes independentes, cada uma no seu
diretório, e o pacote do Buildroot compila só este.

No aparelho o binário instalado é `/usr/bin/giochi-launcher`, mas o supervisor
procura primeiro `/apps/giochi-launcher` (que é `/user/apps`, gravável). Ou
seja: dá para **substituir a UI no cartão** sem regravar o sistema
([docs/LAUNCHER-APP.md](../docs/LAUNCHER-APP.md)).

## `apps/forge`

```sh
apps/forge/giochi-forge profile --model "Anbernic RG351V"
apps/forge/giochi-forge check --project "Jogos/Spacewar/godot"
apps/forge/giochi-forge build --project DIR --out jogo.giochi --key chave.pem
apps/forge/giochi-forge keys --out keys/
apps/forge/giochi-forge store --src ~/baixados --out ./pacotes --key chave.pem
```

Equivalente a `PYTHONPATH=apps/forge python3 -m giochi_forge ...`.

## `apps/store`

Produz o que o Launcher consome e instala no cartão:

```sh
# catalogo + novidades da Home (news.json)
apps/store/giochi-store catalogo --src ./pacotes --out /media/eu/GIOCHI_USER/.cache

# onde o console está visível (cartão no leitor OU MTP pelo cabo)
apps/store/giochi-store destinos

# copia, descobrindo o destino e conferindo o sha256 NO DESTINO
apps/store/giochi-store instalar --src ./pacotes
# com os dois destinos presentes:
apps/store/giochi-store instalar --src ./pacotes --preferir mtp
# caminho explícito (qualquer ponto de montagem, inclusive MTP do gvfs):
apps/store/giochi-store instalar --src ./pacotes --games /run/media/eu/GIOCHI_USER/games

# so' olhar
apps/store/giochi-store listar --src ./pacotes
```

O `news.json` fecha o buraco registrado em [docs/APPS.md](../docs/APPS.md): o
Launcher lia `/user/.cache/news.json` com *fallback* de dicas compiladas porque
**ninguém escrevia** esse arquivo. Agora ele é escrito aqui e lido na Home
(`apps/launcher/main.cpp`, `carregar_novidade`).

Regra que vale para todos os apps: **recusa sempre com o motivo**. Um `.giochi`
com `arch` diferente de `aarch64` é recusado como erro (ADR-13 proíbe
emulação) e o texto diz o que aconteceu — nunca um "falhou" genérico.
