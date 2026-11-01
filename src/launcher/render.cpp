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
const char *tab_label(const char *id)
{
    if (std::strcmp(id, "music") == 0) {
        return "Musica";
    }
    if (std::strcmp(id, "library") == 0) {
        return "Biblioteca";
    }
    if (std::strcmp(id, "home") == 0) {
        return "Home";
    }
    if (std::strcmp(id, "store") == 0) {
        return "Loja";
    }
    if (std::strcmp(id, "ports") == 0) {
        return "Ports";
    }
    return id;
}

void text_centered(SDL_Renderer *sdl, const Font &font, const std::string &text,
                   const UiRect &box, int px, unsigned rgb)
{
    const int w = font.measure(text, px);
    const int baseline = box.y + (box.h + font.ascent(px)) / 2 - px / 4;

    (void)font.draw(sdl, text, box.x + (box.w - w) / 2, baseline, px, rgb, box.w);
}

void paint_topbar(SDL_Renderer *sdl, const Font &font, const UiGeometry &geo,
                  const UiState &ui)
{
    const unsigned surface = giochi_theme_rgb(ui.theme->surface, 0x1E1E1E);
    const unsigned divider = giochi_theme_rgb(ui.theme->divider, 0x333333);
    const unsigned accent = giochi_theme_rgb(ui.theme->accent, 0xE50914);
    const unsigned primary =
        giochi_theme_rgb(ui.theme->text_primary, 0xE5E5E5);
    const unsigned secondary =
        giochi_theme_rgb(ui.theme->text_secondary, 0xA0A0A0);

    fill_rect(sdl, surface, 0, 0, geo.topbar.w, geo.topbar.h);
    fill_rect(sdl, divider, 0, geo.topbar.h - geo.scale_pct / 50, geo.topbar.w,
              geo.scale_pct / 50);

    /* Baseline das abas: centralizado verticalmente na barra. */
    const int baseline = (geo.topbar.h + font.ascent(geo.font_tab)) / 2;

    for (int i = 0; i < geo.tab_count && i < GIOCHI_MAX_TABS; i++) {
        const UiRect &t = geo.tab[i];

        if (t.w <= 0) {
            continue;
        }
        const bool active = i == geo.tab_active;
        const char *label = tab_label(giochi_layout_tab_id(ui.layout, i));

        (void)font.draw(sdl, label, t.x, baseline, geo.font_tab,
                        active ? primary : secondary, t.w);

        if (active) {
            /* Sublinhado alinhado ao TEXTO, nao ao padding (§4.1). */
            const int w = font.measure(label, geo.font_tab);

            fill_rect(sdl, accent, t.x, t.y + t.h - geo.accent_h - 1, w,
                      geo.accent_h);
        }
    }
}

void paint_empty_grid(SDL_Renderer *sdl, const Font &font, const UiGeometry &geo,
                      const UiState &ui, const std::string &text)
{
    const unsigned secondary =
        giochi_theme_rgb(ui.theme->text_secondary, 0xA0A0A0);
    const UiRect box{geo.inset, geo.topbar.h, geo.screen_w - 2 * geo.inset,
                     geo.content.h};

    text_centered(sdl, font, text, box, geo.font_body, secondary);
}

void paint_grid(SDL_Renderer *sdl, const Font &font, const UiGeometry &geo,
                const UiState &ui)
{
    const unsigned elevated =
        giochi_theme_rgb(ui.theme->surface_elevated, 0x2A2A2A);
    const unsigned surface = giochi_theme_rgb(ui.theme->surface, 0x1E1E1E);
    const unsigned accent = giochi_theme_rgb(ui.theme->accent, 0xE50914);
    const unsigned primary =
        giochi_theme_rgb(ui.theme->text_primary, 0xE5E5E5);
    const unsigned secondary =
        giochi_theme_rgb(ui.theme->text_secondary, 0xA0A0A0);

    for (int i = 0; i < geo.tile_count; i++) {
        const UiRect &t = geo.tile[i];
        const int index = geo.first_index + i;
        const bool valid =
            ui.items != nullptr && index < ui.item_count && ui.items[index].valid;

        /* Placeholder da capa: o thumb .rgba so existe depois que o
         * thumbgen rodar; um bloco em `surface` ja comunica "tile vazio"
         * (a proposta §5.2 pede o mesmo para o hero sem capa). */
        fill_rounded(sdl, elevated, t, geo.corner);
        if (!valid) {
            const UiRect inner{t.x + geo.corner, t.y + geo.corner,
                               t.w - 2 * geo.corner, t.h - 2 * geo.corner};

            fill_rounded(sdl, surface, inner, geo.corner);
        }

        if (ui.items != nullptr && index < ui.item_count) {
            const int baseline = t.y + t.h + font.ascent(geo.font_title) +
                                 geo.gap / 2;

            (void)font.draw(sdl, ui.items[index].title, t.x, baseline,
                            geo.font_title, valid ? primary : secondary, t.w);
        }
    }

    if (geo.focus_slot >= 0) {
        draw_ring(sdl, accent, ui_geometry_focus_ring(geo, geo.focus_slot),
                  geo.focus_ring);
    }
}

void paint_placeholder_tab(SDL_Renderer *sdl, const Font &font,
                           const UiGeometry &geo, const UiState &ui,
                           const char *text)
{
    const unsigned elevated =
        giochi_theme_rgb(ui.theme->surface_elevated, 0x2A2A2A);
    const unsigned primary =
        giochi_theme_rgb(ui.theme->text_primary, 0xE5E5E5);
    const unsigned secondary =
        giochi_theme_rgb(ui.theme->text_secondary, 0xA0A0A0);
    const int pad = geo.margin;

    const UiRect panel{geo.margin, geo.topbar.h + geo.margin,
                       geo.screen_w - 2 * geo.margin,
                       geo.content.h - 2 * geo.margin};

    fill_rounded(sdl, elevated, panel, geo.corner);

    const int baseline = panel.y + pad + font.ascent(geo.font_tab);
    const int id = geo.tab_active;

    (void)font.draw(sdl, tab_label(giochi_layout_tab_id(ui.layout, id)),
                    panel.x + pad, baseline, geo.font_tab, primary,
                    panel.w - 2 * pad);
    (void)font.draw(sdl, text, panel.x + pad,
                    baseline + font.line_height(geo.font_body) + geo.gap / 2,
                    geo.font_body, secondary, panel.w - 2 * pad);
}

void paint_hint_bar(SDL_Renderer *sdl, const Font &font, const UiGeometry &geo,
                    const UiState &ui)
{
    const unsigned bg = giochi_theme_rgb(ui.theme->hint_bar_bg, 0x181818);
    const unsigned divider = giochi_theme_rgb(ui.theme->divider, 0x333333);
    const unsigned secondary =
        giochi_theme_rgb(ui.theme->text_secondary, 0xA0A0A0);

    fill_rect(sdl, bg, geo.hint_bar.x, geo.hint_bar.y, geo.hint_bar.w,
              geo.hint_bar.h);
    fill_rect(sdl, divider, geo.hint_bar.x, geo.hint_bar.y, geo.hint_bar.w,
              geo.scale_pct / 50);

    if (ui.hint.empty()) {
        return;
    }

    const int baseline =
        geo.hint_bar.y + (geo.hint_bar.h + font.ascent(geo.font_body)) / 2;

    (void)font.draw(sdl, ui.hint, geo.hint_bar.x + geo.margin, baseline,
                    geo.font_body, secondary,
                    geo.hint_bar.w - 2 * geo.margin);
}

void paint_settings(SDL_Renderer *sdl, const Font &font, const UiGeometry &geo,
                    const UiState &ui)
{
    const unsigned elevated =
        giochi_theme_rgb(ui.theme->surface_elevated, 0x2A2A2A);
    const unsigned accent = giochi_theme_rgb(ui.theme->accent, 0xE50914);
    const unsigned primary =
        giochi_theme_rgb(ui.theme->text_primary, 0xE5E5E5);

    const int pad = geo.margin;
    const UiRect panel{geo.margin * 2, geo.topbar.h + geo.margin,
                       geo.screen_w / 2, geo.content.h - 2 * geo.margin};

    fill_rounded(sdl, elevated, panel, geo.corner);
    draw_ring(sdl, accent, panel, geo.focus_ring);

    (void)font.draw(sdl, "Configuracoes", panel.x + pad,
                    panel.y + pad + font.ascent(geo.font_tab), geo.font_tab,
                    primary, panel.w - 2 * pad);
    (void)font.draw(sdl,
                    "Item " + std::to_string(ui.settings_index + 1) +
                        " (overlay)",
                    panel.x + pad,
                    panel.y + pad + font.ascent(geo.font_tab) +
                        font.line_height(geo.font_body) + geo.gap,
                    geo.font_body, primary, panel.w - 2 * pad);
}

void paint_ui(SDL_Renderer *sdl, const Font &font, const UiGeometry &geo,
              const UiState &ui)
{
    const char *tab_id = giochi_layout_tab_id(ui.layout, geo.tab_active);

    if (std::strcmp(tab_id, "library") == 0) {
        if (ui.item_count <= 0) {
            paint_empty_grid(sdl, font, geo, ui,
                             "Copie um pacote .giochi via USB");
        } else {
            paint_grid(sdl, font, geo, ui);
        }
    } else if (std::strcmp(tab_id, "music") == 0) {
        paint_placeholder_tab(sdl, font, geo, ui,
                              "giochi-musicd: biblioteca em /user/music");
    } else if (std::strcmp(tab_id, "home") == 0) {
        paint_placeholder_tab(
            sdl, font, geo, ui,
            "Continuar / Novidades / Recentes (assets/devices/home.json)");
    } else {
        paint_placeholder_tab(sdl, font, geo, ui,
                              "app externo (layout.json \"layouts\")");
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
    void calcular_geometria(SDL_Renderer *sdl, const UiState &ui)
    {
        const int log_w = ui.layout->width;
        const int log_h = ui.layout->height;

        if (!geo_valida || log_w != geo_.screen_w || log_h != geo_.screen_h) {
            ui_geometry_base(&geo_, log_w, log_h);
            measure_tabs(ui);
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

        for (int i = 0; i < ui.layout->tab_count && i < GIOCHI_MAX_TABS; i++) {
            widths[i] = font_.measure(
                tab_label(giochi_layout_tab_id(ui.layout, i)), geo_.font_tab);
        }
        ui_geometry_tabs(&geo_, widths, ui.layout->tab_count, ui.current_tab);
    }

    SDL_Window *window_ = nullptr;
    SDL_Renderer *sdl_ = nullptr;
    SplashImage splash_;
    Font font_;
    UiGeometry geo_;
    bool geo_valida = false;
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
