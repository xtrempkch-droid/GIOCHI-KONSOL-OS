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

/* Densidade da grade (docs/TEMAS.md): o que muda entre "compacta", "normal" e
 * "espacosa" e' o espaco, nao o tile — o tile e' dado do aparelho, nao do tema.
 * Valores no painel base; escalam como o resto. */
struct DensityTokens {
    int gap;
    int title_h;
    int inset;
    int tile_pct; /* o TILE tambem muda: e' ele que decide quantos cabem */
};

/* Medido: com o tile fixo, mudar so' o espaco nao altera a contagem de colunas
 * em nenhum dos dois paineis (4 em 640 e 5 em 1280 nos tres modos) — o modo
 * seria decorativo. Escalando o tile junto, o resultado muda de verdade:
 *   compacta  6 colunas em 1280x720 e 4 em 640x480
 *   normal    5 e 4
 *   espacosa  4 e 3
 */
constexpr DensityTokens DENSITY_TOKENS[3] = {
    {6, 16, 4, 88},  /* compacta */
    {8, 20, 4, 100}, /* normal */
    {12, 22, 8, 115} /* espacosa */
};

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

UiPresentation ui_present(int rotation_deg, int logical_w, int logical_h,
                          int out_w, int out_h)
{
    UiPresentation p;

    if (logical_w <= 0 || logical_h <= 0 || out_w <= 0 || out_h <= 0) {
        p.motivo = "dimensoes invalidas";
        return p;
    }

    /* Normaliza para 0/90/180/270 (o device.conf pode trazer -90, 450...). */
    int rot = ((rotation_deg % 360) + 360) % 360;

    if (rot == 90 || rot == 270) {
        /* Fisico tem de ser o logico transposto. Se nao for, o painel nao
         * corresponde ao layout: girar produziria imagem cortada, e e' melhor
         * desenhar reto e dizer o motivo no log. */
        if (out_w != logical_h || out_h != logical_w) {
            p.motivo = "rotacao 90/270 mas o framebuffer nao e' o layout "
                       "transposto";
            return p;
        }
        p.angle = rot;
        p.need_target = 1;
        /* Quad do tamanho logico, centrado na saida: depois de girado em
         * torno do mesmo centro, ele cobre exatamente out_w x out_h. */
        p.dst_w = logical_w;
        p.dst_h = logical_h;
        p.dst_x = (out_w - logical_w) / 2;
        p.dst_y = (out_h - logical_h) / 2;
        p.ok = 1;
        return p;
    }

    /* 0 e 180: as dimensoes tem de bater direto. */
    if (out_w != logical_w || out_h != logical_h) {
        p.motivo = "framebuffer com dimensoes diferentes do layout";
        return p;
    }

    p.angle = rot;
    p.need_target = rot == 180 ? 1 : 0;
    p.dst_x = 0;
    p.dst_y = 0;
    p.dst_w = out_w;
    p.dst_h = out_h;
    p.ok = 1;
    return p;
}

int ui_rect_contains(const UiRect &r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/* Recalcula barras, colunas e linhas a partir dos tokens ja' gravados em `g`.
 * Fica separado de ui_geometry_base porque o tema mexe em gap/titulo/barra e o
 * resultado tem de ser recalculado com a MESMA formula (senao o preview e o
 * aparelho divergem na contagem de colunas). */
static void layout_from_tokens(UiGeometry *g)
{
    const int screen_w = g->screen_w;
    const int screen_h = g->screen_h;

    g->topbar = UiRect{0, 0, screen_w, sc(g->topbar_h, screen_h)};
    g->hint_bar = UiRect{0, screen_h - sc(g->hint_bar_h, screen_h), screen_w,
                         sc(g->hint_bar_h, screen_h)};

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
    g->line_h = sc(15, screen_h);

    layout_from_tokens(g);
}

UiThemeMetrics ui_theme_metrics_from(const giochi_theme &t)
{
    UiThemeMetrics m;

    m.corner_px = t.corner_px;
    m.border_px = t.border_px;
    m.focus_ring_px = t.focus_ring_px;
    m.topbar_px = t.topbar_h_px;
    m.hint_bar_px = t.hint_bar_h_px;
    m.density = t.density;
    m.cover_pct = t.cover_pct;
    m.hints = t.hints;
    m.title_scale_pct = t.title_scale_pct;
    return m;
}

void ui_geometry_apply_theme(UiGeometry *g, const UiThemeMetrics &m)
{
    if (g == nullptr || g->screen_h <= 0) {
        return;
    }

    const int h = g->screen_h;
    const int dens = clamp_int(m.density, 0, 2);
    const DensityTokens &dt = DENSITY_TOKENS[dens];

    g->corner = sc(m.corner_px > 0 ? m.corner_px : 0, h);
    g->border = sc(m.border_px > 0 ? m.border_px : 0, h);
    g->focus_ring = sc(m.focus_ring_px > 0 ? m.focus_ring_px : 0, h);
    g->density = dens;
    g->cover_pct = m.cover_pct > 0 ? m.cover_pct : 100;
    g->hints = m.hints;
    g->gap = sc(dt.gap, h);
    g->title_h = sc(dt.title_h, h);
    g->inset = sc(dt.inset, h);
    g->tile_w = sc(TILE_W * dt.tile_pct, h) / 100;
    g->tile_h = sc(TILE_H * dt.tile_pct, h) / 100;
    g->topbar_h = m.topbar_px > 0 ? m.topbar_px : TOPBAR_H;
    g->hint_bar_h = m.hint_bar_px > 0 ? m.hint_bar_px : HINT_H;

    if (m.title_scale_pct > 0 && m.title_scale_pct != 100) {
        g->font_title = sc(FONT_TITLE, h) * m.title_scale_pct / 100;
        g->font_body = sc(FONT_BODY, h) * m.title_scale_pct / 100;
        if (g->font_title < 8) {
            g->font_title = 8;
        }
        if (g->font_body < 8) {
            g->font_body = 8;
        }
        g->line_h = g->font_body * 5 / 4;
    }

    layout_from_tokens(g);
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

UiRect ui_geometry_count_rect(const UiGeometry &g)
{
    /* Altura de uma linha de corpo + respiro, encostada na hint bar. */
    const int line = g.line_h > 0 ? g.line_h : g.font_body + g.font_body / 2;
    const int y = g.hint_bar.y - g.margin - line;

    return UiRect{g.content.x, y > 0 ? y : 0, g.content.w, line};
}

int ui_rect_intersects(const UiRect &a, const UiRect &b)
{
    if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0) {
        return 0;
    }
    return !(a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y ||
             b.y + b.h <= a.y);
}

UiWeight ui_text_weight(UiTextRole role)
{
    /* Uma tabela, sem `if` espalhado: assim o dump-geometry pode publicar a
     * lista inteira e o preview reproduz a mesma escolha sem repetir regra. */
    static const UiWeight kTabela[UI_TEXT_COUNT] = {
        UI_WEIGHT_BOLD,    /* UI_TEXT_TAB_ACTIVE  */
        UI_WEIGHT_REGULAR, /* UI_TEXT_TAB_IDLE    */
        UI_WEIGHT_BOLD,    /* UI_TEXT_TITLE       */
        UI_WEIGHT_REGULAR, /* UI_TEXT_BODY        */
        UI_WEIGHT_BOLD,    /* UI_TEXT_HINT_KEY    */
        UI_WEIGHT_REGULAR, /* UI_TEXT_HINT_ACTION */
        UI_WEIGHT_REGULAR, /* UI_TEXT_STATUS      */
        UI_WEIGHT_BOLD,    /* UI_TEXT_PANEL_TITLE */
        UI_WEIGHT_REGULAR  /* UI_TEXT_MENU_ROW    */
    };

    if (role < 0 || role >= UI_TEXT_COUNT) {
        return UI_WEIGHT_REGULAR;
    }
    return kTabela[role];
}

const char *ui_weight_name(UiWeight weight)
{
    return weight == UI_WEIGHT_BOLD ? "bold" : "regular";
}

UiTileStyle ui_tile_style(const UiGeometry &g, bool focused, bool valid)
{
    UiTileStyle s;

    /* Hierarquia da referencia (docs/proposta/library-1280x720.png): a grade
     * RECUA e o foco sobe. Fazer o contrario — tudo claro e o foco so' com
     * anel — vira parede de caixas e some a leitura de "onde eu estou". */
    s.fill = focused ? UI_ROLE_ELEVATED : UI_ROLE_SURFACE;
    /* Tema com `tile_border` > 0 desenha contorno em TODO tile (o look "caixa"
     * das propostas); sem ele, so' o foco tem contorno. */
    if (focused) {
        s.outline = UI_ROLE_ACCENT;
    } else {
        s.outline = g.border > 0 ? UI_ROLE_DIVIDER : UI_ROLE_NONE;
    }
    s.ring = focused ? g.focus_ring : 0;
    s.title = focused ? UI_ROLE_TEXT_PRIMARY : UI_ROLE_TEXT_SECONDARY;
    s.pad = g.gap / 2;
    s.title_dy = (g.title_h - g.line_h) / 2 + g.line_h;

    /* Slot sem pacote: um recorte mais escuro que o corpo. Nao e' decoracao —
     * e' o que diz "aqui cabe um jogo e nao tem nenhum". */
    if (!valid) {
        s.inset = UI_ROLE_BACKGROUND;
    }
    return s;
}

}  // namespace giochi
