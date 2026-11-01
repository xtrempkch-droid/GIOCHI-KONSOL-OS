# `apps/` — aplicações que não são o núcleo do sistema

O núcleo (supervisor, Launcher, daemons, ferramentas do console) mora em
`src/` e vai para a imagem. Aqui ficam as aplicações do **PC**: o que prepara
conteúdo para o console e nunca entra no rootfs.

```text
apps/
├── forge/          giochi-forge  (Python)  perfil, classificação §4.5.3,
│                                            empacotamento e assinatura
│   └── giochi-forge               atalho: apps/forge/giochi-forge check ...
└── store/          giochi-store  (Python)  catálogo da Loja + news.json +
                                             instalação no cartão
    └── giochi-store               atalho: apps/store/giochi-store catalogo ...
```

## Por que separar

- **Nada daqui vai para a imagem.** `tools/check-image-contents.sh` (no
  `make lint`) falha nos dois sentidos: ferramenta de PC instalada no rootfs e
  binário obrigatório do console faltando (AGENTS.md §3.12).
- **Dependência diferente.** Estes apps precisam de Python 3 no PC; o console
  não tem interpretador. Misturar com `src/` fazia parecer que dependiam do
  mesmo toolchain.
- **Responsabilidade clara.** `src/` = o que roda no aparelho; `apps/` = o que
  roda no PC do mantenedor.

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
(`src/launcher/main.cpp`, `carregar_novidade`).

Regra que vale para os dois apps: **recusa sempre com o motivo**. Um `.giochi`
com `arch` diferente de `aarch64` é recusado como erro (ADR-13 proíbe
emulação) e o texto diz o que aconteceu — nunca um "falhou" genérico.
