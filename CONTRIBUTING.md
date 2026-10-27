# Contribuindo

Obrigado pelo interesse. Este projeto tem duas metas que não são negociáveis e
que você deve manter em qualquer PR:

1. **Nada de systemd, glibc pré-compilado, PulseAudio/PipeWire na base, Vulkan no
   RK3326, XWayland ou shell Wayland.** São decisões arquiteturais registradas
   como ADRs em [docs/architecture-v1.2.md](docs/architecture-v1.2.md).
2. **Cabe no orçamento**: SquashFS ≤ 150 MB, RAM em repouso ≤ 60 MB, VRAM de UI
   ≤ 4 MB (§8 da especificação).

## Antes de abrir PR

```sh
make lint     # shell (sh -n), JSON, defconfigs atualizados
make test     # 12 suítes: unitários C/C++, forge, integração, bootlogo
tools/build-image.sh --check --all
```

Se você mexeu em `buildroot/board/giochi/devices/*.conf` ou nos templates de
defconfig, rode também:

```sh
make defconfigs
```

Os arquivos em `buildroot/configs/` são **gerados** — PRs que editam esses
arquivos diretamente são recusados; a CI (`gen-defconfigs.sh --check`) falha.

## Estilo

- C: C99, sem extensões GNU além de `_GNU_SOURCE`; protótipos para tudo; nada de
  alocação escondida. Rode `make` sem warnings (`-Wall -Wextra -Wformat=2`).
- C++: C++17, RAII, sem exceções atravessando fronteiras C.
- Shell: POSIX (`BusyBox ash`), `set -u`, `logger -t`, sem `bc`, sem bashisms.
  Teste com `sh -n` no mínimo; prefira `dash`/`busybox sh`.
- Python (forge): 3.10+, apenas stdlib, testes em `tests/forge/`.
- Formatação geral: [.editorconfig](.editorconfig) (4 espaços, LF, newline final).

## Commits

- Mensagem no imperativo, curta, com escopo quando fizer sentido:
  `init: segura o launcher durante o jogo`.
- Um commit por mudança lógica. Evite misturar refactor e feature.
- Referencie a seção da especificação quando a mudança implementa um requisito:
  `launcher: B volta um nível (§4.1.2)`.

## Testes

- Lógica pura (botões, JSON, SHA-256, layout, metadata, settings, `buttons.map`)
  vai em `tests/` e roda em qualquer host.
- Integração de processo (supervisor, jogo, app, pacote `.giochi`) vai em
  `tests/integration/lifecycle_test.sh`, que sobe um `giochi-init` real com
  atores falsos.
- Hardware real: siga [docs/QA-CHECKLIST.md](docs/QA-CHECKLIST.md) e registre na
  descrição do PR qual aparelho foi usado.

## Adicionando suporte a um aparelho

Passo a passo em [docs/DEVICES.md](docs/DEVICES.md#adicionar-um-aparelho).
Resumo: crie o `.conf` em `board/giochi/devices/`, o layout em
`assets/devices/<id>/` se o painel não for 640×480, rode `make defconfigs`,
valide com `--check` e atualize a matriz de aparelhos.

## Licença dos seus commits

Ao contribuir você concorda em licenciar o código sob a [MIT](LICENSE). Não
envie código de terceiros sem compatibilidade de licença — em especial device
trees e patches do kernel, que devem ir para `buildroot/board/giochi/dts/` com
crédito e origem.
