/* Despeja a geometria da UI em JSON, para inspecao no PC.
 *
 * Existe para nao existir uma SEGUNDA copia dos numeros de layout num script de
 * preview: a geometria sai do mesmo ui_geometry.cpp que o render.cpp usa, e
 * tools/preview-ui.py so rasteriza o que este programa imprimiu.
 *
 * Uso: dump-geometry [--size 1280x720] [--tab 1] [--items 3] [--focus 0]
 *                    [--labels 48,80,40,30,40] [--theme neon_rose]
 *
 * `--theme` publica tambem a metrica que o tema impoe (raio, borda, densidade,
 * capa, dicas) e o bloco `theme` com as cores. O preview nao le o theme.json
 * por conta propria: se lesse, teria de repetir o mapeamento e voltaria a
 * divergir do aparelho (§3.71).
 */
#include "i18n.hpp"
#include "layout.hpp"
#include "overlay.hpp"
#include "status.hpp"
#include "music.hpp"
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


void campo_json(std::string *out, const char *texto)
{
    /* Aspas e barra invertida sao os unicos caracteres que o JSON nao aceita
     * crus nos nossos rotulos; acento passa direto (UTF-8 e' valido). */
    for (const char *c = texto != nullptr ? texto : ""; *c != '\0'; c++) {
        if (*c == '"' || *c == '\\') {
            out->push_back('\\');
        }
        out->push_back(*c);
    }
}

void emitir_textos(const std::string &assets, const std::string &lang, int tab,
                   int items, int settings, int level, int index,
                   int hints_mode, const char *tema_nome)
{
    giochi::I18n i18n;

    if (!i18n.load(assets.c_str(), lang.c_str())) {
        std::fprintf(stderr, "aviso: catalogo '%s' nao carregou\n",
                     lang.c_str());
    }

    static const char *const ids[] = {"music", "library", "home", "store",
                                      "ports"};

    std::printf("\"lang\":\"%s\",\n\"tabs_text\":[", i18n.lang());
    for (int i = 0; i < 5; i++) {
        std::string key = std::string("tab.") + ids[i];
        std::string txt;
        campo_json(&txt, i18n.tr(key.c_str()));
        std::printf("%s\"%s\"", i > 0 ? "," : "", txt.c_str());
    }
    std::printf("],\n");

    const char *tab_id = (tab >= 0 && tab < 5) ? ids[tab] : "library";
    std::string title, e1, e2, c, hint;
    std::string key = std::string("tab.") + tab_id;

    campo_json(&title, i18n.tr(key.c_str()));
    campo_json(&e1, i18n.tr("library.empty"));
    campo_json(&e2, i18n.tr("library.empty_hint"));
    campo_json(&c, i18n.tr_n("library.count", items).c_str());

    std::printf("\"tab_id\":\"%s\",\n", tab_id);
    std::printf("\"texts\":{\"empty\":\"%s\",\"empty_hint\":\"%s\","
                "\"title\":\"%s\",\"count\":\"%s\",\"home_tip1\":\"%s\","
                "\"home_tip2\":\"%s\",\"music_empty\":\"%s\","
                "\"store_missing\":\"%s\",\"ports_missing\":\"%s\"},\n",
                e1.c_str(), e2.c_str(), title.c_str(), c.c_str(),
                i18n.tr("home.tip.scan"), i18n.tr("home.tip.usb"),
                i18n.tr("music.empty"), i18n.tr("store.missing"),
                i18n.tr("ports.missing"));

    /* A hint bar vem do MESMO lugar que o aparelho usa (ui_hint_text, em
     * i18n.cpp). Tabela duplicada aqui foi o que fez o preview mostrar uma
     * dica diferente da do Launcher. */
    {
        giochi::UiHintContext ctx = giochi::UI_HINT_TABS;

        if (std::strcmp(tab_id, "home") == 0) {
            ctx = giochi::UI_HINT_HOME;
        } else if (std::strcmp(tab_id, "music") == 0) {
            ctx = giochi::UI_HINT_MUSIC;
        } else if (std::strcmp(tab_id, "library") == 0) {
            ctx = giochi::UI_HINT_LIBRARY;
        }
        std::string esc;

        campo_json(&esc, giochi::ui_hint_text(i18n, ctx, hints_mode).c_str());
        std::printf("\"hint\":\"%s\",\n", esc.c_str());
    }
    std::printf("\"status\":{\"battery\":82,\"charging\":0,\"wifi_present\":1,"
                "\"wifi_up\":1,\"wifi_signal\":74,\"ethernet\":0,"
                "\"time\":\"10:42\"},\n");

    std::printf("\"settings\":{\"open\":%d,\"level\":%d,\"index\":%d,"
                "\"title\":\"%s\",\"items\":[",
                settings, level, index,
                i18n.tr(level == 0 ? "settings.title" : "settings.system"));

    if (settings) {
        if (level == 0) {
            /* A lista de chaves mora em overlay.cpp — copia-la aqui foi o que
             * deixou o preview com um menu de 6 linhas quando o aparelho tinha
             * 7. Os indices fecham com giochi_overlay_count(). */
            for (int i = 0; i < GIOCHI_OVERLAY_ITEM_COUNT; i++) {
                std::string lbl;
                campo_json(&lbl, i18n.tr(giochi_overlay_key(i)));
                std::printf("%s{\"label\":\"%s\",\"sub\":%d}", i > 0 ? "," : "",
                            lbl.c_str(), i == GIOCHI_OVERLAY_DEV_OFF_INDEX ? 0 : 1);
            }
        } else {
            auto item = [&i18n](bool first, const char *chave,
                                const char *valor) {
                std::string lbl, val;
                campo_json(&lbl, i18n.tr(chave));
                campo_json(&val, valor);
                std::printf("%s{\"label\":\"%s\",\"value\":\"%s\",\"sub\":0}",
                            first ? "" : ",", lbl.c_str(), val.c_str());
            };
            std::string idioma = std::string(i18n.lang()) + " (A)";

            /* Uma linha por item do submenu, na ordem REAL. Valores de
             * exemplo; o nome do tema vem do catalogo para o preview mostrar
             * o comprimento tipico de verdade. */
            for (int i = 0; i < GIOCHI_SYSTEM_ITEM_COUNT; i++) {
                const char *chave = giochi_overlay_system_key(i);
                std::string valor;

                switch (i) {
                case GIOCHI_SYSTEM_LANGUAGE_INDEX:
                    valor = idioma;
                    break;
                case 1:
                    valor = "America/Sao_Paulo";
                    break;
                case 2:
                    valor = "80%";
                    break;
                case 3:
                    valor = "5 min";
                    break;
                case GIOCHI_SYSTEM_USB_MODE_INDEX:
                    valor = "MTP";
                    break;
                case GIOCHI_SYSTEM_DEV_MODE_INDEX:
                    valor = i18n.tr("settings.off");
                    break;
                default:
                    valor = tema_nome; /* Tema */
                    break;
                }
                item(i == 0, chave, valor.c_str());
            }
        }
    }
    std::printf("]}\n}\n");
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
    std::string lang = "pt-BR";
    std::string assets = "assets";
    std::string theme_id;
    int settings = 0;
    int settings_level = 1;
    int settings_index = 0;

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
        } else if (arg == "--lang" && has_val) {
            lang = argv[++i];
        } else if (arg == "--assets" && has_val) {
            assets = argv[++i];
        } else if (arg == "--theme" && has_val) {
            theme_id = argv[++i];
        } else if (arg == "--settings" && has_val) {
            settings = std::atoi(argv[++i]);
        } else if (arg == "--level" && has_val) {
            settings_level = std::atoi(argv[++i]);
        } else if (arg == "--index" && has_val) {
            settings_index = std::atoi(argv[++i]);
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

    /* Tema: o pedido na linha de comando, senao o padrao do catalogo. O que
     * vale de verdade e' o que o aparelho faria — mesma giochi_theme_resolve. */
    giochi_theme theme;
    char theme_used[GIOCHI_THEME_ID_LEN] = {0};
    char theme_err[128] = {0};

    (void)giochi_theme_resolve(&theme, assets.c_str(),
                               theme_id.empty() ? nullptr : theme_id.c_str(),
                               nullptr, theme_used, sizeof(theme_used),
                               theme_err, sizeof(theme_err));

    giochi::ui_geometry_base(&g, width, height);
    giochi::ui_geometry_apply_theme(&g, giochi::ui_theme_metrics_from(theme));
    giochi::ui_geometry_tabs(&g, label_w, label_count, tab);
    giochi::ui_geometry_grid(&g, items, focus);

    std::printf("{\n");
    std::printf("\"screen\":{\"w\":%d,\"h\":%d},\n", g.screen_w, g.screen_h);
    std::printf("\"theme\":{\"id\":\"%s\",\"name\":\"%s\",\"density\":\"%s\","
                "\"hints\":\"%s\",\"border\":%d,\n"
                "  \"colors\":{\"background\":\"%s\",\"surface\":\"%s\","
                "\"surface_elevated\":\"%s\",\"text_primary\":\"%s\","
                "\"text_secondary\":\"%s\",\"hint_bar_bg\":\"%s\","
                "\"divider\":\"%s\",\"accent\":\"%s\"}},\n",
                theme_used, theme.id, giochi_density_name(g.density),
                giochi_hints_name(g.hints), g.border,
                theme.background, theme.surface, theme.surface_elevated,
                theme.text_primary, theme.text_secondary, theme.hint_bar_bg,
                theme.divider, theme.accent);
    std::printf(
        "\"tokens\":{\"gap\":%d,\"margin\":%d,\"inset\":%d,\"corner\":%d,"
        "\"accent\":%d,\"ring\":%d,\"tile_w\":%d,\"tile_h\":%d,\"title_h\":%d,"
        "\"font_tab\":%d,\"font_title\":%d,\"font_body\":%d,"
        "\"columns\":%d,\"rows\":%d,\"capacity\":%d,\"divider\":%d},\n",
        g.gap, g.margin, g.inset, g.corner, g.accent_h, g.focus_ring, g.tile_w,
        g.tile_h, g.title_h, g.font_tab, g.font_title, g.font_body, g.columns,
        g.rows, g.capacity,
        /* Espessura das linhas de separacao como o render.cpp desenha
         * (scale_pct / 50). O preview desenhava 1 px fixo e ficava mais fino
         * que o aparelho — divergencia de 1 px na tela inteira. */
        g.scale_pct / 50);
    /* Aba Musica: a fila de exemplo e a GEOMETRIA da lista (altura da linha,
     * quantas cabem, primeira visivel) vem de music_list_layout — o mesmo
     * modulo que o render.cpp usa. O preview so' desenha; nao recalcula. */
    {
        giochi::MusicInfo demo;
        const char *titulos[] = {"A Quadrilha", "Asa Branca", "Xote das Meninas",
                                 "Sabiá", "Baião", "Forró no Escuro"};
        const char *artistas[] = {"Dominguinhos", "Luiz Gonzaga", "Dominguinhos",
                                  "Desconhecido", "Luiz Gonzaga", "Desconhecido"};

        for (int i = 0; i < 6; i++) {
            giochi::MusicTrack tr;

            tr.title = titulos[i];
            tr.artist = artistas[i];
            tr.duration_s = 150 + static_cast<unsigned>(i) * 23u;
            demo.tracks.push_back(tr);
        }
        demo.state = giochi::MusicState::Playing;
        demo.current = 2;
        demo.pos_s = 65;
        demo.decoder_ready = true;

        const int disponivel = g.content.h - 2 * g.margin - g.font_tab * 2 -
                               g.gap * 2;
        const giochi::MusicListLayout pl =
            giochi::music_list_layout(12, g.gap, disponivel,
                                      static_cast<int>(demo.tracks.size()), 2);

        std::printf("\"music\":{\"count\":\"%s\",\"time\":\"%s\","
                    "\"row_h\":%d,\"rows\":%d,\"first\":%d,\"current\":%d,"
                    "\"tracks\":[",
                    giochi::music_format_count(demo).c_str(),
                    giochi::music_format_time(demo.pos_s).c_str(),
                    pl.row_h, pl.rows, pl.first, demo.current);
        for (size_t i = 0; i < demo.tracks.size(); i++) {
            std::string tit, art;

            campo_json(&tit, demo.tracks[i].title.c_str());
            campo_json(&art, demo.tracks[i].artist.c_str());
            std::printf("%s{\"title\":\"%s\",\"artist\":\"%s\","
                        "\"dur\":\"%s\"}",
                        i > 0 ? "," : "", tit.c_str(), art.c_str(),
                        giochi::music_format_time(demo.tracks[i].duration_s)
                            .c_str());
        }
        std::printf("]},\n");
    }

    put_rect("topbar", g.topbar);
    put_rect("hint_bar", g.hint_bar);
    put_rect("content", g.content);

    /* Peso por papel, na ordem de giochi::UiTextRole — contrato numerico com
     * tools/preview-ui.py. Publicado aqui para o preview nao repetir a regra
     * (foi o que fez o preview desenhar tudo em regular). */
    std::printf("\"font_weights\":[");
    for (int r = 0; r < giochi::UI_TEXT_COUNT; r++) {
        std::printf("%s\"%s\"", r > 0 ? "," : "",
                    giochi::ui_weight_name(
                        giochi::ui_text_weight(static_cast<giochi::UiTextRole>(r))));
    }
    std::printf("],\n");

    std::printf("\"tab_active\":%d,\"tabs\":[", g.tab_active);
    for (int i = 0; i < g.tab_count; i++) {
        std::printf("%s{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}", i > 0 ? "," : "",
                    g.tab[i].x, g.tab[i].y, g.tab[i].w, g.tab[i].h);
    }
    std::printf("],\n\"first_index\":%d,\"focus_slot\":%d,\"tiles\":[",
                g.first_index, g.focus_slot);
    for (int i = 0; i < g.tile_count; i++) {
        /* O estilo vem de ui_tile_style — o MESMO que o render.cpp usa. Sem
         * isso o preview volta a ser uma segunda verdade (§3.71). */
        const giochi::UiTileStyle st = giochi::ui_tile_style(
            g, i == g.focus_slot, g.first_index + i < items);

        std::printf("%s{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,"
                    "\"fill\":%d,\"inset\":%d,\"outline\":%d,\"title_role\":%d,"
                    "\"ring\":%d,\"pad\":%d,\"title_dy\":%d}",
                    i > 0 ? "," : "", g.tile[i].x, g.tile[i].y, g.tile[i].w,
                    g.tile[i].h, static_cast<int>(st.fill),
                    static_cast<int>(st.inset), static_cast<int>(st.outline),
                    static_cast<int>(st.title), st.ring, st.pad, st.title_dy);
    }
    const giochi::UiRect ring = giochi::ui_geometry_focus_ring(g, g.focus_slot);

    std::printf("],\n\"focus_ring\":{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d},\n",
                ring.x, ring.y, ring.w, ring.h);
    emitir_textos(assets, lang, tab, items, settings, settings_level,
                  settings_index, theme.hints,
                  theme_used[0] != '\0' ? theme_used : "tema");
    return 0;
}
