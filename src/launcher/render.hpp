/* Giochi Konsol OS v1.2 — abstracao de renderizacao do Launcher.
 *
 * Backends: render.cpp (SDL2/Gamescope-DRM) e render_null.cpp (headless,
 * usado em testes de host e como fallback de diagnostico).
 */
#ifndef GIOCHI_RENDER_HPP
#define GIOCHI_RENDER_HPP

#include <string>

#include "i18n.hpp"
#include "layout.hpp"
#include "settings.hpp"
#include "status.hpp"
#include "theme.hpp"

namespace giochi {

/* Item desenhavel da grade. O renderer NAO conhece LibraryEntry: copiar o
 * struct inteiro obrigaria render.hpp a incluir library.hpp, e o
 * render_null.cpp (usado nos testes de host) passaria a depender de metadata. */
struct UiItem {
    std::string title;
    bool valid = false;
};

/* Item do menu de Configuracoes, ja' traduzido e com o valor a direita.
 * O renderizador nao conhece `giochi_overlay` nem os indices: quem monta a
 * lista e' o main, que sabe qual item faz o que. Foi assim que a comparacao
 * `strcmp(item, "Desligar")` deixou de ser um risco ao traduzir. */
struct UiMenuItem {
    std::string label;
    std::string value;
    bool has_submenu = false;
};

struct UiState {
    const giochi_layout *layout = nullptr;
    const giochi_theme *theme = nullptr;
    int current_tab = 0;
    int focus_index = 0;
    bool settings_open = false;
    int settings_index = 0;
    bool music_playing = false;
    std::string hint;

    /* Grade da Biblioteca (vazio nas outras abas). */
    const UiItem *items = nullptr;
    int item_count = 0;

    /* Textos e estado do sistema. `i18n` nunca e' nulo: e' do Launcher. */
    const I18n *i18n = nullptr;
    const UiStatus *status = nullptr;
    const giochi_settings *settings = nullptr;

    /* Menu de Configuracoes (vazio quando fechado). */
    const UiMenuItem *menu = nullptr;
    int menu_count = 0;
    std::string menu_title;
    int menu_level = 0;
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
