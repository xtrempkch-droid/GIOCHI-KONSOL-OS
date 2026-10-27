/* Giochi Konsol OS v1.2 — giochi-launcher (ADR-09: C++17 + SDL2).
 *
 * Esqueleto funcional: carrega layout.json/theme.json/settings.json, mantem o
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
#include "overlay.hpp"
#include "render.hpp"
#include "settings.hpp"
#include "theme.hpp"

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
    bool headless = false;
    int frames = 0; /* >0 encerra apos N frames (usado em smoke test) */
};

struct Launcher {
    Options options;
    giochi_layout layout{};
    giochi_theme theme{};
    giochi_settings settings{};
    giochi_overlay overlay{};
    giochi::InputState input{};
    giochi::UiState ui{};
    giochi::Renderer *renderer = nullptr;
    giochi_evdev *evdev = nullptr;
    bool running = true;

    int current_tab = 0;
    int focus_index = 0;
    std::vector<giochi::LibraryEntry> library;
    int library_focus = 0;
    bool library_loaded = false;

    std::string control_sock;
    std::string music_sock;
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

void music_command(Launcher &app, const char *command)
{
    char reply[512];

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


void set_hint(Launcher &app)
{
    const char *tab = giochi_layout_tab_id(&app.layout, app.current_tab);
    const char *hint = "L/R aba  A ok  B voltar  Select ajustes  Home inicio";

    if (app.overlay.open) {
        hint = "A entrar  B fechar  Select fechar";
    } else if (std::strcmp(tab, "home") == 0) {
        hint = "L/R aba  A abrir  Select ajustes";
    } else if (std::strcmp(tab, "music") == 0) {
        hint = "A play/pause  X proxima  Y anterior  B voltar";
    }
    app.ui.hint = hint;
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
        }
        break;
    case giochi::ActionTabNext:
        if (app.overlay.open) {
            giochi_overlay_move(&app.overlay, 1);
        } else {
            app.current_tab = giochi_layout_next(&app.layout, app.current_tab);
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

            if (std::strcmp(item, "Desligar") == 0) {
                control_command(app, "POWEROFF");
            } else if (std::strcmp(item, "Sistema") == 0) {
                /* Alterna MTP <-> Mass Storage (secao 4.9.3, item Sistema).
                 * A rede USB (SSH em 10.55.0.1) continua ativa nos dois. */
                const char *next = std::strcmp(app.settings.usb_mode, "mtp") == 0
                                       ? "mass" : "mtp";

                set_usb_mode(app, next);
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
            giochi_overlay_close(&app.overlay);
        } else {
            app.current_tab = app.layout.default_index;
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
    if (giochi_theme_load(&app.theme, app.options.asset_dir.c_str(),
                          "theme.json", err, sizeof(err)) != 0) {
        GLOG_WARN("tema: fallback compilado (%s)", err);
    }
    if (giochi_settings_load(&app.settings, app.options.user_dir.c_str(),
                             err, sizeof(err)) != 0) {
        GLOG_WARN("settings.json: defaults (%s)", err);
    }

    app.ui.layout = &app.layout;
    app.ui.theme = &app.theme;
    app.current_tab = app.layout.default_index;

    giochi_overlay_init(&app.overlay);
    giochi::input_reset(&app.input);

    /* Secao 2.2: mapa canonico em /etc/giochi/buttons.map. A secao usada
     * (default|x35s) vem de /etc/giochi/device.conf, gerado no build. */
    if (const char *env_map = getenv("GIOCHI_BUTTONS_MAP")) {
        app.options.buttons_map = env_map;
    } else {
        std::string device_conf = rooted("/etc/giochi/device.conf");
        std::size_t len = 0;
        char *text = giochi_read_file(device_conf.c_str(), &len);

        if (text != nullptr) {
            std::string content(text, len);
            std::size_t pos = content.find("GIOCHI_BUTTONS_MAP=");

            if (pos != std::string::npos) {
                std::size_t start = pos + 20;
                std::size_t end = content.find('\n', start);
                std::string value = content.substr(start, end - start);

                while (!value.empty() &&
                       (value.back() == ' ' || value.back() == '\r')) {
                    value.pop_back();
                }
                if (!value.empty()) {
                    app.options.buttons_map = value;
                }
            }
            free(text);
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
        app.ui.settings_open = app.overlay.open != 0;
        app.ui.settings_index = app.overlay.index;
        app.ui.focus_index = app.library_focus;
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
