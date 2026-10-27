/* Giochi Konsol OS v1.2 — abstracao de renderizacao do Launcher.
 *
 * Backends: render.cpp (SDL2/Gamescope-DRM) e render_null.cpp (headless,
 * usado em testes de host e como fallback de diagnostico).
 */
#ifndef GIOCHI_RENDER_HPP
#define GIOCHI_RENDER_HPP

#include <string>

#include "layout.hpp"
#include "theme.hpp"

namespace giochi {

struct UiState {
    const giochi_layout *layout = nullptr;
    const giochi_theme *theme = nullptr;
    int current_tab = 0;
    int focus_index = 0;
    bool settings_open = false;
    int settings_index = 0;
    bool music_playing = false;
    std::string hint;
};

class Renderer {
public:
    virtual ~Renderer() = default;

    virtual int frame(const UiState &ui) = 0;
    virtual bool should_quit() const = 0;
    virtual const char *name() const = 0;
};

/* Backend preferido (SDL2) com fallback automatico para headless.
 * Retorna nullptr apenas se a alocacao falhar; err recebe o diagnostico. */
Renderer *render_open(const giochi_layout &layout, std::string *err);

/* Backend headless (sempre disponivel). */
Renderer *render_open_null(const giochi_layout &layout);

}  // namespace giochi

#endif /* GIOCHI_RENDER_HPP */
