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

int ui_rect_contains(const UiRect &r, int x, int y);

}  // namespace giochi

#endif /* GIOCHI_UI_GEOMETRY_HPP */
