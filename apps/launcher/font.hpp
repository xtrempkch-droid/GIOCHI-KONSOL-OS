/* Giochi Konsol OS v1.2 — fonte embutida (atlas gerado por tools/make-font.py).
 *
 * O console NAO tem freetype nem SDL2_ttf: o atlas e gerado no PC, versionado
 * como os demais assets e lido como bytes. Ver AGENTS.md §3.67.
 *
 * Duas responsabilidades:
 *   - metricas (largura de um texto) — usada para posicionar as abas, que sao
 *     medidas e nao uma celula fixa (docs/LAYOUT-PROPOSTA.md §4.1);
 *   - desenho, com recorte por `...` medido em PIXELS (nao em caracteres, que
 *     era o defeito que a proposta §5.2 aponta no script antigo).
 */
#ifndef GIOCHI_FONT_HPP
#define GIOCHI_FONT_HPP

#include <string>
#include <vector>

#include "ui_geometry.hpp"

#ifdef GIOCHI_HAVE_SDL2
struct SDL_Renderer;
struct SDL_Texture;
#endif

namespace giochi {

/* Peso da face. O atlas traz duas (assets/font.json -> "weights"); quem
 * escolhe e' ui_text_weight(), para o render e o preview nao divergirem. */
enum class FontWeight { Regular = 0, Bold = 1 };

class Font {
public:
    Font() = default;
    ~Font();
    Font(const Font &) = delete;
    Font &operator=(const Font &) = delete;

#ifdef GIOCHI_HAVE_SDL2
    /* Carrega <dir>/font.json + <dir>/font.a8. Devolve false (e o Launcher
     * desenha so as caixas) se faltar qualquer um dos dois. */
    bool load(SDL_Renderer *renderer, const char *dir);

    /* Texto na cor dada, baseline em `baseline_y`.
     * `max_w` > 0 recorta com "..." medido em pixels.
     * Devolve a largura desenhada. */
    int draw(SDL_Renderer *renderer, const std::string &text, int x,
             int baseline_y, int px, unsigned rgb, int max_w,
             FontWeight weight = FontWeight::Regular) const;
#endif

    /* Largura em pixels, sem recorte. Disponivel mesmo sem SDL (o
     * posicionamento do layout depende dela). */
    int measure(const std::string &text, int px,
                FontWeight weight = FontWeight::Regular) const;

    /* Versao recortada: devolve `text` ou um prefixo + "...". */
    std::string truncate(const std::string &text, int px, int max_w,
                         FontWeight weight = FontWeight::Regular) const;

    int line_height(int px, FontWeight weight = FontWeight::Regular) const;
    int ascent(int px, FontWeight weight = FontWeight::Regular) const;
    bool loaded() const { return !sizes_.empty(); }

private:
    struct Cell {
        int cp = 0;
        int x = 0;
        int w = 0;       /* largura do bloco no atlas (>= advance) */
        int advance = 0; /* passo do cursor */
    };
    struct Face {
        int px = 0;
        FontWeight weight = FontWeight::Regular;
        int line_height = 0;
        int ascent = 0;
        int y = 0;
        std::vector<Cell> cells;
    };

    /* Melhor face para (px, peso): o px mais proximo DENTRO do peso pedido.
     * Cair para o outro peso seria pior que cair para outro tamanho — um
     * titulo em regular passa desapercebido, um titulo em 12 px nao. */
    const Face *face_for(int px, FontWeight weight) const;
    int advance_of(const Face &face, unsigned cp) const;

#ifdef GIOCHI_HAVE_SDL2
    SDL_Texture *texture_ = nullptr;
    int tex_w_ = 0;
    int tex_h_ = 0;
#endif
    std::vector<Face> sizes_;
};

}  // namespace giochi

#endif /* GIOCHI_FONT_HPP */
