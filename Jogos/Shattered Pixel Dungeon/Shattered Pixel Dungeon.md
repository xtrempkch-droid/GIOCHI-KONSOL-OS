# Shattered Pixel Dungeon — fora da lista de jogos padrão

Nota de intenção preservada do mantenedor. **Este jogo não é jogo padrão do
sistema** e não está no rootfs.

- **Fonte:** <https://github.com/00-evan/shattered-pixel-dungeon>
- **Licença:** GPL-3.0 (confirmado na API do GitHub em 2026-10-27).

## Por que ficou fora

GPL-3.0 é *copyleft*: distribuir o jogo dentro da imagem obrigaria a abrir e
manter a licença de tudo que se liga a ele. Não é problema do jogo — é decisão
de licenciamento do sistema inteiro. A regra do repositório é: **jogo padrão só
com licença que permita redistribuição fechada** (domínio público, permissão
dos autores ou código próprio).

A decisão está registrada em dois lugares e as duas precisam concordar:

- [assets/default-games.json](../../assets/default-games.json) → seção
  `excluded`, com o motivo e o upstream;
- [docs/GAMES.md](../../docs/GAMES.md) → tabela de excluídos.

`tools/check-default-games.py` (em `make lint`) recusa este id na seção
`games`, e `tests/test_default_games.py` cobra que ele **continue** em
`excluded`, para ninguém reverter a decisão sem saber.

## O plano, quando houver decisão

Anotado pelo mantenedor, sem prazo:

- reestruturar o jogo, com acabamento próximo de **Zenonia** — **sem ferir
  direitos autorais**;
- **multi-idiomas é obrigatório** (mesmo padrão do
  [Colossal Cave](../Colossal%20Cave%20Adventure/Colossal%20Cave%20Adventure.md),
  que tem os textos todos em `scripts/i18n/locale.gd`).

Nada foi portado: não há projeto Godot, código ou asset nesta pasta.
