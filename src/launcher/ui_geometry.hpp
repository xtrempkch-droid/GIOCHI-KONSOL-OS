/* Giochi Konsol OS v1.2 — geometria da UI (secoes 4.1.4/4.1.7/4.9.2).
 *
 * Modulo PURO: nenhuma dependencia de SDL, para poder ser testado no host
 * (tests/test_ui_geometry.cpp) e para o desenho nao virar um monte de numero
 * magico espalhado pelo render.cpp.
 *
 * Os tokens vem de docs/LAYOUT-PROPOSTA.md §3, que e a tabela MEDIDA pelo
 * gerador `tools/make-layout-proposal.py`. O painel base e 640x480; o X55 e
 * 1280x720, ou seja exatamente x1,5 — a mesma escala declarada em
 * assets/devices/x55/, que tests/test_device_scale.py cobra.
 *
 * As COLUNAS nao sao token escalado: saem da largura util, porque 5 colunas de
 * 228 px nao caberiam em 640 px. Medido na proposta: 4 colunas em 640 (grade de
 * 632 px) e 5 em 1280 (grade de 1188 px).
 */
#ifndef GIOCHI_UI_GEOMETRY_HPP
#define GIOCHI_UI_GEOMETRY_HPP

#include "layout.hpp"

#define GIOCHI_MAX_TILES 24

namespace giochi {

struct UiRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct UiGeometry {
    int screen_w = 640;
    int screen_h = 480;
    int scale_pct = 100;

    UiRect topbar;
    UiRect hint_bar;
    UiRect content; /* area util entre as duas barras, com margem aplicada */

    /* Abas. `tab[i].w` sai da largura MEDIDA do rotulo, porque o espacamento e
     * "x da proxima = x + largura + 20" (§4.1) e nao uma celula fixa. Aba que
     * nao cabe recebe w = 0 e nao deve ser desenhada. */
    UiRect tab[GIOCHI_MAX_TABS];
    int tab_count = 0;
    int tab_active = 0;

    /* Grade de itens. `first_index` e o primeiro item desenhado (paginacao do
     * item em foco), `tile_count` o que cabe nesta tela. */
    UiRect tile[GIOCHI_MAX_TILES];
    int tile_count = 0;
    int first_index = 0;
    int capacity = 0;
    int columns = 0;
    int rows = 0;
    int focus_slot = -1; /* indice em `tile` do item em foco; -1 se fora */

    /* Tokens escalados, expostos para o desenho nao recalcular. */
    int tile_w = 0;
    int tile_h = 0;
    int title_h = 0;
    int gap = 0;
    int margin = 0;
    int inset = 0;
    int corner = 0;
    int accent_h = 0;   /* espessura do sublinhado da aba ativa */
    int focus_ring = 0; /* espessura do anel de foco da grade */

    /* Fontes (px) — 12/14/20 no painel base, 18/21/30 na X55. */
    int font_tab = 14;
    int font_title = 12;
    int font_body = 12;
    int line_h = 15; /* altura de uma linha de font_body (do atlas) */
};

/* Preenche as barras e os tokens. Chamar antes de ui_geometry_tabs e
 * ui_geometry_grid. */
void ui_geometry_base(UiGeometry *g, int screen_w, int screen_h);

/* Posiciona as abas com as larguras medidas dos rotulos (px). Abas que nao
 * couberem recebem w = 0. */
void ui_geometry_tabs(UiGeometry *g, const int *label_width, int count,
                      int active);

/* Posiciona a grade de `item_count` itens com foco em `focus_index`. */
void ui_geometry_grid(UiGeometry *g, int item_count, int focus_index);

/* Retangulo do anel de foco do slot — por FORA do tile (§5.2). */
UiRect ui_geometry_focus_ring(const UiGeometry &g, int slot);

/* Onde vai o contador de pacotes.
 *
 * Medido no preview: desenhado logo abaixo da topbar, ele CAIA EM CIMA do
 * primeiro tile (o conteudo comeca em topbar + margem). A faixa livre e' entre
 * o fim da grade e a hint bar — e e' onde ele passa a ficar.
 * Devolve um retangulo de altura de linha, alinhado a' direita, na sarjeta. */
UiRect ui_geometry_count_rect(const UiGeometry &g);

/* Intersecao de dois retangulos (0 = nao se tocam). Usado pelos testes para
 * provar que um elemento nao invade outro. */
int ui_rect_intersects(const UiRect &a, const UiRect &b);

/* --- Aparencia do tile: papeis do tema, nao cores ---------------------
 *
 * Por que isto existe (armadilha §3.71): o render.cpp e o preview-ui.py
 * desenhavam o tile cada um com a sua regra, e as duas divergiram — o preview
 * pintava TODOS os tiles com `surface_elevated`, o que transforma a grade numa
 * parede de caixas claras e apaga a hierarquia (o foco tem de ser o unico
 * elemento "aceso"). O usuario viu isso como "preview bugado".
 *
 * A regra agora mora AQUI, uma vez, e os dois lados so' traduzem papel -> cor.
 */
enum UiRole {
    UI_ROLE_NONE = 0, /* nao desenhar */
    UI_ROLE_BACKGROUND,
    UI_ROLE_SURFACE,
    UI_ROLE_ELEVATED,
    UI_ROLE_DIVIDER,
    UI_ROLE_ACCENT,
    UI_ROLE_TEXT_PRIMARY,
    UI_ROLE_TEXT_SECONDARY
};

struct UiTileStyle {
    UiRole fill = UI_ROLE_SURFACE;    /* corpo do tile */
    UiRole inset = UI_ROLE_NONE;      /* recorte interno (slot sem pacote) */
    UiRole outline = UI_ROLE_NONE;    /* borda do tile */
    UiRole title = UI_ROLE_TEXT_SECONDARY;
    int ring = 0;   /* espessura do anel de foco por FORA (0 = sem) */
    int pad = 0;    /* recuo do titulo a partir da borda esquerda do tile */
    int title_dy = 0; /* deslocamento da BASE do titulo a partir de tile.y+tile.h */
};

/* --- Peso tipografico por papel -------------------------------------
 *
 * Mora aqui, e nao no render.cpp, porque o preview (tools/preview-ui.py) tem
 * de escolher a MESMA face: quando a escolha vivia em dois lugares, o preview
 * passou a desenhar tudo em regular enquanto a proposta pedia negrito.
 *
 * A referencia visual e' docs/proposta/library-1280x720.png: titulo de tile,
 * aba ativa, tecla da hint bar e titulo de painel em NEGRITO; corpo, estado
 * vazio, acao da hint bar e relogio em regular.
 *
 * Os indices sao contrato com o dump-geometry JSON (campo `font_weights`) —
 * mesma regra do UiRole. Nao reordene sem atualizar tools/preview-ui.py e
 * tests/test_locales.py.
 */
enum UiTextRole {
    UI_TEXT_TAB_ACTIVE = 0,
    UI_TEXT_TAB_IDLE,
    UI_TEXT_TITLE,
    UI_TEXT_BODY,
    UI_TEXT_HINT_KEY,
    UI_TEXT_HINT_ACTION,
    UI_TEXT_STATUS,
    UI_TEXT_PANEL_TITLE,
    UI_TEXT_MENU_ROW,
    UI_TEXT_COUNT
};

enum UiWeight { UI_WEIGHT_REGULAR = 0, UI_WEIGHT_BOLD = 1 };

UiWeight ui_text_weight(UiTextRole role);

/* "regular" | "bold" — o mesmo nome que o font.json usa. */
const char *ui_weight_name(UiWeight weight);

/* Estilo de um tile. Referencia: docs/proposta/library-1280x720.png — tile
 * normal fica no `surface` (recua), o focado sobe para `surface_elevated` com
 * borda e anel de acento, e o titulo acompanha (secundario -> primario). */
UiTileStyle ui_tile_style(const UiGeometry &g, bool focused, bool valid);

/* --- Apresentacao: painel fisico x painel logico ----------------------
 *
 * Medido na X55: o painel e' 720x1280 em retrato com `rotation = <270>` no DTB,
 * e o layout e' 1280x720 (paisagem). O kernel NAO gira a imagem — quem gira e'
 * o compositor, e o SDL 2.28 nao le a orientacao do conector. Resultado
 * observado no aparelho: UI em pe' e com a escala errada.
 *
 * A saida e' desenhar no tamanho LOGICO (o do layout) e apresentar girado.
 * Este struct diz o que fazer; o render.cpp so' obedece.
 */
struct UiPresentation {
    int angle = 0;   /* graus no sentido horario, para SDL_RenderCopyEx */
    int dst_x = 0;   /* retangulo de destino JÁ DEPOIS da rotacao */
    int dst_y = 0;
    int dst_w = 0;
    int dst_h = 0;
    int need_target = 0; /* 1 = precisa de textura intermediaria */
    int ok = 0;          /* 0 = dimensoes nao batem; ver `motivo` */
    const char *motivo = "";
};

/* rotation_deg: 0/90/180/270 (valores do device.conf).
 * logical: tamanho em que a UI e' DESENHADA.  out: tamanho real do framebuffer.
 *
 * Regra: com 90/270 as dimensoes fisicas sao as logicas trocadas. O retangulo
 * de destino e' do tamanho LOGICO e centrado na saida — assim o quad girado
 * cobre exatamente o framebuffer (o SDL gira em torno do centro do dstrect).
 */
UiPresentation ui_present(int rotation_deg, int logical_w, int logical_h,
                          int out_w, int out_h);

int ui_rect_contains(const UiRect &r, int x, int y);

}  // namespace giochi

#endif /* GIOCHI_UI_GEOMETRY_HPP */
