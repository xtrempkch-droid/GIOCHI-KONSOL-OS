/* Despeja a geometria da UI em JSON, para inspecao no PC.
 *
 * Existe para nao existir uma SEGUNDA copia dos numeros de layout num script de
 * preview: a geometria sai do mesmo ui_geometry.cpp que o render.cpp usa, e
 * tools/preview-ui.py so rasteriza o que este programa imprimiu.
 *
 * Uso: dump-geometry [--size 1280x720] [--tab 1] [--items 3] [--focus 0]
 *                    [--labels 48,80,40,30,40]
 */
#include "layout.hpp"
#include "ui_geometry.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

void put_rect(const char *name, const giochi::UiRect &r, bool comma = true)
{
    std::printf("\"%s\":{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}%s", name, r.x,
                r.y, r.w, r.h, comma ? "," : "");
}

}  // namespace

int main(int argc, char **argv)
{
    int width = 1280;
    int height = 720;
    int tab = 1;
    int items = 3;
    int focus = 0;
    std::string labels = "60,96,40,30,44";

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        const bool has_val = i + 1 < argc;

        if (arg == "--size" && has_val) {
            if (std::sscanf(argv[++i], "%dx%d", &width, &height) != 2) {
                std::fprintf(stderr, "erro: --size espera LARGURAxALTURA\n");
                return 2;
            }
        } else if (arg == "--tab" && has_val) {
            tab = std::atoi(argv[++i]);
        } else if (arg == "--items" && has_val) {
            items = std::atoi(argv[++i]);
        } else if (arg == "--focus" && has_val) {
            focus = std::atoi(argv[++i]);
        } else if (arg == "--labels" && has_val) {
            labels = argv[++i];
        } else {
            std::fprintf(stderr,
                         "uso: dump-geometry [--size 1280x720] [--tab N] "
                         "[--items N] [--focus N] [--labels a,b,c]\n");
            return 2;
        }
    }

    int label_w[GIOCHI_MAX_TABS] = {0};
    int label_count = 0;
    {
        std::size_t pos = 0;

        while (pos <= labels.size() && label_count < GIOCHI_MAX_TABS) {
            const std::size_t comma = labels.find(',', pos);
            const std::string piece = labels.substr(
                pos, comma == std::string::npos ? std::string::npos : comma - pos);

            if (!piece.empty()) {
                label_w[label_count++] = std::atoi(piece.c_str());
            }
            if (comma == std::string::npos) {
                break;
            }
            pos = comma + 1;
        }
    }

    giochi::UiGeometry g;

    giochi::ui_geometry_base(&g, width, height);
    giochi::ui_geometry_tabs(&g, label_w, label_count, tab);
    giochi::ui_geometry_grid(&g, items, focus);

    std::printf("{\n");
    std::printf("\"screen\":{\"w\":%d,\"h\":%d},\n", g.screen_w, g.screen_h);
    std::printf(
        "\"tokens\":{\"gap\":%d,\"margin\":%d,\"inset\":%d,\"corner\":%d,"
        "\"accent\":%d,\"ring\":%d,\"tile_w\":%d,\"tile_h\":%d,\"title_h\":%d,"
        "\"font_tab\":%d,\"font_title\":%d,\"font_body\":%d,"
        "\"columns\":%d,\"rows\":%d,\"capacity\":%d},\n",
        g.gap, g.margin, g.inset, g.corner, g.accent_h, g.focus_ring, g.tile_w,
        g.tile_h, g.title_h, g.font_tab, g.font_title, g.font_body, g.columns,
        g.rows, g.capacity);
    put_rect("topbar", g.topbar);
    put_rect("hint_bar", g.hint_bar);
    put_rect("content", g.content);

    std::printf("\"tab_active\":%d,\"tabs\":[", g.tab_active);
    for (int i = 0; i < g.tab_count; i++) {
        std::printf("%s{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}", i > 0 ? "," : "",
                    g.tab[i].x, g.tab[i].y, g.tab[i].w, g.tab[i].h);
    }
    std::printf("],\n\"first_index\":%d,\"focus_slot\":%d,\"tiles\":[",
                g.first_index, g.focus_slot);
    for (int i = 0; i < g.tile_count; i++) {
        std::printf("%s{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}", i > 0 ? "," : "",
                    g.tile[i].x, g.tile[i].y, g.tile[i].w, g.tile[i].h);
    }
    const giochi::UiRect ring = giochi::ui_geometry_focus_ring(g, g.focus_slot);

    std::printf("],\n\"focus_ring\":{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}\n}\n",
                ring.x, ring.y, ring.w, ring.h);
    return 0;
}
