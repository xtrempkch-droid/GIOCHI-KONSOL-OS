/* Backend SDL2 (Gamescope / DRM-KMS). Compilado apenas com GIOCHI_HAVE_SDL2.
 *
 * TODO(v1.2): desenhar grid 4x3 (tile 152x85 + titulo 20px), topbar 40px,
 * hint bar 30px e overlay de Configuracoes (secoes 4.1.4/4.1.7/4.9.2). Este
 * esqueleto valida janela + loop de eventos e limpa a tela com a cor de fundo
 * do tema; o teto de VRAM (4 MB) sera medido em S15.
 *
 * Bootsplash (ver docs/BOOTSPLASH.md): os primeiros frames mostram
 * /usr/share/giochi/bootlogo.rgba (gerado por tools/make-bootlogo.py) em cima
 * do fundo do tema. Sem o arquivo, ou com GIOCHI_NO_SPLASH=1, nada muda.
 */
#include "render.hpp"

#include "font.hpp"
#include "giochi_deviceconf.h"
#include "giochi_json.h"
#include "giochi_log.h"
#include "giochi_util.h"
#include "ui_geometry.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#ifdef GIOCHI_HAVE_SDL2

#include <SDL.h>

namespace giochi {

namespace {

/* --- Bootsplash -------------------------------------------------------
 *
 * Formato: RGBA8888 top-down (bootlogo.rgba) + bootlogo.json com largura e
 * altura. Tamanho tipico 640x349 (~893 KB), bem abaixo do teto de 4 MB.
 * O splash e exibido por GIOCHI_SPLASH_FRAMES frames (default 180 ~= 3 s a
 * 60 fps) e e interrompido no primeiro botao pressionado.
 */
class SplashImage {
public:
    SplashImage() = default;
    ~SplashImage() { release(); }

    SplashImage(const SplashImage &) = delete;
    SplashImage &operator=(const SplashImage &) = delete;

    bool load(SDL_Renderer *sdl, const char *base_dir)
    {
        std::string prefix = base_dir != nullptr ? std::string(base_dir)
                                                 : std::string("/usr/share/giochi");
        std::string rgba_path = prefix + "/bootlogo.rgba";
        std::string json_path = prefix + "/bootlogo.json";

        std::size_t len = 0;
        const char *env_path = std::getenv("GIOCHI_BOOTLOGO");
        if (env_path != nullptr && *env_path != '\0') {
            rgba_path = env_path;
        }

        char *data = giochi_read_file(rgba_path.c_str(), &len);
        if (data == nullptr) {
            GLOG_DEBUG("splash: '%s' ausente; sem bootsplash",
                       rgba_path.c_str());
            return false;
        }

        int width = 0;
        int height = 0;
        char err[128] = {0};
        gj_value *meta = gj_parse_file(json_path.c_str(), err, sizeof(err));

        if (meta != nullptr) {
            width = gj_as_int(gj_object_get(meta, "width"), 0);
            height = gj_as_int(gj_object_get(meta, "height"), 0);
            gj_free(meta);
        }
        if (width <= 0 || height <= 0) {
            /* Sem metadados, assume a proporcao do logo (1408x768) com a
             * mesma largura maxima do gerador. */
            width = 640;
            height = 349;
            GLOG_WARN("splash: %s ilegivel; assumindo %dx%d", json_path.c_str(),
                      width, height);
        }
        if ((std::size_t)width * (std::size_t)height * 4U != len) {
            GLOG_WARN("splash: tamanho inesperado (%zu bytes para %dx%d); "
                      "ignorando", len, width, height);
            free(data);
            return false;
        }

        texture_ = SDL_CreateTexture(sdl, SDL_PIXELFORMAT_ABGR8888,
                                     SDL_TEXTUREACCESS_STATIC, width, height);
        if (texture_ == nullptr) {
            GLOG_WARN("splash: SDL_CreateTexture: %s", SDL_GetError());
            free(data);
            return false;
        }
        SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
        if (SDL_UpdateTexture(texture_, nullptr, data, width * 4) != 0) {
            GLOG_WARN("splash: SDL_UpdateTexture: %s", SDL_GetError());
            SDL_DestroyTexture(texture_);
            texture_ = nullptr;
            free(data);
            return false;
        }
        free(data);

        width_ = width;
        height_ = height;
        GLOG_INFO("splash: %dx%d carregado de %s", width_, height_,
                  rgba_path.c_str());
        return true;
    }

    void release()
    {
        if (texture_ != nullptr) {
            SDL_DestroyTexture(texture_);
            texture_ = nullptr;
        }
        width_ = 0;
        height_ = 0;
    }

    /* Desenha centralizado, ocupando no maximo 92% da tela. */
    void draw(SDL_Renderer *sdl) const
    {
        if (texture_ == nullptr) {
            return;
        }
        int out_w = 0;
        int out_h = 0;
        (void)SDL_GetRendererOutputSize(sdl, &out_w, &out_h);
        if (out_w <= 0 || out_h <= 0) {
            return;
        }
        double scale = 0.92 * out_w / width_;
        if (0.92 * out_h / height_ < scale) {
            scale = 0.92 * out_h / height_;
        }
        SDL_Rect dst;
        dst.w = (int)(width_ * scale);
        dst.h = (int)(height_ * scale);
        dst.x = (out_w - dst.w) / 2;
        dst.y = (out_h - dst.h) / 2;
        (void)SDL_RenderCopy(sdl, texture_, nullptr, &dst);
    }

    bool loaded() const { return texture_ != nullptr; }

private:
    SDL_Texture *texture_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

/* GIOCHI_SPLASH_FRAMES=N        quantos quadros o splash fica (0 desliga)
 * GIOCHI_SPLASH_ALWAYS=1        fica para sempre: e' o teste "o SDL desenha?",
 *                               sem depender do resto da UI
 * Default 180 (~3 s a 60 fps): 45 era rapido demais para o olho encontrar a
 * tela ligando e concluir que ALGUMA coisa foi desenhada.
 */
int splash_frames_from_env(int fallback)
{
    const char *sempre = std::getenv("GIOCHI_SPLASH_ALWAYS");

    if (sempre != nullptr && *sempre == '1') {
        return -1; /* marcador de "permanente" */
    }

    const char *env = std::getenv("GIOCHI_SPLASH_FRAMES");
    if (env == nullptr || *env == '\0') {
        return fallback;
    }
    int value = std::atoi(env);
    return value >= 0 ? value : fallback;
}

/* --- Desenho da UI ----------------------------------------------------
 *
 * Toda a geometria vem de ui_geometry (modulo puro, testado no host) e todo o
 * texto do atlas gerado por tools/make-font.py. Aqui so existe "pintar", sem
 * numero de layout e sem metrica de fonte — foi o que a proposta §4 pediu e o
 * que faltava desde a v1.1 (o frame() so limpava a tela).
 *
 * A secao de cada aba segue docs/LAYOUT-PROPOSTA.md §5. As abas sem asset de
 * layout (Loja e Ports apontam para APP:giochi-store e APP:portmaster) mostram
 * um painel explicito, em vez de tela vazia: "0 pacotes" e diagnostico, tela
 * preta nao e.
 */

void set_color(SDL_Renderer *sdl, unsigned rgb)
{
    (void)SDL_SetRenderDrawColor(sdl, static_cast<Uint8>((rgb >> 16) & 0xFFU),
                                 static_cast<Uint8>((rgb >> 8) & 0xFFU),
                                 static_cast<Uint8>(rgb & 0xFFU), 0xFFU);
}

void fill_rect(SDL_Renderer *sdl, unsigned rgb, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    set_color(sdl, rgb);
    SDL_Rect r{x, y, w, h};
    (void)SDL_RenderFillRect(sdl, &r);
}

int corner_inset(int radius, int dy)
{
    const int ry = radius - dy - 1;
    int dx = 0;

    while (dx < radius &&
           (radius - dx - 1) * (radius - dx - 1) + ry * ry > radius * radius) {
        dx++;
    }
    return dx;
}

void fill_rounded(SDL_Renderer *sdl, unsigned rgb, const UiRect &box, int radius)
{
    if (box.w <= 0 || box.h <= 0) {
        return;
    }
    if (radius <= 0 || radius * 2 > box.h || radius * 2 > box.w) {
        fill_rect(sdl, rgb, box.x, box.y, box.w, box.h);
        return;
    }

    set_color(sdl, rgb);

    for (int dy = 0; dy < box.h; dy++) {
        int inset = 0;

        if (dy < radius) {
            inset = corner_inset(radius, dy);
        } else if (dy >= box.h - radius) {
            inset = corner_inset(radius, box.h - dy - 1);
        }

        SDL_Rect r{box.x + inset, box.y + dy, box.w - 2 * inset, 1};

        if (r.w > 0) {
            (void)SDL_RenderFillRect(sdl, &r);
        }
    }
}

/* Anel de foco: quatro tiras, para nao comer o tile (§5.2). */
void draw_ring(SDL_Renderer *sdl, unsigned rgb, const UiRect &box, int thickness)
{
    if (thickness <= 0 || box.w <= 0 || box.h <= 0) {
        return;
    }
    fill_rect(sdl, rgb, box.x, box.y, box.w, thickness);
    fill_rect(sdl, rgb, box.x, box.y + box.h - thickness, box.w, thickness);
    fill_rect(sdl, rgb, box.x, box.y, thickness, box.h);
    fill_rect(sdl, rgb, box.x + box.w - thickness, box.y, thickness, box.h);
}

/* Rotulo da aba. O id vem do layout.json; o texto e o que o usuario le. */
/* Rotulo da aba, do catalogo de idioma. O id vem do layout.json e e' estavel;
 * o texto e' o que o usuario le'. Chave "tab.<id>". */
std::string tab_label(const I18n &i18n, const char *id)
{
    const std::string key = std::string("tab.") + id;

    return std::string(i18n.tr(key.c_str()));
}

void text_centered(SDL_Renderer *sdl, const Font &font, const std::string &text,
                   const UiRect &box, int px, unsigned rgb,
                   FontWeight weight = FontWeight::Regular)
{
    const int w = font.measure(text, px, weight);
    const int baseline = box.y + (box.h + font.ascent(px, weight)) / 2 - px / 4;

    (void)font.draw(sdl, text, box.x + (box.w - w) / 2, baseline, px, rgb,
                    box.w, weight);
}

/* --- icones -----------------------------------------------------------------
 *
 * Desenhados por retangulo/circulo, sem textura: sao ~40 px de lado e cabem
 * dentro do teto de VRAM da §4.1.6 sem ocupar um byte. Um PNG por icone
 * custaria import, cache e um caminho de erro a mais.
 *
 * O indicador de rede e' de **Wi-Fi** (arcos), nao de celular (barras
 * verticais): um portatil nao tem modem, e barras de celular mentem sobre o
 * que o aparelho sabe fazer. Cabo (USB gadget ECM) tem icone proprio.
 */
/* Arco estampado: percorre o angulo e carimba quadradinhos de `thickness`.
 *
 * Sem SDL2_gfx nao existe "desenhar arco"; a alternativa barata e' esta. Com
 * raio de 10-20 px o resultado e' identico a um arco vetorial.
 * Angulos em GRAUS, 0 = direita, 90 = cima (y cresce para baixo, por isso o
 * sinal negativo em sin).
 */
void stroke_arc(SDL_Renderer *sdl, unsigned rgb, int cx, int cy, int radius,
                int thickness, int from_deg, int to_deg)
{
    if (radius <= 0 || thickness <= 0 || to_deg <= from_deg) {
        return;
    }
    set_color(sdl, rgb);

    const int passos = (to_deg - from_deg) * 2 + 1;

    for (int i = 0; i <= passos; i++) {
        const double deg =
            from_deg + (static_cast<double>(to_deg - from_deg) * i) / passos;
        const double rad = deg * 3.14159265358979323846 / 180.0;
        const int x = cx + static_cast<int>(radius * std::cos(rad) + 0.5);
        const int y = cy - static_cast<int>(radius * std::sin(rad) + 0.5);

        SDL_Rect r{x - thickness / 2, y - thickness / 2, thickness, thickness};

        (void)SDL_RenderFillRect(sdl, &r);
    }
}

/* Wi-Fi: tres arcos concentricos abertos para CIMA + o ponto da base.
 *
 * A primeira versao desenhava tiras horizontais empilhadas — na tela isso le'
 * como BARRAS DE CELULAR, justamente o que o indicador nao deve ser (um
 * portatil nao tem modem). Arco e' arco.
 *
 * `signal_pct` < 0 = desconhecido (aceso); sem associacao, tudo em `off`.
 */
void draw_wifi(SDL_Renderer *sdl, int cx, int base_y, int size, unsigned rgb,
               int signal_pct, int up)
{
    const unsigned off = 0x3A3A3A;
    const int thickness = size / 9 > 1 ? size / 9 : 2;
    /* 0..4 arcos acesos conforme o sinal; desconhecido acende todos. */
    const int lit = up ? (signal_pct < 0 ? 3
                                         : 1 + (signal_pct * 3) / 100)
                       : 0;

    for (int i = 0; i < 3; i++) {
        /* r = size/3, 2*size/3... do menor (mais perto do ponto) para o maior */
        const int r = size / 3 + i * (size / 5);
        const unsigned cor = i < lit ? rgb : off;

        stroke_arc(sdl, cor, cx, base_y, r, thickness, 40, 140);
    }

    /* Ponto da base: sempre presente, aceso quando ha associacao. */
    const int d = thickness + 1;

    fill_rect(sdl, up ? rgb : off, cx - d / 2, base_y - d / 2, d, d);
}

void draw_ethernet(SDL_Renderer *sdl, int x, int y, int size, unsigned rgb,
                   int up)
{
    /* Tomada RJ45 estilizada: um retangulo com tres "pinos". */
    const unsigned colour = up ? rgb : 0x404040;

    fill_rect(sdl, colour, x, y, size, size / 2);
    for (int i = 0; i < 3; i++) {
        fill_rect(sdl, 0x101010, x + size / 6 + i * (size / 3),
                  y + size / 6, size / 8, size / 3);
    }
}

void draw_battery(SDL_Renderer *sdl, int x, int y, int w, int h, unsigned rgb,
                  int pct, int charging)
{
    if (pct < 0) {
        return; /* sem bateria conhecida: nao desenhar e' melhor que "0%" */
    }

    const int border = h / 8 > 1 ? h / 8 : 1;

    /* Corpo: contorno + terminal. */
    fill_rect(sdl, rgb, x, y, w, border);
    fill_rect(sdl, rgb, x, y + h - border, w, border);
    fill_rect(sdl, rgb, x, y, border, h);
    fill_rect(sdl, rgb, x + w - border, y, border, h);
    fill_rect(sdl, rgb, x + w, y + h / 3, border, h / 3);

    /* Carga. Vermelho abaixo de 15% (o aviso que importa), verde carregando. */
    const unsigned fill = charging ? 0x3FB950 : (pct <= 15 ? 0xE50914 : rgb);
    const int inner_w = w - 2 * border;
    const int inner_h = h - 2 * border;
    const int bar = inner_w * pct / 100;

    fill_rect(sdl, fill, x + border, y + border, bar, inner_h);

    /* Raio: um triangulo feito de tiras, sobre a carga. */
    if (charging) {
        const int cw = w / 3;
        const int cx = x + (w - cw) / 2;

        for (int i = 0; i < cw; i++) {
            fill_rect(sdl, 0x101010, cx + i, y + border + i, 2,
                      inner_h - 2 * i > 0 ? inner_h - 2 * i : 1);
        }
    }
}

void paint_status_right(SDL_Renderer *sdl, const Font &font,
                        const UiGeometry &geo, const UiState &ui)
{
    if (ui.status == nullptr || ui.i18n == nullptr) {
        return;
    }

    const UiStatus &st = *ui.status;
    const unsigned divider = giochi_theme_rgb(ui.theme->divider, 0x333333);
    const unsigned primary = giochi_theme_rgb(ui.theme->text_primary, 0xE5E5E5);
    const unsigned secondary =
        giochi_theme_rgb(ui.theme->text_secondary, 0xA0A0A0);

    const int gap = geo.gap;
    const int icon = geo.topbar.h / 3;
    const int mid = geo.topbar.h / 2;
    int x = geo.topbar.w - geo.margin;

    (void)divider;

    /* Da direita para a esquerda (§4.1): relogio, bateria+%, cabo, wi-fi. */
    if (st.valid_time) {
        char relogio[8];

        std::snprintf(relogio, sizeof(relogio), "%02d:%02d", st.hour, st.minute);

        const FontWeight peso =
            ui_text_weight(UI_TEXT_STATUS) == UI_WEIGHT_BOLD
                ? FontWeight::Bold
                : FontWeight::Regular;
        const int w = font.measure(relogio, geo.font_body, peso);

        x -= w;
        (void)font.draw(sdl, relogio, x,
                        (geo.topbar.h + font.ascent(geo.font_body, peso)) / 2,
                        geo.font_body, secondary, 0, peso);
        x -= gap;
    }

    if (st.battery_pct >= 0) {
        char pct[16];

        std::snprintf(pct, sizeof(pct), "%d%%", st.battery_pct);

        const FontWeight peso =
            ui_text_weight(UI_TEXT_STATUS) == UI_WEIGHT_BOLD
                ? FontWeight::Bold
                : FontWeight::Regular;
        const int text_w = font.measure(pct, geo.font_body, peso);

        x -= text_w;
        (void)font.draw(sdl, pct, x,
                        (geo.topbar.h + font.ascent(geo.font_body, peso)) / 2,
                        geo.font_body, secondary, 0, peso);
        x -= gap / 2;

        const int bw = icon * 2;

        x -= bw;
        draw_battery(sdl, x, mid - icon / 2, bw - icon / 3, icon, primary,
                     st.battery_pct, st.charging);
        x -= gap;
    }

    if (st.ethernet_up) {
        x -= icon;
        draw_ethernet(sdl, x, mid - icon / 2, icon, secondary, 1);
        x -= gap / 2;
    }

    if (st.wifi_present) {
        x -= icon;
        draw_wifi(sdl, x + icon / 2, mid + icon / 2 - 1, icon, secondary,
                  st.wifi_signal, st.wifi_up);
    }
}

void paint_topbar(SDL_Renderer *sdl, const Font &font, const UiGeometry &geo,
                  const UiState &ui)
{
    const unsigned surface = giochi_theme_rgb(ui.theme->surface, 0x1E1E1E);
    const unsigned divider = giochi_theme_rgb(ui.theme->divider, 0x333333);
    const unsigned accent = giochi_theme_rgb(ui.theme->accent, 0xE50914);
    const unsigned primary = giochi_theme_rgb(ui.theme->text_primary, 0xE5E5E5);
    const unsigned secondary =
        giochi_theme_rgb(ui.theme->text_secondary, 0xA0A0A0);

    fill_rect(sdl, surface, 0, 0, geo.topbar.w, geo.topbar.h);
    fill_rect(sdl, divider, 0, geo.topbar.h - geo.scale_pct / 50, geo.topbar.w,
              geo.scale_pct / 50);

    const int baseline = (geo.topbar.h + font.ascent(geo.font_tab)) / 2;

    for (int i = 0; i < geo.tab_count && i < GIOCHI_MAX_TABS; i++) {
        const UiRect &t = geo.tab[i];

        if (t.w <= 0) {
            continue;
        }
        const bool active = i == geo.tab_active;
        const std::string label =
            tab_label(*ui.i18n, giochi_layout_tab_id(ui.layout, i));
        const FontWeight peso = active ? FontWeight::Bold : FontWeight::Regular;

        (void)font.draw(sdl, label, t.x, baseline, geo.font_tab,
                        active ? primary : secondary, t.w, peso);

        if (active) {
            /* A sublinha acompanha a largura do texto NEGRITO — o mesmo que
             * ui_geometry_tabs usou para medir a celula, entao ela termina
             * onde a palavra termina (referencia: library-1280x720.png). */
            const int w = font.measure(label, geo.font_tab, peso);

            fill_rect(sdl, accent, t.x, t.y + t.h - geo.accent_h - 1, w,
                      geo.accent_h);
        }
    }

    paint_status_right(sdl, font, geo, ui);
}

/* Papel do tema -> cor. O papel vem de ui_tile_style(), que e' a fonte unica
 * da aparencia do tile: o preview-ui.py le os MESMOS papeis pelo
 * dump-geometry. Duas regras de desenho foi o que fez a grade virar parede de
 * caixas claras (§3.71). */
static unsigned cor_papel(const giochi_theme *t, giochi::UiRole papel)
{
    switch (papel) {
    case giochi::UI_ROLE_BACKGROUND:
        return giochi_theme_rgb(t->background, 0x0F0F0F);
    case giochi::UI_ROLE_SURFACE:
        return giochi_theme_rgb(t->surface, 0x1E1E1E);
    case giochi::UI_ROLE_ELEVATED:
        return giochi_theme_rgb(t->surface_elevated, 0x2A2A2A);
    case giochi::UI_ROLE_DIVIDER:
        return giochi_theme_rgb(t->divider, 0x333333);
    case giochi::UI_ROLE_ACCENT:
        return giochi_theme_rgb(t->accent, 0xE50914);
    case giochi::UI_ROLE_TEXT_PRIMARY:
        return giochi_theme_rgb(t->text_primary, 0xE5E5E5);
    case giochi::UI_ROLE_TEXT_SECONDARY:
        return giochi_theme_rgb(t->text_secondary, 0xA0A0A0);
    case giochi::UI_ROLE_NONE:
        break;
    }
    return 0;
}

/* --- grade da Biblioteca ---------------------------------------------------
 *
 * Detalhes que a §5.2 pede e que a primeira versao nao tinha:
 *   - scrollbar na sarjeta direita (largura do token, nao 2 px improvisados);
 *   - titulo recortado por PIXEL, nunca por caractere;
 *   - contagem de pacotes no canto, porque "0 pacotes" e' diagnostico.
 */
void paint_grid(SDL_Renderer *sdl, const Font &font, const UiGeometry &geo,
                const UiState &ui)
{
    const unsigned accent = giochi_theme_rgb(ui.theme->accent, 0xE50914);
    const unsigned secondary =
        giochi_theme_rgb(ui.theme->text_secondary, 0xA0A0A0);

    for (int i = 0; i < geo.tile_count; i++) {
        const UiRect &t = geo.tile[i];
        const int index = geo.first_index + i;
        const bool valid = ui.items != nullptr && index < ui.item_count &&
                           ui.items[index].valid;
        const giochi::UiTileStyle st =
            giochi::ui_tile_style(geo, i == geo.focus_slot, valid);

        fill_rounded(sdl, cor_papel(ui.theme, st.fill), t, geo.corner);

        if (st.inset != giochi::UI_ROLE_NONE) {
            const int recuo = geo.corner / 2 > 0 ? geo.corner / 2 : 1;
            const UiRect inner{t.x + recuo, t.y + recuo, t.w - 2 * recuo,
                               t.h - 2 * recuo};

            fill_rounded(sdl, cor_papel(ui.theme, st.inset), inner,
                         geo.corner - recuo > 0 ? geo.corner - recuo : 0);
        }

        if (st.outline != giochi::UI_ROLE_NONE) {
            /* Espessura por PAPEL: o foco usa o anel do tema, o tile comum usa
             * a borda. Usar o anel nos dois engrossava a grade inteira. */
            const int esp = i == geo.focus_slot
                                ? (geo.focus_ring > 0 ? geo.focus_ring : 1)
                                : (geo.border > 0 ? geo.border : 1);

            draw_ring(sdl, cor_papel(ui.theme, st.outline), t, esp);
        }

        if (ui.items != nullptr && index < ui.item_count) {
            const std::string &title = ui.items[index].title;
            const int baseline = t.y + t.h + st.title_dy;
            const int largura = t.w - st.pad > 0 ? t.w - st.pad : t.w;

            const FontWeight peso =
                ui_text_weight(UI_TEXT_TITLE) == UI_WEIGHT_BOLD
                    ? FontWeight::Bold
                    : FontWeight::Regular;

            (void)font.draw(sdl, title, t.x + st.pad, baseline, geo.font_title,
                            cor_papel(ui.theme, st.title), largura, peso);
        }
    }

    if (geo.focus_slot >= 0) {
        const giochi::UiTileStyle st =
            giochi::ui_tile_style(geo, true, true);

        if (st.ring > 0) {
            draw_ring(sdl, accent,
                      ui_geometry_focus_ring(geo, geo.focus_slot), st.ring);
        }
    }

    /* Scrollbar na sarjeta: so' quando ha mais de uma pagina. O trilho e' o
     * `divider`; o cursor, o acento. */
    if (ui.item_count > geo.capacity && geo.capacity > 0) {
        const int sb_w = geo.focus_ring;
        const int x = geo.screen_w - geo.inset - sb_w;
        const int total = geo.rows;
        const int pages = (ui.item_count + geo.capacity - 1) / geo.capacity;
        const int page = geo.capacity > 0 ? geo.first_index / geo.capacity : 0;
        const int cell_h = geo.tile_h + geo.title_h + geo.gap;
        const int track_h = total * cell_h - geo.gap;
        const int bar_h = track_h / (pages > 0 ? pages : 1);
        const int y = geo.topbar.h + geo.margin +
                      (pages > 1 ? (track_h - bar_h) * page / (pages - 1) : 0);

        fill_rect(sdl, giochi_theme_rgb(ui.theme->divider, 0x333333), x,
                  geo.topbar.h + geo.margin, sb_w, track_h);
        fill_rect(sdl, accent, x, y, sb_w, bar_h > geo.gap ? bar_h : geo.gap);
    }

    /* Contador: o numero de pacotes diz mais que a grade vazia.
     * A posicao vem de ui_geometry_count_rect — desenhar "logo abaixo da
     * topbar" punha o texto EM CIMA do primeiro tile (medido no preview). */
    if (ui.i18n != nullptr) {
        const std::string contagem =
            ui.i18n->tr_n("library.count", ui.item_count);
        const UiRect linha = ui_geometry_count_rect(geo);
        const int w = font.measure(contagem, geo.font_body);

        (void)font.draw(sdl, contagem, linha.x + linha.w - w,
                        linha.y + font.ascent(geo.font_body), geo.font_body,
                        secondary, linha.w);
    }
}

/* --- hint bar por par (botao, acao) ----------------------------------------
 *
 * A §4.2 pede uma LISTA de pares, com chip do botao, e nao uma frase: a frase
 * antiga juntava os rotulos numa unica string e colidia com a dica da direita.
 * Aqui cada item e' medido e o que nao couber e' omitido inteiro — nunca
 * cortado no meio.
 */
struct HintItem {
    const char *button; /* "A", "B", "Select", "Home" */
    const char *action; /* ja' traduzido */
};

void paint_hint_bar(SDL_Renderer *sdl, const Font &font, const UiGeometry &geo,
                    const UiState &ui)
{
    const unsigned bg = giochi_theme_rgb(ui.theme->hint_bar_bg, 0x181818);
    const unsigned divider = giochi_theme_rgb(ui.theme->divider, 0x333333);
    const unsigned elevated =
        giochi_theme_rgb(ui.theme->surface_elevated, 0x2A2A2A);
    const unsigned primary = giochi_theme_rgb(ui.theme->text_primary, 0xE5E5E5);
    const unsigned secondary =
        giochi_theme_rgb(ui.theme->text_secondary, 0xA0A0A0);

    fill_rect(sdl, bg, geo.hint_bar.x, geo.hint_bar.y, geo.hint_bar.w,
              geo.hint_bar.h);
    fill_rect(sdl, divider, geo.hint_bar.x, geo.hint_bar.y, geo.hint_bar.w,
              geo.scale_pct / 50);

    if (ui.hint.empty()) {
        return;
    }

    const int px = geo.font_body;
    /* Tecla em NEGRITO, acao em regular (ui_text_weight). Tudo o que mede ou
     * desenha usa o peso do seu papel — misturar os dois foi o que deixou o
     * texto fino na primeira versao. */
    const FontWeight peso_tecla =
        ui_text_weight(UI_TEXT_HINT_KEY) == UI_WEIGHT_BOLD ? FontWeight::Bold
                                                          : FontWeight::Regular;
    const FontWeight peso_acao =
        ui_text_weight(UI_TEXT_HINT_ACTION) == UI_WEIGHT_BOLD
            ? FontWeight::Bold
            : FontWeight::Regular;
    const int px_tecla = geo.font_body - 2;
    const int baseline =
        geo.hint_bar.y + (geo.hint_bar.h + font.ascent(px, peso_acao)) / 2;
    const int pad_x = geo.gap;
    const int pad_y = (geo.hint_bar.h - font.line_height(px, peso_acao)) / 3;
    const int chip_h = font.line_height(px_tecla, peso_tecla) + 2 * pad_y;

    /* O main monta a string no formato "A:Abrir|B:Voltar|...": pares ja'
     * traduzidos e separados, para o desenho nao conhecer idioma nem acao. */
    int x = geo.hint_bar.x + geo.margin;
    const int limit = geo.hint_bar.x + geo.hint_bar.w - geo.margin;
    std::size_t pos = 0;

    while (pos <= ui.hint.size()) {
        const std::size_t bar = ui.hint.find('|', pos);

        const std::string pair = ui.hint.substr(
            pos, bar == std::string::npos ? std::string::npos : bar - pos);

        if (!pair.empty()) {
            const std::size_t colon = pair.find(':');
            const std::string button = colon == std::string::npos
                                           ? pair
                                           : pair.substr(0, colon);
            const std::string action = colon == std::string::npos
                                           ? std::string()
                                           : pair.substr(colon + 1);

            const int bw = font.measure(button, px_tecla, peso_tecla);
            const int chip_w = bw + 2 * pad_x;
            const int aw =
                action.empty() ? 0 : font.measure(action, px, peso_acao);
            const int need = chip_w + (aw > 0 ? geo.gap / 2 + aw : 0);

            /* O ultimo item nao tem gap depois dele. */
            if (x + need > limit) {
                break; /* omite o resto inteiro: meio item e' pior */
            }

            fill_rounded(sdl, elevated,
                         UiRect{x, geo.hint_bar.y + (geo.hint_bar.h - chip_h) / 2,
                                chip_w, chip_h},
                         geo.corner / 2 > 1 ? geo.corner / 2 : 2);
            text_centered(sdl, font, button,
                          UiRect{x, geo.hint_bar.y, chip_w, geo.hint_bar.h},
                          px_tecla, primary, peso_tecla);

            x += chip_w;

            if (aw > 0) {
                x += geo.gap / 2;
                (void)font.draw(sdl, action, x, baseline, px, secondary, 0,
                                peso_acao);
                x += aw;
            }
            x += geo.gap * 2;
        }

        if (bar == std::string::npos) {
            break;
        }
        pos = bar + 1;
    }
}

/* --- Configuracoes ---------------------------------------------------------
 *
 * A lista vem pronta do main (rotulo traduzido + valor). O render NAO compara
 * com texto de item: a versao anterior fazia `strcmp(item, "Desligar")` para
 * decidir a acao, o que quebraria ao traduzir.
 */
void paint_settings(SDL_Renderer *sdl, const Font &font, const UiGeometry &geo,
                    const UiState &ui)
{
    const unsigned elevated =
        giochi_theme_rgb(ui.theme->surface_elevated, 0x2A2A2A);
    const unsigned surface = giochi_theme_rgb(ui.theme->surface, 0x1E1E1E);
    const unsigned accent = giochi_theme_rgb(ui.theme->accent, 0xE50914);
    const unsigned primary = giochi_theme_rgb(ui.theme->text_primary, 0xE5E5E5);
    const unsigned secondary =
        giochi_theme_rgb(ui.theme->text_secondary, 0xA0A0A0);

    const int pad = geo.margin;
    const int panel_w = geo.screen_w / 2;
    const UiRect panel{geo.margin * 2, geo.topbar.h + geo.margin, panel_w,
                       geo.content.h - 2 * geo.margin};

    fill_rounded(sdl, elevated, panel, geo.corner);
    draw_ring(sdl, accent, panel, geo.focus_ring);

    const FontWeight peso_titulo =
        ui_text_weight(UI_TEXT_PANEL_TITLE) == UI_WEIGHT_BOLD ? FontWeight::Bold
                                                             : FontWeight::Regular;
    const FontWeight peso_linha =
        ui_text_weight(UI_TEXT_MENU_ROW) == UI_WEIGHT_BOLD ? FontWeight::Bold
                                                          : FontWeight::Regular;
    const int title_baseline =
        panel.y + pad + font.ascent(geo.font_tab, peso_titulo);

    (void)font.draw(sdl,
                    ui.menu_title.empty() ? std::string("Configuracoes") :
                                            ui.menu_title,
                    panel.x + pad, title_baseline, geo.font_tab, primary,
                    panel.w - 2 * pad, peso_titulo);
    fill_rect(sdl, accent, panel.x + pad,
              title_baseline + font.line_height(geo.font_body) / 2,
              panel.w / 5, geo.accent_h / 2 > 0 ? geo.accent_h / 2 : 1);

    const int row_h = font.line_height(geo.font_tab) + geo.gap;
    int y = title_baseline + font.line_height(geo.font_tab) + geo.gap;

    for (int i = 0; i < ui.menu_count; i++) {
        const UiMenuItem &item = ui.menu[i];

        if (y + row_h > panel.y + panel.h - pad) {
            break; /* nao desenha fora do painel */
        }

        const bool sel = i == ui.settings_index;
        const UiRect row{panel.x + pad / 2, y, panel.w - pad, row_h};

        if (sel) {
            fill_rounded(sdl, surface, row, geo.corner);
            fill_rect(sdl, accent, row.x, row.y + geo.gap / 2, geo.accent_h,
                      row.h - geo.gap);
        }

        const int row_baseline =
            row.y + (row_h + font.ascent(geo.font_body, peso_linha)) / 2 -
            geo.font_body / 8;

        (void)font.draw(sdl, item.label, row.x + pad, row_baseline,
                        geo.font_body, sel ? primary : secondary,
                        row.w / 2, peso_linha);

        /* Valor a' direita; seta quando ha submenu. */
        const std::string right = item.has_submenu ? std::string(">") :
                                                     item.value;

        if (!right.empty()) {
            const int w = font.measure(right, geo.font_body, peso_linha);

            (void)font.draw(sdl, right, row.x + row.w - pad - w, row_baseline,
                            geo.font_body, sel ? primary : secondary, 0,
                            peso_linha);
        }
        y += row_h;
    }
}

/* --- abas sem conteudo ----------------------------------------------------
 *
 * Loja e Ports apontam para apps que ainda nao existem (docs/APPS.md). Dizer
 * isso e' diagnostico; tela vazia nao e'. Home e Musica mostram a origem do
 * dado pelo mesmo motivo.
 */
void paint_message_tab(SDL_Renderer *sdl, const Font &font,
                       const UiGeometry &geo, const UiState &ui,
                       const char *titulo, const char *linha1,
                       const char *linha2)
{
    const unsigned elevated =
        giochi_theme_rgb(ui.theme->surface_elevated, 0x2A2A2A);
    const unsigned primary = giochi_theme_rgb(ui.theme->text_primary, 0xE5E5E5);
    const unsigned secondary =
        giochi_theme_rgb(ui.theme->text_secondary, 0xA0A0A0);
    const unsigned accent = giochi_theme_rgb(ui.theme->accent, 0xE50914);
    const int pad = geo.margin;

    const UiRect panel{geo.margin, geo.topbar.h + geo.margin,
                       geo.screen_w - 2 * geo.margin,
                       geo.content.h - 2 * geo.margin};

    fill_rounded(sdl, elevated, panel, geo.corner);
    fill_rect(sdl, accent, panel.x + pad, panel.y + pad, panel.w / 8,
              geo.accent_h);

    const FontWeight peso_titulo =
        ui_text_weight(UI_TEXT_PANEL_TITLE) == UI_WEIGHT_BOLD ? FontWeight::Bold
                                                             : FontWeight::Regular;
    int baseline =
        panel.y + pad + geo.accent_h + font.ascent(geo.font_tab, peso_titulo);
    const int text_w = panel.w - 2 * pad;

    (void)font.draw(sdl, titulo, panel.x + pad, baseline, geo.font_tab, primary,
                    text_w, peso_titulo);

    baseline += font.line_height(geo.font_tab) + geo.gap / 2;
    if (linha1 != nullptr && *linha1 != '\0') {
        (void)font.draw(sdl, linha1, panel.x + pad, baseline, geo.font_body,
                        secondary, text_w);
        baseline += font.line_height(geo.font_body) + geo.gap / 4;
    }
    if (linha2 != nullptr && *linha2 != '\0') {
        (void)font.draw(sdl, linha2, panel.x + pad, baseline, geo.font_body,
                        secondary, text_w);
    }
}

void paint_ui(SDL_Renderer *sdl, const Font &font, const UiGeometry &geo,
              const UiState &ui)
{
    const I18n &i18n = *ui.i18n;
    const char *tab_id = giochi_layout_tab_id(ui.layout, geo.tab_active);

    if (std::strcmp(tab_id, "library") == 0) {
        if (ui.item_count <= 0) {
            paint_message_tab(sdl, font, geo, ui, i18n.tr("tab.library"),
                              i18n.tr("library.empty"),
                              i18n.tr("library.empty_hint"));
        } else {
            paint_grid(sdl, font, geo, ui);
        }
    } else if (std::strcmp(tab_id, "home") == 0) {
        /* Novidade da Loja tem prioridade sobre a dica: e' a unica informacao
         * que muda sozinha na Home. Sem news.json, cai nas dicas compiladas. */
        const bool tem_novidade = !ui.news_title.empty();

        paint_message_tab(sdl, font, geo, ui, i18n.tr("tab.home"),
                          tem_novidade ? ui.news_title.c_str()
                                       : i18n.tr("home.tip.scan"),
                          tem_novidade ? ui.news_body.c_str()
                                       : i18n.tr("home.tip.usb"));
    } else if (std::strcmp(tab_id, "music") == 0) {
        paint_message_tab(sdl, font, geo, ui, i18n.tr("tab.music"),
                          i18n.tr("music.empty"), "");
    } else if (std::strcmp(tab_id, "store") == 0) {
        paint_message_tab(sdl, font, geo, ui, i18n.tr("tab.store"),
                          i18n.tr("store.missing"), "");
    } else {
        paint_message_tab(sdl, font, geo, ui, i18n.tr("tab.ports"),
                          i18n.tr("ports.missing"), "");
    }

    paint_topbar(sdl, font, geo, ui);
    paint_hint_bar(sdl, font, geo, ui);

    if (ui.settings_open) {
        paint_settings(sdl, font, geo, ui);
    }
}

class SdlRenderer final : public Renderer {
public:
    SdlRenderer(SDL_Window *window, SDL_Renderer *sdl)
        : window_(window), sdl_(sdl) {}

    ~SdlRenderer() override
    {
        if (target_ != nullptr) {
            SDL_DestroyTexture(target_);
            target_ = nullptr;
        }
        splash_.release();
        if (sdl_ != nullptr) {
            SDL_DestroyRenderer(sdl_);
        }
        if (window_ != nullptr) {
            SDL_DestroyWindow(window_);
        }
        SDL_Quit();
    }

    int frame(const UiState &ui) override
    {
        SDL_Event event;

        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) {
                quit_ = true;
            }
            if (event.type == SDL_KEYDOWN) {
                dismiss_splash();
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    quit_ = true;
                }
            }
            if (event.type == SDL_JOYBUTTONDOWN ||
                event.type == SDL_CONTROLLERBUTTONDOWN) {
                dismiss_splash();
            }
        }

        if (ui.layout == nullptr) {
            return 0;
        }

        /* Uma vez por tamanho de saida: decide girar ou nao (ui_present) e
         * recalcula a geometria sobre o tamanho LOGICO do layout. */
        calcular_geometria(sdl_, ui);

        /* Com rotacao, tudo e' desenhado numa textura do tamanho logico e
         * girado no fim — senao o painel em retrato mostra a UI deitada. */
        if (target_ != nullptr) {
            (void)SDL_SetRenderTarget(sdl_, target_);
        }

        unsigned rgb = 0x0F0F0F;
        if (ui.theme != nullptr) {
            rgb = giochi_theme_rgb(ui.theme->background, rgb);
        }

        (void)SDL_SetRenderDrawColor(sdl_,
                                     static_cast<Uint8>((rgb >> 16) & 0xFFU),
                                     static_cast<Uint8>((rgb >> 8) & 0xFFU),
                                     static_cast<Uint8>(rgb & 0xFFU), 0xFFU);
        (void)SDL_RenderClear(sdl_);

        if (splash_frames_ != 0) {
            splash_.draw(sdl_);
        } else {
            paint_ui(sdl_, font_, geo_, ui);
        }

        if (target_ != nullptr) {
            (void)SDL_SetRenderTarget(sdl_, nullptr);
        }
        apresentar(sdl_);

        if (splash_frames_ > 0) {
            splash_frames_--;
            if (splash_frames_ == 0) {
                splash_.release();
                GLOG_INFO("splash: fim; assumindo a UI");
            }
        }
        /* splash_frames_ < 0 = modo permanente: nao decrementa. */
        return 0;
    }

    bool should_quit() const override { return quit_; }
    const char *name() const override { return "sdl2"; }

    /* Carrega o atlas de fonte. Sem ele a UI ainda desenha as caixas: e
     * diagnostico, nao tela preta. */
    void load_font(const char *base_dir)
    {
        if (!font_.load(sdl_, base_dir)) {
            GLOG_WARN("render: fonte ausente; a UI sai sem rotulo");
        }
    }

    void load_splash(const char *base_dir)
    {
        if (!splash_.load(sdl_, base_dir)) {
            GLOG_WARN("render: splash ausente em '%s'; a tela cai direto na UI",
                      base_dir != nullptr ? base_dir : "/usr/share/giochi");
            return;
        }
        splash_frames_ = splash_frames_from_env(180);
        if (splash_frames_ == 0) {
            splash_.release();
            return;
        }
        /* Sempre LOGADO: se este numero nao aparece no giochi-boot.log, o
         * backend SDL2 nem abriu, e o problema esta antes do desenho. */
        if (splash_frames_ < 0) {
            GLOG_INFO("splash: modo permanente (GIOCHI_SPLASH_ALWAYS=1)");
        } else {
            GLOG_INFO("splash: exibindo %d quadros", splash_frames_);
        }
    }

    /* Qualquer entrada do usuario encerra o splash — menos no modo permanente,
     * que existe justamente para ver se o SDL desenha. */
    void dismiss_splash()
    {
        if (splash_frames_ > 0) {
            splash_frames_ = 0;
            splash_.release();
        }
    }

private:
    /* A geometria usa o tamanho LOGICO (o do layout, 1280x720 na X55), nunca a
     * saida fisica do framebuffer. Medido no aparelho: o painel da X55 entrega
     * 720x1280 em retrato, e calcular a geometria sobre isso dava escala 2,66x,
     * uma coluna e a UI em pe' — o "layout bugado" de 2026-10-31.
     * A correcao da orientacao fica em `apresentar()`. */
    static bool mesma_metrica(const UiThemeMetrics &a, const UiThemeMetrics &b)
    {
        return a.corner_px == b.corner_px && a.border_px == b.border_px &&
               a.focus_ring_px == b.focus_ring_px &&
               a.topbar_px == b.topbar_px &&
               a.hint_bar_px == b.hint_bar_px && a.density == b.density &&
               a.cover_pct == b.cover_pct && a.hints == b.hints &&
               a.title_scale_pct == b.title_scale_pct;
    }

    void calcular_geometria(SDL_Renderer *sdl, const UiState &ui)
    {
        const int log_w = ui.layout->width;
        const int log_h = ui.layout->height;

        const UiThemeMetrics m =
            ui.theme_metrics != nullptr ? *ui.theme_metrics : UiThemeMetrics{};

        /* O tema tambem invalida a geometria: densidade muda o gap, logo muda
         * quantas colunas cabem. Sem esta comparacao, trocar de tema a quente
         * deixaria a grade com a contagem antiga. */
        if (!geo_valida || log_w != geo_.screen_w || log_h != geo_.screen_h ||
            !mesma_metrica(metricas_, m)) {
            ui_geometry_base(&geo_, log_w, log_h);
            ui_geometry_apply_theme(&geo_, m);
            measure_tabs(ui);
            metricas_ = m;
            geo_valida = true;
        }

        ui_geometry_grid(&geo_, ui.item_count, ui.focus_index);

        if (geo_.tab_active != ui.current_tab) {
            geo_.tab_active = ui.current_tab;
        }

        /* A apresentacao depende da saida REAL; recalcula quando muda. */
        int out_w = 0;
        int out_h = 0;

        if (SDL_GetRendererOutputSize(sdl, &out_w, &out_h) != 0 || out_w <= 0 ||
            out_h <= 0) {
            out_w = log_w;
            out_h = log_h;
        }
        if (!apresentacao_pronta || out_w != out_visto_w ||
            out_h != out_visto_h) {
            preparar_apresentacao(sdl, ui, out_w, out_h);
        }
    }

    /* Decides once per output size: draw offscreen and rotate, or draw direct.
     * The angle comes from GIOCHI_ROTATE (override) or GIOCHI_ROTATION (read
     * from device.conf by S20graphics). */
    int rotacao_pedida(const UiState &ui) const
    {
        /* 1. /boot/rotate: o campo decide o sentido sem recompilar. O
         *    S20graphics ja' leu e exportou; ler de novo aqui mantem o
         *    contrato se alguem subir o Launcher a mao. */
        if (const char *manual = std::getenv("GIOCHI_ROTATE");
            manual != nullptr && *manual != '\0') {
            return std::atoi(manual);
        }

        /* 2. /etc/giochi/device.conf: a MESMA fonte que o S20graphics usa, lida
         *    do arquivo (nao do ambiente). Depender de variavel de ambiente
         *    aqui significa "quem me iniciou decide a orientacao" — e o
         *    giochi-init pode ser iniciado pelo rcS, pelo S99launcher ou pelo
         *    inittab. Medido: foi assim que a UI saiu em pe' uma vez.
         *    Regra da §3.47: dado que vive dentro da imagem se le' da imagem. */
        {
            char valor[16] = {0};
            const char *cfg = std::getenv("GIOCHI_DEVICE_CONF");
            const char *path = (cfg != nullptr && *cfg != '\0')
                                   ? cfg : GIOCHI_DEVICECONF_PATH;

            if (giochi_deviceconf_get(path, "GIOCHI_ROTATION", valor,
                                      sizeof(valor)) == 0) {
                return std::atoi(valor);
            }
        }

        /* 3. Ultimo recurso: o ambiente (imagens antigas, teste de host). */
        const char *dev = std::getenv("GIOCHI_ROTATION");

        if (dev != nullptr && *dev != '\0') {
            return std::atoi(dev);
        }
        /* Sem informacao: deduz pelo layout. "landscape" num painel em retrato
         * precisa de 90/270, mas nao da' para saber o sentido — 270 e' o valor
         * do DTB da X55 e o unico aparelho em retrato hoje. */
        (void)ui;
        return 0;
    }

    void preparar_apresentacao(SDL_Renderer *sdl, const UiState &ui, int out_w,
                               int out_h)
    {
        const int log_w = geo_.screen_w;
        const int log_h = geo_.screen_h;
        const int rot = rotacao_pedida(ui);
        const giochi::UiPresentation p =
            giochi::ui_present(rot, log_w, log_h, out_w, out_h);

        out_visto_w = out_w;
        out_visto_h = out_h;
        apresentacao_pronta = true;

        if (target_ != nullptr) {
            SDL_DestroyTexture(target_);
            target_ = nullptr;
        }

        if (!p.ok) {
            /* Melhor reto do que cortado — e o log diz por que. */
            GLOG_WARN("render: sem rotacao (%s): pedido %d, layout %dx%d, "
                      "saida %dx%d",
                      p.motivo, rot, log_w, log_h, out_w, out_h);
            angulo_ = 0;
            dst_ = SDL_Rect{0, 0, out_w, out_h};
            return;
        }

        angulo_ = p.angle;
        dst_ = SDL_Rect{p.dst_x, p.dst_y, p.dst_w, p.dst_h};

        if (p.need_target == 0) {
            return;
        }

        target_ = SDL_CreateTexture(sdl, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_TARGET, log_w, log_h);
        if (target_ == nullptr) {
            GLOG_WARN("render: sem textura intermediaria (%s); desenhando "
                      "reto na saida %dx%d",
                      SDL_GetError(), out_w, out_h);
            angulo_ = 0;
            dst_ = SDL_Rect{0, 0, out_w, out_h};
            return;
        }
        GLOG_INFO("render: desenhando %dx%d e girando %d graus para a saida "
                  "%dx%d (dst %d,%d %dx%d)",
                  log_w, log_h, angulo_, out_w, out_h, dst_.x, dst_.y, dst_.w,
                  dst_.h);
    }

    void apresentar(SDL_Renderer *sdl)
    {
        if (target_ == nullptr) {
            (void)SDL_RenderPresent(sdl);
            return;
        }

        (void)SDL_SetRenderTarget(sdl, nullptr);
        (void)SDL_SetRenderDrawColor(sdl, 0, 0, 0, 0xFF);
        (void)SDL_RenderClear(sdl);
        (void)SDL_RenderCopyEx(sdl, target_, nullptr, &dst_,
                               static_cast<double>(angulo_), nullptr,
                               SDL_FLIP_NONE);
        (void)SDL_RenderPresent(sdl);
    }

    void measure_tabs(const UiState &ui)
    {
        int widths[GIOCHI_MAX_TABS] = {0};

        static const I18n kVazio;

        const I18n &i18n = ui.i18n != nullptr ? *ui.i18n : kVazio;

        for (int i = 0; i < ui.layout->tab_count && i < GIOCHI_MAX_TABS; i++) {
            /* Medido em NEGRITO de proposito: a aba ativa e' negrita e a
             * inativa nao. Medir pelo regular faria o negrito transbordar a
             * celula; medir pelo negrito deixa folga na inativa, que e'
             * invisivel. E nao faz a barra tremer ao trocar de aba. */
            widths[i] = font_.measure(
                tab_label(i18n, giochi_layout_tab_id(ui.layout, i)),
                geo_.font_tab, FontWeight::Bold);
        }
        ui_geometry_tabs(&geo_, widths, ui.layout->tab_count, ui.current_tab);
    }

    SDL_Window *window_ = nullptr;
    SDL_Renderer *sdl_ = nullptr;
    SplashImage splash_;
    Font font_;
    UiGeometry geo_;
    bool geo_valida = false;
    UiThemeMetrics metricas_{};
    SDL_Texture *target_ = nullptr;
    SDL_Rect dst_{0, 0, 0, 0};
    int angulo_ = 0;
    int out_visto_w = 0;
    int out_visto_h = 0;
    bool apresentacao_pronta = false;
    int splash_frames_ = 0;
    bool quit_ = false;
};

}  // namespace

static Renderer *open_sdl(const giochi_layout &layout, std::string *err)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        if (err != nullptr) {
            *err = std::string("SDL_Init: ") + SDL_GetError();
        }
        return nullptr;
    }

    SDL_Window *window = SDL_CreateWindow("Giochi Konsol OS",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          layout.width, layout.height,
                                          SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (window == nullptr) {
        if (err != nullptr) {
            *err = std::string("SDL_CreateWindow: ") + SDL_GetError();
        }
        SDL_Quit();
        return nullptr;
    }

    SDL_Renderer *sdl = SDL_CreateRenderer(window, -1,
                                           SDL_RENDERER_ACCELERATED);
    if (sdl == nullptr) {
        sdl = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (sdl == nullptr) {
        if (err != nullptr) {
            *err = std::string("SDL_CreateRenderer: ") + SDL_GetError();
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        return nullptr;
    }

    /* Um lugar, no log, que diz qual driver de video o SDL escolheu, qual
     * renderizador saiu e em que tamanho. E' o primeiro dado que separa
     * "o SDL nao abriu" de "abriu e desenhou no lugar errado". */
    {
        const char *driver = SDL_GetCurrentVideoDriver();
        SDL_RendererInfo info = {};
        int out_w = 0;
        int out_h = 0;

        (void)SDL_GetRendererOutputSize(sdl, &out_w, &out_h);
        (void)SDL_GetRendererInfo(sdl, &info);
        GLOG_INFO("render: SDL2 driver=%s renderizador=%s saida=%dx%d "
                  "layout=%dx%d",
                  driver != nullptr ? driver : "?",
                  info.name != nullptr ? info.name : "?", out_w, out_h,
                  layout.width, layout.height);
    }

    auto *renderer = new SdlRenderer(window, sdl);

    /* Bootsplash: opcional e silencioso quando ausente (ver BOOTSPLASH.md). */
    if (std::getenv("GIOCHI_NO_SPLASH") == nullptr) {
        const char *base = std::getenv("GIOCHI_BOOTLOGO_DIR");
        renderer->load_splash(base);
    }

    /* Fonte: idem, com GIOCHI_FONT_DIR para apontar outro atlas. */
    const char *font_dir = std::getenv("GIOCHI_FONT_DIR");

    renderer->load_font(font_dir);
    return renderer;
}

/* Quando a UI nao consegue abrir o video, o S20graphics ja' desvinculou o
 * fbcon (para o cursor nao piscar por cima do desenho). O resultado, medido no
 * aparelho, era tela preta e nenhuma pista — o log existia, mas so' no serial
 * e no cartao.
 *
 * Aqui o console volta: re-bindando o vtcon, as mensagens do boot (que o rcS
 * ja' duplica com `tee`) reaparecem na tela. Custa nada quando o video
 * funciona, porque so' roda no caminho de falha.
 * GIOCHI_KEEP_FBCON=1 desliga (a tela fica preta de proposito).
 */
void devolver_console_de_video(const char *motivo)
{
    const char *keep = std::getenv("GIOCHI_KEEP_FBCON");

    if (keep != nullptr && std::strcmp(keep, "1") == 0) {
        return;
    }

    for (int i = 0; i < 8; i++) {
        char caminho[64];
        std::snprintf(caminho, sizeof(caminho),
                      "/sys/class/vtconsole/vtcon%d/bind", i);
        FILE *fp = std::fopen(caminho, "w");
        if (fp == nullptr) {
            continue;
        }
        (void)std::fputs("1\n", fp);
        (void)std::fclose(fp);
    }

    /* Escrever no VT e' o que realmente aparece: sem `console=tty0` no
     * bootargs o printk vai so' para o serial, entao re-bindar o fbcon sozinho
     * NAO mostra nada. Medido: o bootargs desta imagem tem apenas
     * `console=ttyS2`. Por isso a mensagem vai direto para /dev/tty1 — que e'
     * o que o usuario ve. */
    const char *vts[] = {"/dev/tty1", "/dev/tty0", "/dev/console"};

    for (const char *vt : vts) {
        FILE *fp = std::fopen(vt, "w");

        if (fp == nullptr) {
            continue;
        }
        (void)std::fprintf(fp,
                           "\n"
                           "  Giochi Konsol OS v1.2\n"
                           "\n"
                           "  A interface grafica nao iniciou.\n"
                           "  Motivo: %s\n"
                           "\n"
                           "  Diagnostico completo:\n"
                           "    particao GIOCHI_BOOT (a primeira), arquivo\n"
                           "    giochi-boot.log — procure as linhas 'sonda:'\n"
                           "    e 'render:'.\n"
                           "\n",
                           (motivo != nullptr && *motivo != '\0') ? motivo
                                                                 : "desconhecido");
        (void)std::fclose(fp);
        return;
    }
}

Renderer *render_open(const giochi_layout &layout, std::string *err)
{
    std::string motivo;
    Renderer *renderer = open_sdl(layout, &motivo);

    if (renderer == nullptr) {
        /* O motivo PRECISA aparecer: sem ele o log do aparelho só dizia
         * "SDL2 indisponivel" e a investigação foi parar em /dev/dri (que
         * estava certo) em vez de na compilação do SDL2 — que saiu sem o
         * backend KMSDRM porque o Kconfig descartou o símbolo em silêncio
         * (AGENTS.md §3.55). */
        devolver_console_de_video(motivo.c_str());
        GLOG_WARN("render: SDL2 indisponivel (%s); usando backend headless",
                  motivo.empty() ? "motivo desconhecido" : motivo.c_str());
        if (err != nullptr) {
            *err = motivo;
        }
        return render_open_null(layout);
    }
    return renderer;
}

}  // namespace giochi

#else /* !GIOCHI_HAVE_SDL2 */

namespace giochi {

Renderer *render_open(const giochi_layout &layout, std::string *err)
{
    if (err != nullptr) {
        *err = "SDL2 nao compilado";
    }
    GLOG_WARN("render: compilado sem GIOCHI_HAVE_SDL2; usando headless");
    return render_open_null(layout);
}

}  // namespace giochi

#endif /* GIOCHI_HAVE_SDL2 */
