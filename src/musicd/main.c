/* Giochi Konsol OS v1.2 — giochi-musicd (secao 4.8).
 *
 * Daemon persistente: varre /user/music/, responde comandos JSON em
 * /run/giochi/music.sock. Sobrevive ao lancamento de jogos (ADR-10); o
 * supervisor so o reinicia se ele morrer.
 */
#include "audio.h"
#include "musicd.h"

#include "giochi_log.h"
#include "giochi_sock.h"
#include "giochi_util.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define GIOCHI_MUSICD_VERSION "1.2"
#define GIOCHI_MUSIC_MAX_CMD 256

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void install_signal_handlers(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    (void)sigaction(SIGTERM, &sa, NULL);
    (void)sigaction(SIGINT, &sa, NULL);
}

typedef struct {
    const char *music_dir;
    const char *sock_path;
    int once;
    int no_audio;
} musicd_options;

static void usage(FILE *out)
{
    (void)fprintf(out,
        "uso: giochi-musicd [opcoes]\n"
        "  --music-dir DIR   pasta de musicas (default: /user/music)\n"
        "  --socket PATH     socket de controle (default: /run/giochi/music.sock)\n"
        "  --once            atende um comando e sai\n"
        "  --no-audio        nao abre o dispositivo ALSA\n"
        "  --version         imprime a versao\n"
        "  --help            esta ajuda\n");
}

static int handle_client(int client, giochi_player *player)
{
    char cmd[GIOCHI_MUSIC_MAX_CMD];
    char reply[8192];
    int len;
    int rc;

    len = giochi_sock_read_line(client, cmd, sizeof(cmd), 1000);
    if (len <= 0) {
        return 0;
    }

    rc = giochi_music_command(player, cmd, reply, sizeof(reply));
    (void)giochi_sock_write_all(client, reply, strlen(reply));
    (void)giochi_sock_write_all(client, "\n", 1);
    return rc;
}

int main(int argc, char **argv)
{
    musicd_options opt;
    giochi_player player;
    char default_dir[512];
    char default_sock[512];
    int listen_fd = -1;
    int status = 0;

    giochi_log_set_level(giochi_log_level_from_env());
    memset(&opt, 0, sizeof(opt));

    if (giochi_path(default_dir, sizeof(default_dir), "/user/music") != 0 ||
        giochi_path(default_sock, sizeof(default_sock),
                    "/run/giochi/music.sock") != 0) {
        GLOG_ERR("caminhos padrao longos demais");
        return 1;
    }
    opt.music_dir = default_dir;
    opt.sock_path = default_sock;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0) {
            (void)printf("giochi-musicd %s\n", GIOCHI_MUSICD_VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--once") == 0) {
            opt.once = 1;
        } else if (strcmp(argv[i], "--no-audio") == 0) {
            opt.no_audio = 1;
        } else if (strcmp(argv[i], "--music-dir") == 0 && i + 1 < argc) {
            opt.music_dir = argv[++i];
        } else if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            opt.sock_path = argv[++i];
        } else {
            (void)fprintf(stderr, "giochi-musicd: opcao desconhecida '%s'\n",
                          argv[i]);
            usage(stderr);
            return 2;
        }
    }

    if (getenv("GIOCHI_MUSIC_SOCK") != NULL) {
        opt.sock_path = getenv("GIOCHI_MUSIC_SOCK");
    }

    install_signal_handlers();
    giochi_music_player_init(&player);
    (void)giochi_music_scan(&player, opt.music_dir);

    if (!opt.no_audio && giochi_music_decoder_available()) {
        if (giochi_audio_init() != 0) {
            GLOG_WARN("audio indisponivel; daemon segue sem reproducao");
        }
    } else if (!giochi_music_decoder_available()) {
        GLOG_WARN("sem libmpg123/libFLAC: UI responde, audio desativado");
    }

    {
        char err[128];

        listen_fd = giochi_sock_listen_unix(opt.sock_path, 4, err, sizeof(err));
        if (listen_fd < 0) {
            GLOG_ERR("music.sock (%s): %s", opt.sock_path, err);
            giochi_music_player_free(&player);
            return 1;
        }
    }
    GLOG_INFO("giochi-musicd v%s: %zu faixas, socket %s",
              GIOCHI_MUSICD_VERSION, player.count, opt.sock_path);

    while (!g_stop) {
        struct pollfd pfd;
        int rc;

        pfd.fd = listen_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        rc = poll(&pfd, 1, 1000);
        if (rc < 0 && errno == EINTR) {
            continue;
        }

        if (rc < 0) {
            GLOG_ERR("poll(music.sock): %s", strerror(errno));
            status = 1;
            break;
        }
        if (rc == 0) {
            /* TODO(v1.2): avancar posicao/decoder e reenviar PCM ao ALSA. */
            continue;
        }

        {
            int client = accept(listen_fd, NULL, NULL);

            if (client < 0) {
                if (errno == EINTR) {
                    continue;
                }
                GLOG_ERR("accept(music.sock): %s", strerror(errno));
                status = 1;
                break;
            }
            (void)handle_client(client, &player);
            (void)close(client);
        }

        if (opt.once) {
            break;
        }
    }

    (void)close(listen_fd);
    (void)unlink(opt.sock_path);
    giochi_audio_shutdown();
    giochi_music_player_free(&player);
    GLOG_INFO("giochi-musicd encerrado");
    return status;
}
