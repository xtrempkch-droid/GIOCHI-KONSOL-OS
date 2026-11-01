#include "ui_geometry.hpp"

namespace giochi {

namespace {

/* Tokens do painel de REFERENCIA (640x480) — docs/LAYOUT-PROPOSTA.md §3. */
constexpr int BASE_H = 480;
constexpr int TOPBAR_H = 40;
constexpr int HINT_H = 30;
constexpr int MARGIN = 12;
constexpr int INSET = 4; /* recuo da grade: 640 - 2*4 = 632, como na tabela */
constexpr int GAP = 8;
constexpr int TILE_W = 152;
constexpr int TILE_H = 85;
constexpr int TITLE_H = 20;
constexpr int CORNER = 4;
constexpr int ACCENT_H = 3;
constexpr int FOCUS_RING = 2;
constexpr int TAB_GAP = 20;
constexpr int FONT_TAB = 14;
constexpr int FONT_TITLE = 12;
constexpr int FONT_BODY = 12;

/* Arredonda para o mais proximo em vez de truncar: 85 * 1,5 = 127,5 tem de
 * virar 128 (o valor declarado em assets/devices/x55/library.json). */
int sc(int value, int screen_h)
{
    return (value * screen_h + BASE_H / 2) / BASE_H;
}

/* Truncamento, para o token cuja tabela manda 3 -> 4 (e nao 5): o sublinhado
 * de acento e 3 px no painel base e 4 px no grande (§4.1). */
int sc_down(int value, int screen_h)
{
    return value * screen_h / BASE_H;
}

int clamp_int(int value, int lo, int hi)
{
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

}  // namespace

int ui_rect_contains(const UiRect &r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void ui_geometry_base(UiGeometry *g, int screen_w, int screen_h)
{
    if (g == nullptr) {
        return;
    }

    if (screen_h <= 0) {
        screen_h = BASE_H;
    }
    if (screen_w <= 0) {
        screen_w = screen_h * 4 / 3; /* proporcao do painel base (4:3) */
    }

    *g = UiGeometry{};
    g->screen_w = screen_w;
    g->screen_h = screen_h;
    g->scale_pct = screen_h * 100 / BASE_H;

    g->gap = sc(GAP, screen_h);
    g->margin = sc(MARGIN, screen_h);
    g->inset = sc(INSET, screen_h);
    g->corner = sc(CORNER, screen_h);
    g->accent_h = sc_down(ACCENT_H, screen_h);
    g->focus_ring = sc(FOCUS_RING, screen_h);
    g->tile_w = sc(TILE_W, screen_h);
    g->tile_h = sc(TILE_H, screen_h);
    g->title_h = sc(TITLE_H, screen_h);
    g->font_tab = sc(FONT_TAB, screen_h);
    g->font_title = sc(FONT_TITLE, screen_h);
    g->font_body = sc(FONT_BODY, screen_h);

    g->topbar = UiRect{0, 0, screen_w, sc(TOPBAR_H, screen_h)};
    g->hint_bar = UiRect{0, screen_h - sc(HINT_H, screen_h), screen_w,
                         sc(HINT_H, screen_h)};

    const int content_h = g->hint_bar.y - g->topbar.h;
    g->content = UiRect{g->margin, g->topbar.h,
                        screen_w - 2 * g->margin, content_h > 0 ? content_h : 0};

    /* Grade: colunas pelo maior n que cabe na largura util (§3). */
    const int inner_w = screen_w - 2 * g->inset;
    const int cell_w = g->tile_w + g->gap;
    int columns = 1;

    if (cell_w > 0) {
        columns = (inner_w + g->gap) / cell_w;
    }
    if (columns < 1) {
        columns = 1;
    }
    if (columns > GIOCHI_MAX_TILES) {
        columns = GIOCHI_MAX_TILES;
    }
    g->columns = columns;

    /* Linhas: cada celula e tile + titulo, mais o gap entre linhas. O espaco
     * disponivel desconta a margem do topo (content_top = topbar + margin) e o
     * recuo de baixo — e o que o gerador da proposta faz para achar 383/576. */
    const int cell_h = g->tile_h + g->title_h + g->gap;
    const int usable_h = content_h - g->margin - g->inset;
    int rows = 1;

    if (cell_h > 0) {
        rows = (usable_h + g->gap) / cell_h;
    }
    if (rows < 1) {
        rows = 1;
    }
    g->rows = rows;
    g->capacity = clamp_int(rows * columns, 0, GIOCHI_MAX_TILES);
}

void ui_geometry_tabs(UiGeometry *g, const int *label_width, int count,
                      int active)
{
    if (g == nullptr || label_width == nullptr) {
        return;
    }

    const int gap = sc(TAB_GAP, g->screen_h);
    g->tab_count = clamp_int(count, 0, GIOCHI_MAX_TABS);
    g->tab_active = g->tab_count > 0 ? clamp_int(active, 0, g->tab_count - 1) : 0;

    int x = g->margin;

    for (int i = 0; i < g->tab_count; i++) {
        const int w = label_width[i] > 0 ? label_width[i] : 0;

        /* Aba que invade a area dos indicadores (direita) nao e desenhada:
         * melhor sumir do que encostar na bateria — a colisao que a barra de
         * dicas antiga tinha (§4.1). */
        if (x + w > g->screen_w - g->margin) {
            g->tab[i] = UiRect{x, 0, 0, g->topbar.h};
            x += gap;
            continue;
        }
        g->tab[i] = UiRect{x, 0, w, g->topbar.h};
        x += w + gap;
    }
}

void ui_geometry_grid(UiGeometry *g, int item_count, int focus_index)
{
    if (g == nullptr) {
        return;
    }

    g->tile_count = 0;
    g->focus_slot = -1;

    if (g->capacity <= 0) {
        g->first_index = 0;
        return;
    }

    if (item_count < 0) {
        item_count = 0;
    }
    /* Pagina do item em foco: foco negativo nunca existe, mas focus_index
     * maior que item_count acontece quando a biblioteca encolhe. */
    int focus = focus_index;

    if (focus < 0) {
        focus = 0;
    }
    if (focus >= item_count) {
        focus = item_count > 0 ? item_count - 1 : 0;
    }

    const int first = (focus / g->capacity) * g->capacity;
    g->first_index = first;

    int slots = item_count - first;

    if (slots < 0) {
        slots = 0;
    }
    if (slots > g->capacity) {
        slots = g->capacity;
    }
    g->tile_count = slots;

    const int cell_w = g->tile_w + g->gap;
    const int cell_h = g->tile_h + g->title_h + g->gap;
    const int grid_w = g->columns * g->tile_w + (g->columns - 1) * g->gap;
    const int x0 = (g->screen_w - grid_w) / 2;
    const int top = g->topbar.h + g->margin;

    for (int i = 0; i < slots; i++) {
        const int col = i % g->columns;
        const int row = i / g->columns;
        g->tile[i] = UiRect{x0 + col * cell_w, top + row * cell_h, g->tile_w,
                            g->tile_h};
    }

    if (item_count > 0 && focus >= first && focus < first + g->tile_count) {
        g->focus_slot = focus - first;
    }
}

UiRect ui_geometry_focus_ring(const UiGeometry &g, int slot)
{
    if (slot < 0 || slot >= g.tile_count) {
        return UiRect{};
    }

    const UiRect &t = g.tile[slot];
    const int r = g.focus_ring;

    return UiRect{t.x - r, t.y - r, t.w + 2 * r, t.h + 2 * r};
}

}  // namespace giochi
