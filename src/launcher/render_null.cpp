/* Backend headless: valida o motor de layout sem GPU/DRM.
 * Usado quando SDL2 nao esta disponivel e nos testes de host.
 */
#include "render.hpp"

#include "giochi_log.h"

namespace giochi {

namespace {

class NullRenderer final : public Renderer {
public:
    explicit NullRenderer(const giochi_layout &layout)
        : width_(layout.width), height_(layout.height)
    {
        GLOG_INFO("render: backend headless %dx%d", width_, height_);
    }

    int frame(const UiState &ui) override
    {
        frames_++;
        if (frames_ == 1 && ui.layout != nullptr) {
            GLOG_INFO("render: primeiro frame, aba '%s'%s",
                      giochi_layout_tab_id(ui.layout, ui.current_tab),
                      ui.settings_open ? " (configuracoes abertas)" : "");
        }
        return 0;
    }

    bool should_quit() const override { return false; }
    const char *name() const override { return "null"; }

private:
    int width_ = 640;
    int height_ = 480;
    int frames_ = 0;
};

}  // namespace

Renderer *render_open_null(const giochi_layout &layout)
{
    return new NullRenderer(layout);
}

}  // namespace giochi
