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

#include "giochi_json.h"
#include "giochi_log.h"
#include "giochi_util.h"

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
 * O splash e exibido por GIOCHI_SPLASH_FRAMES frames (default 45 ~= 0.75 s a
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

int splash_frames_from_env(int fallback)
{
    const char *env = std::getenv("GIOCHI_SPLASH_FRAMES");
    if (env == nullptr || *env == '\0') {
        return fallback;
    }
    int value = std::atoi(env);
    return value >= 0 ? value : fallback;
}

class SdlRenderer final : public Renderer {
public:
    SdlRenderer(SDL_Window *window, SDL_Renderer *sdl)
        : window_(window), sdl_(sdl) {}

    ~SdlRenderer() override
    {
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

        unsigned rgb = 0x0F0F0F;
        if (ui.theme != nullptr) {
            rgb = giochi_theme_rgb(ui.theme->background, rgb);
        }

        (void)SDL_SetRenderDrawColor(sdl_,
                                     static_cast<Uint8>((rgb >> 16) & 0xFFU),
                                     static_cast<Uint8>((rgb >> 8) & 0xFFU),
                                     static_cast<Uint8>(rgb & 0xFFU), 0xFFU);
        (void)SDL_RenderClear(sdl_);

        if (splash_frames_ > 0) {
            splash_.draw(sdl_);
        } else if (ui.layout != nullptr) {
            /* TODO(v1.2): topbar/tab_bar/grid/hint_bar. */
        }

        SDL_RenderPresent(sdl_);

        if (splash_frames_ > 0) {
            splash_frames_--;
            if (splash_frames_ == 0) {
                splash_.release();
            }
        }
        return 0;
    }

    bool should_quit() const override { return quit_; }
    const char *name() const override { return "sdl2"; }

    void load_splash(const char *base_dir)
    {
        if (!splash_.load(sdl_, base_dir)) {
            return;
        }
        splash_frames_ = splash_frames_from_env(45);
        if (splash_frames_ == 0) {
            splash_.release();
        }
    }

    /* Qualquer entrada do usuario encerra o splash imediatamente. */
    void dismiss_splash()
    {
        if (splash_frames_ > 0) {
            splash_frames_ = 0;
            splash_.release();
        }
    }

private:
    SDL_Window *window_ = nullptr;
    SDL_Renderer *sdl_ = nullptr;
    SplashImage splash_;
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

    auto *renderer = new SdlRenderer(window, sdl);

    /* Bootsplash: opcional e silencioso quando ausente (ver BOOTSPLASH.md). */
    if (std::getenv("GIOCHI_NO_SPLASH") == nullptr) {
        const char *base = std::getenv("GIOCHI_BOOTLOGO_DIR");
        renderer->load_splash(base);
    }
    return renderer;
}

Renderer *render_open(const giochi_layout &layout, std::string *err)
{
    Renderer *renderer = open_sdl(layout, err);

    if (renderer == nullptr) {
        GLOG_WARN("render: SDL2 indisponivel; usando backend headless");
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
