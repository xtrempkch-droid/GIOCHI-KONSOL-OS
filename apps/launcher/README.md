# `apps/launcher` — a interface do console

O `giochi-launcher` é o **aplicativo** da UI: barra de abas, grade da
biblioteca, overlay de Configurações e o laço de entrada do gamepad. Ele não é
núcleo do sistema — o supervisor (`src/init/`) é quem inicia e supervisiona,
inclusive este app.

Estar em `apps/` é o que permite mexer na interface sem tocar em `src/`
(sistema) nem em `Jogos/`.

## Arquivos

| Arquivo | Papel |
|---|---|
| `main.cpp` | ponto de entrada, laço principal, troca de aba, novidades da Home |
| `layout.{hpp,cpp}` | lê `layout.json` (schema 1.6) e as abas |
| `theme.{hpp,cpp}` | lê `theme.json` (cores, métricas) |
| `settings.{hpp,cpp}` | lê/escreve `settings.json` (só o Launcher escreve — §3.10) |
| `overlay.{hpp,cpp}` | menu de Configurações em dois níveis |
| `input.{hpp,cpp}`, `evdev.{hpp,cpp}` | gamepad via `/etc/giochi/buttons.map` |
| `library.{hpp,cpp}`, `metadata.{hpp,cpp}` | pacotes `.giochi` de `/user/games` |
| `ipc_client.{hpp,cpp}` | fala com `/run/giochi/control.sock` |
| `ui_geometry.{hpp,cpp}` | **módulo puro, sem SDL** — tokens, grade, foco |
| `font.{hpp,cpp}` | atlas de texto gerado no build |
| `i18n.{hpp,cpp}` | catálogos em `assets/locales/` |
| `status.{hpp,cpp}` | bateria, volume, rede, USB |
| `render.{hpp,cpp}`, `render_null.cpp` | desenho SDL2 e o backend nulo |

## Build

```sh
make                     # host: build/host/bin/giochi-launcher
make test                # inclui test_layout, test_ui_geometry, test_i18n, ...
tools/check-sdl2-compile.sh   # compila render.cpp COM SDL2 (o make do host
                              # pula o bloco #ifdef GIOCHI_HAVE_SDL2)
```

No Buildroot, o pacote `giochi-launcher` (`buildroot/package/`) compila este
diretório a partir do snapshot de fontes que o `tools/build-image.sh` monta em
`dist/<id>/pkg-src` (por isso `apps` está na lista de itens copiados — sem
isso o snapshot sai sem os fontes).

## Substituir a UI no aparelho

O binário da imagem é `/usr/bin/giochi-launcher`. O supervisor procura
**primeiro** `/apps/giochi-launcher` (que é `/user/apps`, gravável): copiando um
binário para lá e dando `chmod +x`, a próxima ida à Home já usa o novo —
sem regravar o sistema. Detalhes e o formato do `assets/` de override em
[docs/LAUNCHER-APP.md](../../docs/LAUNCHER-APP.md).
