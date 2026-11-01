#include "font.hpp"

#include "giochi_json.h"
#include "giochi_log.h"
#include "giochi_util.h"

#include <cstdlib>
#include <cstring>
#include <string>

#ifdef GIOCHI_HAVE_SDL2
#include <SDL.h>
#endif

namespace giochi {

namespace {

/* Decodifica UTF-8 em code points. Byte invalido vira o code point de
 * substituicao, e nao lixo: um titulo de jogo em japones aparece como caixas,
 * que e honesto, em vez de sumir do titulo. */
unsigned next_cp(const std::string &s, std::size_t &i)
{
    const unsigned char c = static_cast<unsigned char>(s[i]);

    if (c < 0x80) {
        i++;
        return c;
    }

    int extra = 0;
    unsigned cp = 0;

    if ((c & 0xE0) == 0xC0) {
        extra = 1;
        cp = c & 0x1FU;
    } else if ((c & 0xF0) == 0xE0) {
        extra = 2;
        cp = c & 0x0FU;
    } else if ((c & 0xF8) == 0xF0) {
        extra = 3;
        cp = c & 0x07U;
    } else {
        i++;
        return 0xFFFDU;
    }

    if (i + static_cast<std::size_t>(extra) >= s.size()) {
        i = s.size();
        return 0xFFFDU;
    }
    for (int k = 1; k <= extra; k++) {
        const unsigned char cc = static_cast<unsigned char>(s[i + k]);

        if ((cc & 0xC0) != 0x80) {
            i += 1;
            return 0xFFFDU;
        }
        cp = (cp << 6) | (cc & 0x3FU);
    }
    i += static_cast<std::size_t>(extra) + 1;
    return cp;
}

}  // namespace

Font::~Font()
{
#ifdef GIOCHI_HAVE_SDL2
    if (texture_ != nullptr) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
#endif
}

const Font::Face *Font::face_for(int px) const
{
    const Face *best = nullptr;
    int best_delta = 0;

    for (const auto &face : sizes_) {
        const int delta = face.px > px ? face.px - px : px - face.px;

        if (best == nullptr || delta < best_delta) {
            best = &face;
            best_delta = delta;
        }
    }
    return best;
}

int Font::advance_of(const Face &face, unsigned cp) const
{
    for (const auto &cell : face.cells) {
        if (static_cast<unsigned>(cell.cp) == cp) {
            return cell.advance;
        }
    }
    for (const auto &cell : face.cells) {
        if (cell.cp == 0xFFFD) {
            return cell.advance;
        }
    }
    return face.px / 2;
}

int Font::measure(const std::string &text, int px) const
{
    const Face *face = face_for(px);

    if (face == nullptr) {
        /* Sem atlas: estimativa grosseira, so para o layout nao colapsar. */
        return static_cast<int>(text.size()) * (px / 2 > 0 ? px / 2 : 1);
    }

    int width = 0;
    std::size_t i = 0;

    while (i < text.size()) {
        width += advance_of(*face, next_cp(text, i));
    }
    return width;
}

std::string Font::truncate(const std::string &text, int px, int max_w) const
{
    if (max_w <= 0 || measure(text, px) <= max_w) {
        return text;
    }
    const int ellipsis = measure("...", px);

    if (ellipsis >= max_w) {
        return std::string();
    }

    /* Corta por PIXELS e por code point inteiro: cortar em byte parte um
     * caractere UTF-8 no meio (§5.2 aponta o `tip[:72]` antigo). */
    std::string out;
    std::size_t i = 0;
    int width = 0;

    while (i < text.size()) {
        const std::size_t start = i;
        const unsigned cp = next_cp(text, i);
        const Face *face = face_for(px);
        const int adv = face != nullptr ? advance_of(*face, cp) : px / 2;

        if (width + adv + ellipsis > max_w) {
            break;
        }
        out.append(text, start, i - start);
        width += adv;
    }
    return out + "...";
}

int Font::line_height(int px) const
{
    const Face *face = face_for(px);

    return face != nullptr ? face->line_height : px + px / 2;
}

int Font::ascent(int px) const
{
    const Face *face = face_for(px);

    return face != nullptr ? face->ascent : px;
}

#ifdef GIOCHI_HAVE_SDL2

bool Font::load(SDL_Renderer *renderer, const char *dir)
{
    if (renderer == nullptr) {
        return false;
    }

    const std::string prefix = dir != nullptr ? std::string(dir)
                                             : std::string("/usr/share/giochi");
    const std::string json_path = prefix + "/font.json";
    const std::string data_path = prefix + "/font.a8";

    char err[128] = {0};
    gj_value *meta = gj_parse_file(json_path.c_str(), err, sizeof(err));

    if (meta == nullptr) {
        GLOG_WARN("fonte: '%s' ilegivel (%s)", json_path.c_str(), err);
        return false;
    }

    const int width = gj_as_int(gj_object_get(meta, "width"), 0);
    const int height = gj_as_int(gj_object_get(meta, "height"), 0);
    const gj_value *sizes = gj_object_get(meta, "sizes");

    sizes_.clear();

    if (width <= 0 || height <= 0 || sizes == nullptr) {
        GLOG_WARN("fonte: '%s' sem largura/altura/tamanhos", json_path.c_str());
        gj_free(meta);
        return false;
    }

    for (std::size_t s = 0; s < gj_array_len(sizes); s++) {
        const gj_value *node = gj_array_at(sizes, s);

        if (node == nullptr) {
            continue;
        }
        Face face;
        face.px = gj_as_int(gj_object_get(node, "px"), 0);
        face.line_height = gj_as_int(gj_object_get(node, "line_height"), face.px);
        face.ascent = gj_as_int(gj_object_get(node, "ascent"), face.px);
        face.y = gj_as_int(gj_object_get(node, "y"), 0);

        const gj_value *cells = gj_object_get(node, "cells");

        for (std::size_t c = 0; c < gj_array_len(cells); c++) {
            const gj_value *cell = gj_array_at(cells, c);

            if (cell == nullptr) {
                continue;
            }
            Cell entry;
            entry.cp = gj_as_int(gj_object_get(cell, "cp"), 0);
            entry.x = gj_as_int(gj_object_get(cell, "x"), 0);
            entry.advance = gj_as_int(gj_object_get(cell, "advance"), 0);
            if (entry.advance <= 0) {
                entry.advance = face.px / 2;
            }
            face.cells.push_back(entry);
        }
        if (face.px > 0 && !face.cells.empty()) {
            sizes_.push_back(std::move(face));
        }
    }
    gj_free(meta);

    if (sizes_.empty()) {
        GLOG_WARN("fonte: '%s' sem nenhum tamanho utilizavel", json_path.c_str());
        return false;
    }

    std::size_t len = 0;
    char *coverage = giochi_read_file(data_path.c_str(), &len);
    const std::size_t expected = static_cast<std::size_t>(width) *
                                static_cast<std::size_t>(height);

    if (coverage == nullptr) {
        GLOG_WARN("fonte: '%s' ausente", data_path.c_str());
        sizes_.clear();
        return false;
    }
    if (len < expected) {
        GLOG_WARN("fonte: '%s' tem %zu B, esperado %zu B", data_path.c_str(),
                  len, expected);
        free(coverage);
        sizes_.clear();
        return false;
    }

    /* 8 bits de cobertura -> ARGB8888 branco. Uma unica textura para todos os
     * tamanhos e todas as cores (o tingimento e do ColorMod). */
    texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STATIC, width, height);
    if (texture_ == nullptr) {
        GLOG_WARN("fonte: SDL_CreateTexture: %s", SDL_GetError());
        free(coverage);
        sizes_.clear();
        return false;
    }

    std::string rgba(expected * 4U, '\0');

    for (std::size_t i = 0; i < expected; i++) {
        const unsigned char a = static_cast<unsigned char>(coverage[i]);
        const std::size_t o = i * 4U;
        rgba[o + 0] = static_cast<char>(a);    /* B (premultipliceis? nao) */
        rgba[o + 1] = static_cast<char>(a);    /* G */
        rgba[o + 2] = static_cast<char>(a);    /* R */
        rgba[o + 3] = static_cast<char>(a);    /* A */
    }
    free(coverage);

    if (SDL_UpdateTexture(texture_, nullptr, rgba.data(), width * 4) != 0) {
        GLOG_WARN("fonte: SDL_UpdateTexture: %s", SDL_GetError());
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
        sizes_.clear();
        return false;
    }
    (void)SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
    tex_w_ = width;
    tex_h_ = height;
    return true;
}

int Font::draw(SDL_Renderer *renderer, const std::string &text, int x,
               int baseline_y, int px, unsigned rgb, int max_w) const
{
    const Face *face = face_for(px);

    if (renderer == nullptr || face == nullptr || texture_ == nullptr) {
        return 0;
    }

    const std::string shown = truncate(text, px, max_w);
    const SDL_Color tint = {static_cast<Uint8>((rgb >> 16) & 0xFFU),
                            static_cast<Uint8>((rgb >> 8) & 0xFFU),
                            static_cast<Uint8>(rgb & 0xFFU), 0xFFU};

    (void)SDL_SetTextureColorMod(texture_, tint.r, tint.g, tint.b);

    int pen = x;
    std::size_t i = 0;

    while (i < shown.size()) {
        const unsigned cp = next_cp(shown, i);
        const Cell *cell = nullptr;

        for (const auto &candidate : face->cells) {
            if (static_cast<unsigned>(candidate.cp) == cp) {
                cell = &candidate;
                break;
            }
        }
        if (cell == nullptr) {
            for (const auto &candidate : face->cells) {
                if (candidate.cp == 0xFFFD) {
                    cell = &candidate;
                    break;
                }
            }
        }
        if (cell == nullptr) {
            continue;
        }

        SDL_Rect src{cell->x, face->y, cell->advance, face->line_height};
        SDL_Rect dst{pen, baseline_y - face->ascent, cell->advance,
                     face->line_height};

        if (src.x + src.w > tex_w_) {
            src.w = tex_w_ - src.x;
        }
        if (src.y + src.h > tex_h_) {
            src.h = tex_h_ - src.y;
        }
        dst.w = src.w;
        dst.h = src.h;

        if (src.w > 0 && src.h > 0) {
            (void)SDL_RenderCopy(renderer, texture_, &src, &dst);
        }
        pen += cell->advance;
    }
    return pen - x;
}

#endif /* GIOCHI_HAVE_SDL2 */

}  // namespace giochi
