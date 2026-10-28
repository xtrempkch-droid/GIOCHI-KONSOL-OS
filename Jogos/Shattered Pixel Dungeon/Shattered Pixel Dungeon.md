# Shattered Pixel Dungeon — fora da lista de jogos padrão

Nota de intenção preservada do mantenedor. **Este jogo não é jogo padrão do
sistema** e não está no rootfs.

- **Fonte:** <https://github.com/00-evan/shattered-pixel-dungeon>
- **Licença:** GPL-3.0 — lido no `LICENSE.txt` do próprio repositório upstream
  (texto literal da GPL-3.0, sem licença extra para arte ou áudio).

## O que a GPL-3.0 realmente exige

Três coisas diferentes costumam ser confundidas, e a diferença decide o caso.
Texto conferido no `LICENSE.txt` do próprio repositório (GPL-3.0 literal):

1. **Usar não tem restrição.** §2: a licença "afirma explicitamente sua
   permissão ilimitada de **executar** o Programa não modificado". Rodar o jogo
   — e modificá-lo para si mesmo — não cria obrigação nenhuma.
2. **Crédito é obrigatório, mas é o menor dos deveres.** §4, para cópia
   literal: publicar o aviso de copyright, manter intactos os avisos e entregar
   uma cópia da licença. §5a, para versão modificada: aviso destacado de que o
   trabalho foi modificado, com data.
3. **O que a GPL cobra de verdade é copyleft + fonte.** §5c: o trabalho inteiro
   passa a ser GPL-3.0 para quem receber uma cópia. §6: quem entrega o binário
   tem que entregar a *Corresponding Source*. §10: não se pode impor restrição
   adicional. **Crédito não substitui nada disso.**

## Duas afirmações erradas que circularam aqui

**Errado 1 — "a GPL contamina o resto do sistema".** Não contamina. §5, último
parágrafo, é explícito: *"Inclusion of a covered work in an aggregate does not
cause this License to apply to the other parts of the aggregate"*. Um jogo GPL
ao lado do Launcher na mesma imagem é **agregado**: o Launcher, o `giochi-init`,
o kernel e o resto continuam sob a licença deles. (Esta nota afirmava o
contrário antes de 2026-10-28.)

**Errado 2 — "basta dar o crédito".** Crédito é condição necessária, não
suficiente: sem a fonte, sem a licença no trabalho inteiro e sem poder instalar
a versão modificada, a distribuição continua irregular.

## O impedimento real: anti-tivoization × raiz assinada

§6, *Installation Information*: entregando código-objeto GPL **em**, **com** ou
**especificamente para** um "User Product" (um portátil de consumo é
exatamente isso), a *Corresponding Source* tem que vir acompanhada de
"métodos, procedimentos, **chaves de autorização** ou outras informações
necessárias para instalar e executar versões modificadas". A única dispensa vale
quando **nem você nem terceiros** conseguem instalar código modificado — o caso
de firmware gravado em ROM.

O GIOCHI é o oposto disso: rootfs SquashFS **assinado** (Ed25519), A/B, com a
chave na mão do projeto. Se o jogo vive assinado dentro da imagem, o usuário
não consegue instalar a versão modificada dele, e nós conseguimos — logo a
dispensa não vale. Entregar a chave de assinatura destruiria a raiz imutável,
que é premissa do sistema.

**O caminho que funcionaria** (se o mantenedor quiser): o jogo sai da imagem
assinada e passa a ser instalável em `/user` — a partição gravável, onde o
usuário já pode pôr a build modificada. Aí a *Installation Information* é um
procedimento público e sem chave ("coloque sua build em `/user/games/<id>`"), e
a raiz assinada continua intacta.

## Por que ele não é jogo padrão hoje

Independente de licença, **falta o port**: Shattered Pixel Dungeon é
Java/libGDX (gradle), não é projeto Godot, e a pasta aqui não tem código. O
`assets/default-games.json` exige, para a seção `games`, projeto, `.md`, cena de
bench, `scripts/bench.gd` e `tests/run_tests.sh` — é o que faz "jogo padrão"
significar **teste e benchmark do aparelho**, e não "jogo que instalamos".
Reinstalar o id em `games` faria `tools/check-default-games.py` reprovar, com
razão.

A regra do repositório, hoje: **jogo padrão (na imagem assinada, medido no
benchmark) só com licença permissiva** — domínio público, permissão dos autores
ou código próprio.

## Decisão

**Fica fora.** O mantenedor reafirmou em **2026-10-28**, depois de revisar as
obrigações da GPL-3.0 acima, que o jogo continua fora da lista de jogos padrão;
o assunto volta "quando houver decisão definitiva sobre como proceder".

Nada foi portado: não há projeto Godot, código ou asset nesta pasta. O que
existe é esta nota, o registro em `excluded` e os testes que travam a decisão
para ninguém revertê-la sem saber.

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
