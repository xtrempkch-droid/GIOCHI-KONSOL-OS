# Imagens do sistema

Todas as imagens desta pasta são **geradas por script** a partir dos mesmos
arquivos que o Launcher lê em tempo de execução. Nada foi desenhado à mão:

```sh
make screenshots        # equivalente a: tools/make-screenshots.py
```

| Arquivo | Origem dos dados | O que é |
|---|---|---|
| `home.png` | `assets/theme.json`, `assets/layout.json`, `assets/home.json` | composição da aba Home (640×480) |
| `library.png` | `assets/library.json` | grid 4×3, tile 152×85, título 20 px, scrollbar 4 px |
| `music.png` | `assets/music.json` | now playing 120×120, progresso, controles, fila |
| `settings.png` | `src/launcher/overlay.cpp` (nível 0) | overlay de Configurações |
| `settings-system.png` | idem, nível 1 | submenu Sistema com o **Modo Desenvolvedor** |
| `x55-home.png` | `assets/devices/x55/layout.json` | canvas 1280×720 do painel da X55 |
| `bootlogo.png` | `assets/bootlogo/bootlogo.rgba` | **splash real**, desenhado pelo Launcher |

O alvo de layout (o que a UI **deveria** desenhar, com perfil de painel ×1,5 para
a X55 e a análise de usabilidade das 5 abas) está em
[LAYOUT-PROPOSTA.md](./LAYOUT-PROPOSTA.md), com os renders em `docs/proposta/`.
Este documento continua descrevendo apenas o que os assets declaram hoje.

## O que estas imagens são — e o que não são

**São** a geometria e as cores declaradas nos assets: alturas de topbar (40) e
hint bar (30), tile de 152×85, grid 4×3, raio de canto 4, paleta do tema
`Netflix Dark` (`#0F0F0F` fundo, `#E50914` destaque). Se você mudar o
`theme.json` e rodar `make screenshots`, as imagens mudam junto.

**Não são** captura de tela de um aparelho. O desenho dos widgets no backend
SDL2 ainda é esboço — veja o `TODO(v1.2)` em
[`src/launcher/render.cpp`](../src/launcher/render.cpp), que hoje pinta o fundo
do tema e o bootsplash. O texto de exemplo ("Celeste", "Stardew Valley"…)
existe para dar escala ao tile; ele não vem da biblioteca do usuário.

Quando `render.cpp` estiver completo, o caminho correto passa a ser capturar a
tela do aparelho (`fbgrab`/DRM) e substituir estes arquivos — o script continua
útil como referência de layout antes de ter hardware à mão.

## Por que a X55 aparece com barras laterais

O layout declara `640×480`, mas o painel da X55 é `720×1280` com rotação 270,
o que dá uma área lógica de `1280×720` (16:9). `x55-home.png` escala a
composição 4:3 para a altura do painel e centraliza, deixando as laterais na
cor de fundo — em vez de esticar a imagem e mentir sobre a proporção.

## Reproduzir

```sh
python3 -m pip install pillow      # única dependência
tools/make-screenshots.py          # docs/screenshots/
tools/make-screenshots.py --scale 2   # tudo em 2x (útil para slides)
tools/make-screenshots.py --out-dir /tmp/shot
```
