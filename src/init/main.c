/* Giochi Konsol OS v1.2 — giochi-init (ADR-10).
 *
 * Supervisor pai do Launcher, dos jogos, apps e daemons. Expoe:
 *   /run/giochi/control.sock   comandos de texto (secao 4.9.4)
 *   /run/giochi/transition     FIFO binaria versionada (S4)
 * e regenera /user/.giochi/device_profile.json a cada boot (secao 4.5.2).
 *
 * Loop unico de poll() sobre: self-pipe (SIGCHLD/SIGTERM), control.sock,
 * FIFO de transicao e os fds evdev do hotkey. Sem races: o handler de sinal
 * apenas escreve um byte.
 */
#include "control.h"
#include "device_profile.h"
#include "game.h"
#include "hotkey.h"
#include "power.h"
#include "supervisor.h"

#include "giochi_log.h"
#include "giochi_transition.h"
#include "giochi_util.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GIOCHI_VERSION "1.2"
#define GIOCHI_LOOP_TIMEOUT_MS 500

typedef struct {
    giochi_supervisor supervisor;
    giochi_control control;
    giochi_hotkey_device hotkey;
    int transition_fd;
    int wake_pipe[2];
    int quit;
} giochi_init_ctx;

static giochi_init_ctx *g_ctx = NULL;

static void on_signal(int sig)
{
    unsigned char byte = (unsigned char)sig;

    if (g_ctx != NULL && g_ctx->wake_pipe[1] >= 0) {
        ssize_t ignored = write(g_ctx->wake_pipe[1], &byte, 1);
        (void)ignored;
    }
}

static int install_signal_handlers(giochi_init_ctx *ctx)
{
    struct sigaction sa;

    if (pipe2(ctx->wake_pipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        GLOG_ERR("pipe2: %s", strerror(errno));
        return -1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    (void)sigaction(SIGCHLD, &sa, NULL);
    (void)sigaction(SIGTERM, &sa, NULL);
    (void)sigaction(SIGINT, &sa, NULL);
    (void)sigaction(SIGHUP, &sa, NULL);
    return 0;
}

static void drain_wake_pipe(giochi_init_ctx *ctx)
{
    unsigned char buf[64];
    ssize_t got;

    while ((got = read(ctx->wake_pipe[0], buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < got; i++) {
            if (buf[i] == (unsigned char)SIGTERM ||
                buf[i] == (unsigned char)SIGINT ||
                buf[i] == (unsigned char)SIGHUP) {
                ctx->quit = 1;
            }
        }
    }
}

static void reply_ok(char *reply, size_t replysz, const char *what)
{
    (void)snprintf(reply, replysz, "OK %s", what);
}

static int handle_radio(char *reply, size_t replysz, const char *what, int rc)
{
    if (rc == 0) {
        reply_ok(reply, replysz, what);
        return 0;
    }
    if (rc == -2) {
        (void)snprintf(reply, replysz, "ERR no_adapter");
    } else {
        (void)snprintf(reply, replysz, "ERR %s_failed", what);
    }
    return -1;
}

static int handle_command(const char *cmd, char *reply, size_t replysz,
                          void *user)
{
    giochi_init_ctx *ctx = (giochi_init_ctx *)user;
    char line[512];
    char *verb;
    char *arg;

    giochi_strlcpy(line, cmd, sizeof(line));
    verb = line;
    while (*verb == ' ') {
        verb++;
    }
    arg = strchr(verb, ' ');
    if (arg != NULL) {
        *arg++ = '\0';
        while (*arg == ' ') {
            arg++;
        }
    }

    GLOG_DEBUG("control.sock <- '%s'", cmd);

    if (strcmp(verb, "PING") == 0) {
        (void)snprintf(reply, replysz, "PONG");
        return 0;
    }

    if (strcmp(verb, "POWEROFF") == 0 || strcmp(verb, "REBOOT") == 0 ||
        strcmp(verb, "SUSPEND") == 0) {
        const char *action = strcmp(verb, "POWEROFF") == 0 ? "poweroff"
                           : strcmp(verb, "REBOOT") == 0 ? "reboot"
                           : "suspend";

        if (giochi_power_execute(action) != 0) {
            (void)snprintf(reply, replysz, "ERR %s_failed", action);
            return -1;
        }
        reply_ok(reply, replysz, action);
        if (strcmp(action, "suspend") != 0) {
            ctx->quit = 1;
        }
        return 0;
    }

    if (strcmp(verb, "USB_MODE") == 0) {
        if (arg == NULL || *arg == '\0') {
            (void)snprintf(reply, replysz, "ERR missing_mode");
            return -1;
        }
        if (strcmp(arg, "MTP") != 0 && strcmp(arg, "MASS") != 0) {
            (void)snprintf(reply, replysz, "ERR invalid_mode");
            return -1;
        }
        if (giochi_usb_mode_set(arg) != 0) {
            (void)snprintf(reply, replysz, "ERR usb_mode_failed");
            return -1;
        }
        (void)snprintf(reply, replysz, "OK usb_mode %s", arg);
        return 0;
    }

    if (strcmp(verb, "SET_THEME") == 0) {
        if (arg == NULL || *arg == '\0') {
            (void)snprintf(reply, replysz, "ERR missing_theme");
            return -1;
        }
        /* TODO(v1.2): publicar o tema no Launcher (evento dedicado). */
        (void)snprintf(reply, replysz, "OK theme %s", arg);
        return 0;
    }

    if (strcmp(verb, "LAUNCH_APP") == 0) {
        if (arg == NULL || *arg == '\0') {
            (void)snprintf(reply, replysz, "ERR missing_app");
            return -1;
        }
        if (giochi_supervisor_launch(&ctx->supervisor, arg,
                                     GIOCHI_LAUNCH_KIND_APP) != 0) {
            (void)snprintf(reply, replysz, "ERR launch_failed");
            return -1;
        }
        (void)snprintf(reply, replysz, "OK launch %s", arg);
        return 0;
    }

    if (strcmp(verb, "WIFI_UP") == 0) {
        return handle_radio(reply, replysz, "wifi_up", giochi_wifi_set(1));
    }
    if (strcmp(verb, "WIFI_DOWN") == 0) {
        return handle_radio(reply, replysz, "wifi_down", giochi_wifi_set(0));
    }
    if (strcmp(verb, "BT_UP") == 0) {
        return handle_radio(reply, replysz, "bt_up", giochi_bt_set(1));
    }
    if (strcmp(verb, "BT_DOWN") == 0) {
        return handle_radio(reply, replysz, "bt_down", giochi_bt_set(0));
    }

    (void)snprintf(reply, replysz, "ERR unknown_command");
    return -1;
}

static void handle_transition(giochi_init_ctx *ctx)
{
    for (;;) {
        giochi_transition_msg msg;
        int rc = giochi_transition_recv(ctx->transition_fd, &msg);

        if (rc <= 0) {
            return;
        }

        switch (msg.cmd) {
        case GIOCHI_TRANS_LAUNCH: {
            char binary[GIOCHI_MAX_PATH];
            char err[192] = {0};

            GLOG_INFO("transition: lancar jogo '%s'", msg.path);

            /* Secao 2.2 passo 3: extrai (ADR-11) e resolve o entrypoint. */
            if (giochi_game_prepare(msg.path, msg.metadata, binary,
                                    sizeof(binary), err, sizeof(err)) != 0) {
                GLOG_ERR("transition: %s", err);
                giochi_supervisor_revive_launcher(&ctx->supervisor);
                break;
            }
            if (giochi_supervisor_launch(&ctx->supervisor, binary,
                                         GIOCHI_LAUNCH_KIND_GAME) != 0) {
                GLOG_ERR("transition: falha ao lancar '%s'", binary);
            }
            break;
        }
        case GIOCHI_TRANS_OPEN_APP:
            GLOG_INFO("transition: abrir app '%s'", msg.path);
            if (giochi_supervisor_launch(&ctx->supervisor, msg.path,
                                         GIOCHI_LAUNCH_KIND_APP) != 0) {
                GLOG_ERR("transition: falha ao abrir '%s'", msg.path);
            }
            break;
        case GIOCHI_TRANS_EXIT_GAME:
            (void)giochi_supervisor_exit_game(&ctx->supervisor);
            break;
        case GIOCHI_TRANS_RETURN_HOME:
            GLOG_INFO("transition: retornar para Home");
            giochi_supervisor_revive_launcher(&ctx->supervisor);
            break;
        default:
            GLOG_WARN("transition: comando desconhecido %u", msg.cmd);
            break;
        }
    }
}

static void usage(FILE *out)
{
    (void)fprintf(out,
        "uso: giochi-init [opcoes]\n"
        "  --profile PATH   caminho de device_profile.json\n"
        "  --no-spawn       nao inicia Launcher/musicd (testes)\n"
        "  --once           atende um comando de control.sock e sai\n"
        "  --version        imprime a versao\n"
        "  --help           esta ajuda\n");
}

int main(int argc, char **argv)
{
    giochi_init_ctx ctx;
    const char *profile_arg = NULL;
    const char *sock_env;
    char profile_path[512];
    char run_dir[256];
    char sock_path[256];
    char fifo_path[256];
    int no_spawn = 0;
    int once = 0;
    int status = 0;
    int i;

    memset(&ctx, 0, sizeof(ctx));
    ctx.control.fd = -1;
    ctx.transition_fd = -1;
    ctx.wake_pipe[0] = -1;
    ctx.wake_pipe[1] = -1;
    g_ctx = &ctx;
    giochi_log_set_level(giochi_log_level_from_env());

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0) {
            (void)printf("giochi-init %s\n", GIOCHI_VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--no-spawn") == 0) {
            no_spawn = 1;
            continue;
        }
        if (strcmp(argv[i], "--once") == 0) {
            once = 1;
            continue;
        }
        if (strcmp(argv[i], "--profile") == 0) {
            if (++i >= argc) {
                usage(stderr);
                return 2;
            }
            profile_arg = argv[i];
            continue;
        }
        (void)fprintf(stderr, "giochi-init: opcao desconhecida '%s'\n",
                      argv[i]);
        usage(stderr);
        return 2;
    }

    if (getenv("GIOCHI_NO_SPAWN") != NULL) {
        no_spawn = 1;
    }

    if (giochi_path(run_dir, sizeof(run_dir), "/run/giochi") != 0 ||
        giochi_mkdir_p(run_dir, 0755) != 0) {
        GLOG_ERR("nao foi possivel criar %s", run_dir);
        return 1;
    }

    if (profile_arg != NULL) {
        giochi_strlcpy(profile_path, profile_arg, sizeof(profile_path));
    } else if (giochi_path(profile_path, sizeof(profile_path),
                           "/user/.giochi/device_profile.json") != 0) {
        GLOG_ERR("caminho de device_profile.json longo demais");
        return 1;
    }

    if (giochi_device_profile_write(profile_path) != 0) {
        GLOG_WARN("seguindo sem device_profile.json atualizado");
    }

    sock_env = getenv("GIOCHI_CONTROL_SOCK");
    if (sock_env != NULL && *sock_env != '\0') {
        giochi_strlcpy(sock_path, sock_env, sizeof(sock_path));
    } else if (giochi_path(sock_path, sizeof(sock_path),
                           "/run/giochi/control.sock") != 0) {
        GLOG_ERR("caminho de control.sock longo demais");
        return 1;
    }

    if (giochi_control_open(&ctx.control, sock_path) != 0) {
        GLOG_ERR("control.sock indisponivel");
        return 1;
    }

    if (giochi_path(fifo_path, sizeof(fifo_path), "/run/giochi/transition") != 0) {
        GLOG_ERR("caminho do FIFO longo demais");
        giochi_control_close(&ctx.control);
        return 1;
    }
    ctx.transition_fd = giochi_transition_open(fifo_path);
    if (ctx.transition_fd < 0) {
        GLOG_WARN("FIFO de transicao indisponivel; jogos nao serao lancados");
    }

    if (!no_spawn) {
        char hotkey_err[128];

        if (giochi_hotkey_open(&ctx.hotkey, hotkey_err,
                               sizeof(hotkey_err)) != 0) {
            GLOG_WARN("hotkey: %s", hotkey_err);
        }
    }

    giochi_supervisor_init(&ctx.supervisor);
    if (!no_spawn) {
        (void)giochi_supervisor_add(&ctx.supervisor, "musicd", "giochi-musicd",
                                    NULL, 1);
        (void)giochi_supervisor_add(&ctx.supervisor, "launcher",
                                    "giochi-launcher", NULL, 1);
        (void)giochi_supervisor_start_all(&ctx.supervisor);
    } else {
        GLOG_INFO("--no-spawn: supervisionando apenas comandos");
    }

    if (install_signal_handlers(&ctx) != 0) {
        status = 1;
        goto done;
    }

    GLOG_INFO("Giochi Konsol OS v%s - giochi-init pronto (soc=%s)",
              GIOCHI_VERSION, giochi_device_soc());

    while (!ctx.quit) {
        struct pollfd pfds[3 + GIOCHI_HOTKEY_MAX_FDS];
        int nfds = 0;
        int hotkey_offset;
        int hotkey_count;
        int control_index;
        int transition_index;
        int rc;

        pfds[nfds].fd = ctx.wake_pipe[0];
        pfds[nfds].events = POLLIN;
        pfds[nfds].revents = 0;
        nfds++;

        control_index = -1;
        if (ctx.control.fd >= 0) {
            pfds[nfds].fd = ctx.control.fd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            control_index = nfds;
            nfds++;
        }

        transition_index = -1;
        if (ctx.transition_fd >= 0) {
            pfds[nfds].fd = ctx.transition_fd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            transition_index = nfds;
            nfds++;
        }

        hotkey_offset = nfds;
        hotkey_count = giochi_hotkey_pollfds(&ctx.hotkey, pfds, nfds,
                                             (int)(sizeof(pfds) /
                                                   sizeof(pfds[0])));
        nfds += hotkey_count;

        rc = poll(pfds, (nfds_t)nfds, GIOCHI_LOOP_TIMEOUT_MS);
        if (rc < 0 && errno == EINTR) {
            /* O self-pipe acorda o loop; o poll nao reinicia sozinho. */
            rc = 0;
        }

        if (rc < 0) {
            GLOG_ERR("poll: %s", strerror(errno));
            status = 1;
            break;
        }

        giochi_supervisor_reap(&ctx.supervisor);

        if (rc == 0) {
            continue;
        }

        if ((pfds[0].revents & POLLIN) != 0) {
            drain_wake_pipe(&ctx);
        }

        if (control_index >= 0 &&
            (pfds[control_index].revents & POLLIN) != 0) {
            if (giochi_control_accept(&ctx.control, handle_command, &ctx) < 0) {
                GLOG_ERR("falha no control.sock; encerrando");
                status = 1;
                break;
            }
            if (once) {
                break;
            }
        }

        if (transition_index >= 0 &&
            (pfds[transition_index].revents & POLLIN) != 0) {
            handle_transition(&ctx);
        }

        if (hotkey_count > 0) {
            if (giochi_hotkey_process(&ctx.hotkey, pfds, hotkey_offset,
                                      hotkey_count, giochi_now_ms()) != 0) {
                GLOG_INFO("hotkey Home+Start: encerrando jogo");
                (void)giochi_supervisor_exit_game(&ctx.supervisor);
            }
        }
    }

done:
    GLOG_INFO("encerrando giochi-init");
    giochi_supervisor_stop_all(&ctx.supervisor);
    giochi_hotkey_close(&ctx.hotkey);
    if (ctx.transition_fd >= 0) {
        (void)close(ctx.transition_fd);
    }
    giochi_control_close(&ctx.control);
    if (ctx.wake_pipe[0] >= 0) {
        (void)close(ctx.wake_pipe[0]);
    }
    if (ctx.wake_pipe[1] >= 0) {
        (void)close(ctx.wake_pipe[1]);
    }
    g_ctx = NULL;
    return status;
}
