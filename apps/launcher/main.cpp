/* Giochi Konsol OS v1.2 — giochi-launcher (ADR-09: C++17 + SDL2).
 *
 * Esqueleto funcional: carrega layout.json, o catalogo assets/themes, settings.json, mantem o
 * estado das 5 abas, overlay de Configuracoes, cliente do music.sock e
 * transicoes via /run/giochi/transition. A renderizacao dos widgets
 * (grid 4x3, topbar, hint bar) esta marcada com TODO no backend SDL2.
 */
#include "evdev.hpp"
#include "input.hpp"
#include "ipc_client.hpp"
#include "layout.hpp"
#include "library.hpp"
#include "metadata.hpp"
#include "i18n.hpp"
#include "overlay.hpp"
#include "status.hpp"
#include "music.hpp"
#include "render.hpp"
#include "settings.hpp"
#include "theme.hpp"

#include "giochi_deviceconf.h"
#include "giochi_json.h"
#include "giochi_log.h"
#include "giochi_transition.h"
#include "giochi_util.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

constexpr const char *kVersion = "1.2";

struct Options {
    std::string asset_dir;
    std::string user_dir;
    std::string run_dir;
    std::string buttons_map = "default";
    std::string theme_id; /* vazio = settings.json -> padrao do aparelho */
    bool headless = false;
    int frames = 0; /* >0 encerra apos N frames (usado em smoke test) */
};

struct Launcher {
    Options options;
    giochi_layout layout{};
    giochi_theme theme{};
    giochi::UiThemeMetrics theme_metrics{};
    giochi_theme_catalog cat{};
    giochi_settings settings{};
    giochi_overlay overlay{};
    giochi::InputState input{};
    giochi::UiState ui{};
    giochi::I18n i18n{};
    giochi::UiStatus status{};
    std::vector<giochi::UiMenuItem> menu;
    std::string menu_title;
    int menu_level = 0;
    giochi::Renderer *renderer = nullptr;
    giochi_evdev *evdev = nullptr;
    bool running = true;

    int current_tab = 0;
    int focus_index = 0;
    std::vector<giochi::LibraryEntry> library;
    std::vector<giochi::UiItem> ui_items;
    int library_focus = 0;
    bool library_loaded = false;

    std::string control_sock;
    std::string music_sock;
    /* Estado da aba Musica (fila, faixa atual, posicao). Preenchido
     * por music_refresh a partir do STATUS do giochi-musicd. */
    giochi::MusicInfo music;
    std::string transition_fifo;
    int transition_fd = -1;
};

void print_usage(FILE *out)
{
    std::fprintf(out,
        "uso: giochi-launcher [opcoes]\n"
        "  --asset-dir DIR   layouts e tema (default: /usr/share/giochi)\n"
        "  --user-dir DIR    dados do usuario (default: /user)\n"
        "  --run-dir DIR     runtime (default: /run/giochi)\n"
        "  --buttons-map ID  secao de buttons.map (default: default)\n"
        "  --theme ID        forca o tema (data/themes/<ID>.json);\n"
        "                    sem isto vale settings.json e depois o padrao\n"
        "                    do aparelho (device-theme.json)\n"
        "  --headless        sem SDL2/evdev (diagnostico)\n"
        "  --frames N        encerra apos N frames (smoke test)\n"
        "  --version         imprime a versao\n"
        "  --help            esta ajuda\n");
}

std::string rooted(const char *suffix)
{
    char buf[512];

    if (giochi_path(buf, sizeof(buf), suffix) != 0) {
        return std::string(suffix);
    }
    return std::string(buf);
}

bool parse_args(int argc, char **argv, Options *opt)
{
    opt->asset_dir = rooted("/usr/share/giochi");
    opt->user_dir = rooted("/user");
    opt->run_dir = rooted("/run/giochi");

    /* Launcher como app (docs/LAUNCHER-APP.md): se existir
     * /apps/giochi-launcher/assets/layout.json, os assets de la' SOBREPOEM
     * os da imagem — quem desenvolve o Launcher coloca o binario novo E os
     * assets novos na particao de dados (via /apps), sem regravar o sistema.
     * O --asset-dir explicito continua vencendo (e' processado no laco
     * abaixo, depois desta checagem). */
    {
        std::string app_assets = rooted("/apps/giochi-launcher/assets");
        std::string probe = app_assets + "/layout.json";

        if (access(probe.c_str(), R_OK) == 0) {
            opt->asset_dir = app_assets;
        }
    }

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(stdout);
            std::exit(0);
        } else if (arg == "--version") {
            std::printf("giochi-launcher %s\n", kVersion);
            std::exit(0);
        } else if (arg == "--headless") {
            opt->headless = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            opt->frames = std::atoi(argv[++i]);
        } else if (arg == "--asset-dir" && i + 1 < argc) {
            opt->asset_dir = argv[++i];
        } else if (arg == "--user-dir" && i + 1 < argc) {
            opt->user_dir = argv[++i];
        } else if (arg == "--run-dir" && i + 1 < argc) {
            opt->run_dir = argv[++i];
        } else if (arg == "--buttons-map" && i + 1 < argc) {
            opt->buttons_map = argv[++i];
        } else if (arg == "--theme" && i + 1 < argc) {
            opt->theme_id = argv[++i];
        } else {
            std::fprintf(stderr, "giochi-launcher: opcao desconhecida '%s'\n",
                         arg.c_str());
            print_usage(stderr);
            return false;
        }
    }
    return true;
}

void send_transition(Launcher &app, giochi_transition_cmd cmd,
                     const std::string &path, const std::string &metadata)
{
    giochi_transition_msg msg;

    if (app.transition_fd < 0) {
        GLOG_WARN("transition: FIFO indisponivel (%s)",
                  app.transition_fifo.c_str());
        return;
    }
    giochi_transition_fill(&msg, cmd, path.c_str(), metadata.c_str());
    if (giochi_transition_send(app.transition_fd, &msg) != 0) {
        GLOG_WARN("transition: envio falhou");
        return;
    }
    GLOG_INFO("transition: cmd=%u path='%s'", msg.cmd, msg.path);
}

/* Resposta do musicd: o `main.c` do daemon aloca 8192 para o STATUS, que
 * carrega a fila INTEIRA. Com 512 a fila era truncada no meio do JSON e o
 * parsing falharia — parecendo "sem musica". */
static constexpr size_t kMusicReplyMax = 8192;

void music_command(Launcher &app, const char *command)
{
    char reply[kMusicReplyMax];

    if (app.music_sock.empty()) {
        return;
    }
    if (giochi_ipc_request(app.music_sock.c_str(), command, reply,
                           sizeof(reply), 500) != 0) {
        GLOG_WARN("musica: '%s' sem resposta (socket %s)", command,
                  app.music_sock.c_str());
        return;
    }
    GLOG_DEBUG("musica: %s -> %s", command, reply);
}

/* Pede o STATUS e guarda a fila. Chamado no startup (depois do RESCAN) e ao
 * entrar na aba Musica — a lista tem de refletir o que esta' tocando AGORA. */
void music_refresh(Launcher &app)
{
    char reply[kMusicReplyMax];

    if (app.music_sock.empty()) {
        app.music.detail = "sem music.sock";
        return;
    }
    if (giochi_ipc_request(app.music_sock.c_str(), "STATUS", reply,
                           sizeof(reply), 500) != 0) {
        app.music = giochi::MusicInfo{};
        app.music.detail = "sem resposta do musicd";
        GLOG_WARN("musica: STATUS sem resposta (%s)", app.music_sock.c_str());
        return;
    }
    app.music = giochi::music_parse_status(reply);
    if (!app.music.detail.empty()) {
        GLOG_WARN("musica: STATUS ilegivel: %s", app.music.detail.c_str());
    } else {
        GLOG_DEBUG("musica: %zu faixa(s), atual %d, estado %d",
                   app.music.tracks.size(), app.music.current,
                   static_cast<int>(app.music.state));
    }
}

/* Ao ENTRAR na aba Musica, rele' a fila: a faixa atual muda quando o usuario
 * aperta NEXT/PREV e a lista tem de acompanhar. Sem isto a aba mostrava o
 * estado do startup para sempre. */
void tab_entrou(Launcher &app)
{
    const char *tab = giochi_layout_tab_id(&app.layout, app.current_tab);

    if (tab != nullptr && std::strcmp(tab, "music") == 0) {
        music_refresh(app);
    }
}

void control_command(Launcher &app, const char *command)
{
    char reply[512];

    if (app.control_sock.empty()) {
        return;
    }
    if (giochi_ipc_request(app.control_sock.c_str(), command, reply,
                           sizeof(reply), 500) != 0) {
        GLOG_WARN("controle: '%s' sem resposta", command);
        return;
    }
    GLOG_INFO("controle: %s -> %s", command, reply);
}

/* Secao 4.9.4: modo USB via control.sock + persistencia em settings.json. */
/* Adendo 4.6.1: Modo Desenvolvedor (SSH via USB), desativado por padrao. */
void set_developer_mode(Launcher &app, bool enabled)
{
    app.settings.developer_ssh_usb_enabled = enabled ? 1 : 0;

    char err[128] = {0};
    if (giochi_settings_save(&app.settings, app.options.user_dir.c_str(), err,
                             sizeof(err)) != 0) {
        GLOG_WARN("settings.json: nao foi possivel gravar developer (%s)", err);
    }
    control_command(app, enabled ? "DEV_MODE ON" : "DEV_MODE OFF");
    GLOG_INFO("Modo Desenvolvedor: %s", enabled ? "ativado" : "desativado");
}

void set_usb_mode(Launcher &app, const char *mode)
{
    std::string upper = std::strcmp(mode, "mass") == 0 ? "MASS" : "MTP";

    giochi_strlcpy(app.settings.usb_mode, mode, sizeof(app.settings.usb_mode));

    char err[128] = {0};
    if (giochi_settings_save(&app.settings, app.options.user_dir.c_str(), err,
                             sizeof(err)) != 0) {
        GLOG_WARN("settings.json: nao foi possivel gravar usb_mode (%s)", err);
    }
    control_command(app, (std::string("USB_MODE ") + upper).c_str());
}


/* A hint bar vem de ui_hint_text (i18n.cpp): a tabela de pares mora num
 * lugar so', compartilhada com o tools/dump-geometry. Antes cada um tinha a
 * sua, e o preview mostrava uma dica que o aparelho nao mostrava.
 *
 * Item que nao funciona nao aparece: B e' inerte na Home (§4.1.2), entao a
 * aba Home nao anuncia B. */
void set_hint(Launcher &app)
{
    giochi::UiHintContext ctx = giochi::UI_HINT_TABS;

    if (app.overlay.open) {
        ctx = app.overlay.level == 0 ? giochi::UI_HINT_SETTINGS_0
                                     : giochi::UI_HINT_SETTINGS_1;
    } else {
        const char *tab = giochi_layout_tab_id(&app.layout, app.current_tab);

        if (std::strcmp(tab, "home") == 0) {
            ctx = giochi::UI_HINT_HOME;
        } else if (std::strcmp(tab, "music") == 0) {
            ctx = giochi::UI_HINT_MUSIC;
        } else if (std::strcmp(tab, "library") == 0) {
            ctx = giochi::UI_HINT_LIBRARY;
        }
    }
    app.ui.hint = giochi::ui_hint_text(app.i18n, ctx, app.theme.hints);
}

/* Idiomas que a imagem entrega: assets/locales/<id>.json. A lista e' curta de
 * proposito — idioma que nao tem catalogo viraria chaves cruas na tela. */
static const char *const kIdiomas[] = {"pt_BR", "en"};
static constexpr int kIdiomaCount =
    static_cast<int>(sizeof(kIdiomas) / sizeof(kIdiomas[0]));

/* Troca de idioma a quente, sem reiniciar o Launcher: recarrega o catalogo,
 * grava em settings.json (o init rele esse arquivo) e reconstroi o menu, que
 * tem os rotulos congelados em app.menu. */
void cycle_language(Launcher &app)
{
    int atual = 0;

    for (int i = 0; i < kIdiomaCount; i++) {
        if (std::strcmp(kIdiomas[i], app.i18n.lang()) == 0) {
            atual = i;
            break;
        }
    }
    const char *proximo = kIdiomas[(atual + 1) % kIdiomaCount];

    if (!app.i18n.load(app.options.asset_dir.c_str(), proximo)) {
        GLOG_WARN("idioma: '%s' nao carregou; mantendo '%s'", proximo,
                  app.i18n.lang());
        return;
    }
    giochi_strlcpy(app.settings.language, app.i18n.lang(),
                   sizeof(app.settings.language));

    char err[128] = {0};

    if (giochi_settings_save(&app.settings, app.options.user_dir.c_str(), err,
                             sizeof(err)) != 0) {
        GLOG_WARN("settings.json: nao foi possivel gravar language (%s)", err);
    }
    GLOG_INFO("idioma: %s", app.i18n.lang());
}

/* Primeira linha de /user/.cache/news.json (o app de loja do PC escreve).
 *
 * O formato e' {"items":[{"title":...,"body":...}]}. Arquivo ausente NAO e'
 * erro: a Home cai nas dicas compiladas. Arquivo corrompido tambem nao: aparece
 * no log e a tela continua funcionando — Home vazia por causa de um JSON de
 * terceiro seria pior que Home com dica. */
void carregar_novidade(Launcher &app)
{
    char path[512];
    char err[160];

    app.ui.news_title.clear();
    app.ui.news_body.clear();

    if (snprintf(path, sizeof(path), "%s/.cache/news.json",
                 app.options.user_dir.c_str()) >= (int)sizeof(path)) {
        return;
    }

    gj_value *doc = gj_parse_file(path, err, sizeof(err));

    if (doc == nullptr) {
        return; /* ausente e' o caso normal antes da primeira sincronizacao */
    }

    const gj_value *itens = gj_object_get(doc, "items");
    const gj_value *item = gj_array_at(itens, 0);

    if (item != nullptr) {
        app.ui.news_title = gj_as_string(gj_object_get(item, "title"), "");
        app.ui.news_body = gj_as_string(gj_object_get(item, "body"), "");

        if (!app.ui.news_title.empty()) {
            GLOG_INFO("novidade: %s", app.ui.news_title.c_str());
        }
    }
    gj_free(doc);
}

/* Nome de exibicao de um tema do catalogo (o do proprio arquivo, senao o id).
 * O menu mostra nome, nao id: "Neon Rosé" diz mais que "neon_rose". */
std::string theme_display_name(Launcher &app, const char *id)
{
    if (giochi_theme_catalog_load(&app.cat, app.options.asset_dir.c_str(),
                                  nullptr, 0) == 0) {
        const int i = giochi_theme_catalog_find(&app.cat, id);

        if (i >= 0) {
            return std::string(app.cat.entry[i].name);
        }
    }
    return std::string(id); /* sem catalogo: o id e' o melhor rotulo */
}

void build_menu(Launcher &app);

/* Troca de tema a quente: carrega o proximo do catalogo, recalcula a metrica e
 * grava em settings.json. Nao reinicia o Launcher e nao mexe em idioma/aba. */
void cycle_theme(Launcher &app)
{
    if (giochi_theme_catalog_load(&app.cat, app.options.asset_dir.c_str(), nullptr,
                                  0) != 0 || app.cat.count < 2) {
        GLOG_WARN("tema: catalogo com menos de 2 temas; nada a alternar");
        return;
    }

    int atual = giochi_theme_catalog_find(&app.cat, app.theme.id);

    if (atual < 0) {
        atual = giochi_theme_catalog_find(&app.cat, app.settings.theme_id);
    }
    const char *proximo = app.cat.entry[(atual + 1) % app.cat.count].id;
    char err[128] = {0};

    if (giochi_theme_load(&app.theme, app.options.asset_dir.c_str(),
                          (std::string("themes/") + proximo + ".json").c_str(),
                          err, sizeof(err)) != 0) {
        GLOG_WARN("tema: '%s' nao carregou (%s); mantendo '%s'", proximo, err,
                  app.theme.id);
        return;
    }

    app.theme_metrics = giochi::ui_theme_metrics_from(app.theme);
    giochi_strlcpy(app.settings.theme_id, proximo,
                   sizeof(app.settings.theme_id));

    if (giochi_settings_save(&app.settings, app.options.user_dir.c_str(), err,
                             sizeof(err)) != 0) {
        GLOG_WARN("settings.json: nao foi possivel gravar theme_id (%s)", err);
    }
    GLOG_INFO("tema: '%s' (%s, densidade %s)", proximo, app.theme.id,
              giochi_density_name(app.theme.density));

    /* A barra de dicas e os rotulos mudam com o tema: refazer agora evita um
     * frame com a dica do tema anterior. */
    set_hint(app);
    build_menu(app);
}

/* Monta a lista que o render desenha. O render NAO conhece idioma nem acao:
 * recebe rotulo ja' traduzido + valor. Antes ele comparava o texto do item com
 * "Desligar" para decidir a acao, o que quebraria ao traduzir.
 *
 * A ordem de kSistema casa com giochi_overlay (overlay.cpp) e com os indices
 * GIOCHI_SYSTEM_*_INDEX: idioma, fuso, brilho, suspensao, USB, desenvolvedor. */
void build_menu(Launcher &app)
{
    app.menu.clear();
    app.menu_level = app.overlay.level;

    if (app.overlay.level == 0) {
        const int n = giochi_overlay_count(&app.overlay);

        app.menu_title = app.i18n.tr("settings.title");

        for (int i = 0; i < n; i++) {
            static const char *const chaves[GIOCHI_OVERLAY_ITEM_COUNT] = {
                "settings.appearance", "settings.system", "settings.sound",
                "settings.store",      "settings.network", "settings.about",
                "settings.poweroff"};

            giochi::UiMenuItem item;

            item.label = i < GIOCHI_OVERLAY_ITEM_COUNT
                             ? app.i18n.tr(chaves[i])
                             : giochi_overlay_item(i);
            item.has_submenu = i == 1; /* Sistema */
            app.menu.push_back(item);
        }
        return;
    }

    app.menu_title = app.i18n.tr("settings.system");

    const bool dev = app.settings.developer_ssh_usb_enabled != 0;
    const bool mass = std::strcmp(app.settings.usb_mode, "mass") == 0;

    /* tr_n, e NAO snprintf com o texto do catalogo como formato: uma traducao
     * com "%" (ou pior, "%s") viraria leitura de pilha. O catalogo guarda
     * "{n} min"; a substituicao e' textual e previsivel. */
    const std::string brilho =
        app.i18n.tr_n("settings.level", app.settings.brightness);
    const std::string suspenso = app.i18n.tr_n(
        "settings.minutes", app.settings.suspend_timeout_s / 60);

    struct Linha {
        const char *chave;
        std::string valor;
    };
    const Linha linhas[GIOCHI_SYSTEM_ITEM_COUNT] = {
        {"settings.language", app.i18n.display_name()},
        {"settings.timezone", app.settings.timezone},
        {"settings.brightness", brilho},
        {"settings.suspend", suspenso},
        {"settings.usb_mode", mass ? "Mass Storage" : "MTP"},
        {"settings.dev_mode",
         app.i18n.tr(dev ? "settings.on" : "settings.off")},
        {"settings.theme", theme_display_name(app, app.theme.id)}};

    for (int i = 0; i < GIOCHI_SYSTEM_ITEM_COUNT; i++) {
        giochi::UiMenuItem item;

        item.label = app.i18n.tr(linhas[i].chave);
        item.value = linhas[i].valor;
        app.menu.push_back(item);
    }
}

void activate_tab(Launcher &app)
{
    if (app.current_tab < 0 || app.current_tab >= app.layout.tab_count) {
        return;
    }
    const giochi_tab &tab = app.layout.tabs[app.current_tab];

    if (tab.is_app) {
        GLOG_INFO("lancando app externo: %s", tab.target);
        send_transition(app, GIOCHI_TRANS_OPEN_APP, tab.target, "");
        return;
    }
    GLOG_INFO("aba '%s' sem acao de ativacao (grid)", tab.id);
}

/* Secao 2.2 passo 1: envia caminho do pacote + metadata.json inline. */
void activate_library_item(Launcher &app)
{
    if (app.library_focus < 0 ||
        static_cast<std::size_t>(app.library_focus) >= app.library.size()) {
        app.ui.hint = "Biblioteca vazia";
        return;
    }

    const giochi::LibraryEntry &entry =
        app.library[static_cast<std::size_t>(app.library_focus)];

    if (!entry.valid) {
        app.ui.hint = "Rejeitado: " + entry.error;
        GLOG_WARN("biblioteca: lancamento recusado (%s)", entry.error.c_str());
        return;
    }

    /* Metadata vem do cache do install (<sha256>.json). */
    std::string meta_path = app.options.user_dir + "/.cache/meta/" +
                            entry.sha256 + ".json";
    std::size_t len = 0;
    char *text = giochi_read_file(meta_path.c_str(), &len);
    std::string metadata = text != nullptr ? std::string(text, len)
                                           : std::string();

    free(text);

    if (metadata.empty()) {
        GLOG_WARN("biblioteca: metadata em cache ausente para %s",
                  entry.title.c_str());
    }
    GLOG_INFO("lancando '%s' (%s)", entry.title.c_str(),
              entry.package_path.c_str());
    send_transition(app, GIOCHI_TRANS_LAUNCH, entry.package_path, metadata);
}

void handle_action(Launcher &app, giochi::Action action)
{
    switch (action) {
    case giochi::ActionTabPrev:
        if (app.overlay.open) {
            giochi_overlay_move(&app.overlay, -1);
        } else {
            app.current_tab = giochi_layout_prev(&app.layout, app.current_tab);
            tab_entrou(app);
        }
        break;
    case giochi::ActionTabNext:
        if (app.overlay.open) {
            giochi_overlay_move(&app.overlay, 1);
        } else {
            app.current_tab = giochi_layout_next(&app.layout, app.current_tab);
            tab_entrou(app);
        }
        break;
    case giochi::ActionActivate:
        if (!app.overlay.open &&
            std::strcmp(giochi_layout_tab_id(&app.layout, app.current_tab),
                        "library") == 0) {
            activate_library_item(app);
            break;
        }
        if (app.overlay.open) {
            const char *item = giochi_overlay_current(&app.overlay);

            if (app.overlay.level == 0) {
                if (std::strcmp(item, "Desligar") == 0) {
                    control_command(app, "POWEROFF");
                } else {
                    (void)giochi_overlay_enter(&app.overlay);
                }
            } else if (app.overlay.level == 1 && app.overlay.index == 0) {
                /* Idioma e' o primeiro item do submenu Sistema. */
                cycle_language(app);
            } else if (app.overlay.index == GIOCHI_SYSTEM_USB_MODE_INDEX) {
                /* Alterna MTP <-> Mass Storage. A rede USB continua ativa. */
                const char *next = std::strcmp(app.settings.usb_mode, "mtp") == 0
                                       ? "mass" : "mtp";

                set_usb_mode(app, next);
            } else if (app.overlay.index == GIOCHI_SYSTEM_DEV_MODE_INDEX) {
                set_developer_mode(app,
                                   app.settings.developer_ssh_usb_enabled == 0);
            } else if (app.overlay.index == GIOCHI_SYSTEM_THEME_INDEX) {
                cycle_theme(app);
            }
        } else if (std::strcmp(giochi_layout_tab_id(&app.layout,
                                                    app.current_tab),
                               "music") == 0) {
            music_command(app, "PLAY");
        } else {
            activate_tab(app);
        }
        break;
    case giochi::ActionBack:
        if (app.overlay.open) {
            (void)giochi_overlay_back(&app.overlay);
        } else {
            app.current_tab = app.layout.default_index;
    carregar_novidade(app);
        }
        break;
    case giochi::ActionHome:
        giochi_overlay_close(&app.overlay);
        app.current_tab = app.layout.center_index;
        break;
    case giochi::ActionSettingsToggle:
        giochi_overlay_toggle(&app.overlay);
        break;
    case giochi::ActionExitGame:
        send_transition(app, GIOCHI_TRANS_EXIT_GAME, "", "");
        break;
    case giochi::ActionMoveUp:
        if (app.overlay.open) {
            giochi_overlay_move(&app.overlay, -1);
        } else if (std::strcmp(giochi_layout_tab_id(&app.layout,
                                                    app.current_tab),
                               "library") == 0) {
            if (app.library_focus > 0) {
                app.library_focus--;
            }
        } else if (app.focus_index > 0) {
            app.focus_index--;
        }
        break;
    case giochi::ActionMoveDown:
        if (app.overlay.open) {
            giochi_overlay_move(&app.overlay, 1);
        } else if (std::strcmp(giochi_layout_tab_id(&app.layout,
                                                    app.current_tab),
                               "library") == 0) {
            if (static_cast<std::size_t>(app.library_focus + 1) <
                app.library.size()) {
                app.library_focus++;
            }
        } else {
            app.focus_index++;
        }
        break;
    case giochi::ActionNextTrack:
        if (std::strcmp(giochi_layout_tab_id(&app.layout, app.current_tab),
                        "music") == 0) {
            music_command(app, "NEXT");
        }
        break;
    case giochi::ActionPrevTrack:
        if (std::strcmp(giochi_layout_tab_id(&app.layout, app.current_tab),
                        "music") == 0) {
            music_command(app, "PREV");
        }
        break;
    case giochi::ActionNone:
    default:
        break;
    }
}

void pump_events(Launcher &app, int timeout_ms)
{
    giochi::Button button = giochi::BtnNone;
    bool pressed = false;

    if (app.evdev == nullptr) {
        usleep(static_cast<useconds_t>(timeout_ms) * 1000U);
        return;
    }
    if (giochi_evdev_poll(app.evdev, timeout_ms, &app.input, &button,
                          &pressed) == 1) {
        giochi::Action action = giochi::input_handle(&app.input, button,
                                                     pressed, giochi_now_ms());

        if (action != giochi::ActionNone) {
            handle_action(app, action);
        }
    }
}

bool init(Launcher &app)
{
    char err[256] = {0};

    app.control_sock = app.options.run_dir + "/control.sock";
    app.music_sock = app.options.run_dir + "/music.sock";
    app.transition_fifo = app.options.run_dir + "/transition";
    app.transition_fd = giochi_transition_open(app.transition_fifo.c_str());
    if (app.transition_fd < 0) {
        GLOG_WARN("transition: nao foi possivel abrir %s",
                  app.transition_fifo.c_str());
    }

    if (giochi_layout_load(&app.layout, app.options.asset_dir.c_str(),
                           "layout.json", err, sizeof(err)) != 0) {
        GLOG_WARN("layout: fallback compilado (%s)", err);
    }
    if (giochi_settings_load(&app.settings, app.options.user_dir.c_str(),
                             err, sizeof(err)) != 0) {
        GLOG_WARN("settings.json: defaults (%s)", err);
    }

    /* Tema ativo: --theme -> GIOCHI_THEME -> settings.json -> padrao do
     * aparelho -> padrao do catalogo. O id que valeu vai para o log: sem ele,
     * "a UI mudou de cor sozinha" e' um mistério sem rastro (§3.47). */
    {
        char device_theme[GIOCHI_THEME_ID_LEN] = {0};
        char used[GIOCHI_THEME_ID_LEN] = {0};
        const char *env_theme = std::getenv("GIOCHI_THEME");

        /* settings.json AUSENTE nao e' escolha do usuario: o `theme_id` que
         * esta' em memoria veio do default COMPILADO, nao do arquivo. Se ele
         * contasse, o tema de fabrica do aparelho (device-theme.json) nunca
         * valeria — medido: a X55 subia em dark_default com neon_rose na
         * imagem. */
        char settings_path[512];
        const bool tem_settings =
            snprintf(settings_path, sizeof(settings_path), "%s/settings.json",
                     app.options.user_dir.c_str()) < (int)sizeof(settings_path) &&
            access(settings_path, R_OK) == 0;

        const char *pedido = !app.options.theme_id.empty()
                                 ? app.options.theme_id.c_str()
                                 : (env_theme != nullptr
                                        ? env_theme
                                        : (tem_settings ? app.settings.theme_id
                                                        : nullptr));

        (void)giochi_theme_device_default(device_theme, sizeof(device_theme),
                                          app.options.asset_dir.c_str());

        const int rc = giochi_theme_resolve(
            &app.theme, app.options.asset_dir.c_str(), pedido, device_theme,
            used, sizeof(used), err, sizeof(err));

        /* `err` pode vir preenchido mesmo com rc=0: e' o caso do tema pedido
         * que nao existe (cai no padrao, mas o pedido precisa aparecer no log,
         * senao o settings.json velho muda a UI em silencio). */
        if (err[0] != '\0') {
            GLOG_WARN("tema: %s", err);
        }
        if (rc == 0) {
            GLOG_INFO("tema: '%s' (%s, densidade %s, dicas %s)", used,
                      app.theme.id, giochi_density_name(app.theme.density),
                      giochi_hints_name(app.theme.hints));
        } else {
            GLOG_WARN("tema: fallback compilado");
        }

        /* settings.json pode apontar para um tema que nao existe mais: nesse
         * caso o arquivo e' corrigido para o que realmente valeu. */
        if (used[0] != '\0' &&
            std::strcmp(app.settings.theme_id, used) != 0) {
            giochi_strlcpy(app.settings.theme_id, used,
                           sizeof(app.settings.theme_id));
        }
    }

    app.ui.layout = &app.layout;
    app.ui.theme = &app.theme;
    app.ui.theme_metrics = &app.theme_metrics;
    app.theme_metrics = giochi::ui_theme_metrics_from(app.theme);
    app.current_tab = app.layout.default_index;
    carregar_novidade(app);

    giochi_overlay_init(&app.overlay);

    /* O giochi-musicd varre /user/music UMA VEZ, quando sobe — que e' antes de
     * o usuario copiar musica por MTP/SSH. Sem pedir a varredura agora, uma
     * faixa adicionada depois do boot nunca aparece (medido no aparelho:
     * "0 faixas" no boot, "1 faixas" so' apos reiniciar o daemon). O RESCAN
     * nao mexe na faixa atual, entao tambem e' seguro ao voltar do jogo. */
    if (!app.music_sock.empty()) {
        music_command(app, "RESCAN");
        music_refresh(app);
    }

    /* Idioma: GIOCHI_LANG -> settings.json -> padrao. Carregado ANTES de
     * qualquer rotulo existir, senao o primeiro frame sai com chaves cruas. */
    {
        const std::string lang =
            giochi::i18n_resolve_language(app.options.user_dir.c_str());

        app.i18n.load(app.options.asset_dir.c_str(), lang.c_str());
    }
    giochi::input_reset(&app.input);

    /* Secao 2.2: mapa canonico em /etc/giochi/buttons.map. A secao usada
     * (default|x35s) vem de /etc/giochi/device.conf, gerado no build.
     *
     * A leitura passa por giochi_deviceconf_get(): a versao anterior procurava
     * "GIOCHI_BUTTONS_MAP=" e pulava 20 caracteres, mas a chave tem 19 — a
     * secao virava "efault" e o mapa compilado entrava no lugar, em silencio
     * (log real da X55: "buttons.map: secao [efault] ausente"). */
    if (const char *env_map = getenv("GIOCHI_BUTTONS_MAP")) {
        app.options.buttons_map = env_map;
    } else {
        std::string device_conf = rooted(GIOCHI_DEVICECONF_PATH);
        char value[64];

        if (giochi_deviceconf_get(device_conf.c_str(), "GIOCHI_BUTTONS_MAP",
                                  value, sizeof(value)) == 0) {
            app.options.buttons_map = value;
        }
    }
    {
        std::string map_file = app.options.asset_dir + "/buttons.map";
        if (giochi::input_load_button_map(&app.input, map_file,
                                          app.options.buttons_map) != 0) {
            std::string etc_map = rooted("/etc/giochi/buttons.map");
            (void)giochi::input_load_button_map(&app.input, etc_map,
                                                app.options.buttons_map);
        }
    }

    if (!app.options.headless) {
        std::string render_err;
        app.renderer = giochi::render_open(app.layout, &render_err);
        if (app.renderer == nullptr) {
            GLOG_WARN("render: SDL2 indisponivel (%s); usando headless",
                      render_err.c_str());
        }
        app.evdev = giochi_evdev_open(err, sizeof(err));
        if (app.evdev == nullptr) {
            GLOG_WARN("evdev: sem gamepad (%s)", err);
        }
    }

    {
        std::string games = app.options.user_dir + "/games";
        std::string meta = app.options.user_dir + "/.cache/meta";
        std::string thumbs = app.options.user_dir + "/.cache/thumbs";

        app.library = giochi::library_scan(games, meta, thumbs,
                                          giochi::device_proton_supported());
        app.library_loaded = true;

        std::size_t valid = 0;

        for (const auto &entry : app.library) {
            if (entry.valid) {
                valid++;
            } else {
                GLOG_WARN("biblioteca: '%s' rejeitado: %s",
                          entry.title.c_str(), entry.error.c_str());
            }
        }
        GLOG_INFO("biblioteca: %zu pacotes (%zu validos)", app.library.size(),
                  valid);

        /* O renderer recebe so o que precisa desenhar: titulo + validade. */
        app.ui_items.clear();
        app.ui_items.reserve(app.library.size());
        for (const auto &entry : app.library) {
            giochi::UiItem item;

            item.title = entry.title.empty() ? entry.package_path : entry.title;
            item.valid = entry.valid;
            app.ui_items.push_back(std::move(item));
        }
    }

    set_hint(app);
    GLOG_INFO("launcher v%s pronto: %d abas, default '%s', render=%s",
              kVersion, app.layout.tab_count,
              giochi_layout_tab_id(&app.layout, app.layout.default_index),
              (app.renderer != nullptr ? app.renderer->name() : "none"));
    return true;
}

void shutdown(Launcher &app)
{
    if (app.evdev != nullptr) {
        giochi_evdev_close(app.evdev);
        app.evdev = nullptr;
    }
    if (app.renderer != nullptr) {
        delete app.renderer;
        app.renderer = nullptr;
    }
    if (app.transition_fd >= 0) {
        close(app.transition_fd);
        app.transition_fd = -1;
    }
}

}  // namespace

int main(int argc, char **argv)
{
    Launcher app;
    int frames = 0;

    giochi_log_set_level(giochi_log_level_from_env());

    if (!parse_args(argc, argv, &app.options)) {
        return 2;
    }
    if (getenv("GIOCHI_HEADLESS") != nullptr) {
        app.options.headless = true;
    }

    if (!init(app)) {
        shutdown(app);
        return 1;
    }

    while (app.running) {
        if (app.renderer != nullptr && app.renderer->should_quit()) {
            break;
        }

        app.ui.current_tab = app.current_tab;
        app.ui.focus_index = app.focus_index;
        /* Aba Musica: a tela le' o estado que music_refresh carregou. */
        app.ui.music = &app.music;
        app.ui.music_focus = app.focus_index;
        app.ui.settings_open = app.overlay.open != 0;
        app.ui.settings_index = app.overlay.index;
        app.ui.focus_index = app.library_focus;
        app.ui.items = app.ui_items.empty() ? nullptr : app.ui_items.data();
        app.ui.item_count = static_cast<int>(app.ui_items.size());

        /* Os indicadores mudam em segundos, nao em quadros: ler /sys a 60 fps
         * seria desperdicio. Uma vez por segundo basta para bateria e relogio. */
        if (frames % 60 == 0) {
            giochi::status_update(&app.status);
        }
        app.ui.i18n = &app.i18n;
        app.ui.status = &app.status;
        app.ui.settings = &app.settings;

        if (app.overlay.open) {
            build_menu(app);
            app.ui.menu = app.menu.empty() ? nullptr : app.menu.data();
            app.ui.menu_count = static_cast<int>(app.menu.size());
            app.ui.menu_title = app.menu_title;
            app.ui.menu_level = app.menu_level;
        } else {
            app.ui.menu = nullptr;
            app.ui.menu_count = 0;
        }
        set_hint(app);

        if (app.renderer != nullptr) {
            if (app.renderer->frame(app.ui) != 0) {
                GLOG_ERR("render: frame falhou; encerrando");
                break;
            }
        }
        pump_events(app, 16);

        frames++;
        if (app.options.frames > 0 && frames >= app.options.frames) {
            break;
        }
    }

    GLOG_INFO("launcher encerrado apos %d frames", frames);
    shutdown(app);
    return 0;
}
