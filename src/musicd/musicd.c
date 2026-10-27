#include "musicd.h"

#include "giochi_log.h"
#include "giochi_util.h"

#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#if defined(GIOCHI_HAVE_MPG123) || defined(GIOCHI_HAVE_FLAC)
#  define GIOCHI_DECODER_AVAILABLE 1
#else
#  define GIOCHI_DECODER_AVAILABLE 0
#endif

int giochi_music_decoder_available(void)
{
    return GIOCHI_DECODER_AVAILABLE;
}

void giochi_music_player_init(giochi_player *p)
{
    if (p == NULL) {
        return;
    }
    memset(p, 0, sizeof(*p));
    p->decoder_ready = GIOCHI_DECODER_AVAILABLE;
}

void giochi_music_player_free(giochi_player *p)
{
    if (p == NULL) {
        return;
    }
    free(p->tracks);
    p->tracks = NULL;
    p->count = 0;
    p->capacity = 0;
    p->current = 0;
}

static int has_music_extension(const char *name)
{
    const char *dot = strrchr(name, '.');

    if (dot == NULL) {
        return 0;
    }
    return strcasecmp(dot, ".mp3") == 0 ||
           strcasecmp(dot, ".flac") == 0 ||
           strcasecmp(dot, ".ogg") == 0;
}

static void title_from_filename(char *dst, size_t dstsz, const char *filename)
{
    size_t i;

    giochi_strlcpy(dst, filename, dstsz);
    for (i = 0; dst[i] != '\0'; i++) {
        if (dst[i] == '_') {
            dst[i] = ' ';
        }
    }
    for (i = strlen(dst); i > 0; i--) {
        if (dst[i - 1] == '.') {
            dst[i - 1] = '\0';
            break;
        }
    }
}

static int track_compare(const void *lhs, const void *rhs)
{
    const giochi_track *a = lhs;
    const giochi_track *b = rhs;

    return strcmp(a->path, b->path);
}

int giochi_music_scan(giochi_player *p, const char *dir)
{
    DIR *dp;
    struct dirent *entry;

    if (p == NULL || dir == NULL) {
        return -1;
    }

    free(p->tracks);
    p->tracks = NULL;
    p->count = 0;
    p->capacity = 0;
    p->current = 0;
    p->pos_s = 0;
    p->playing = 0;

    dp = opendir(dir);
    if (dp == NULL) {
        GLOG_WARN("musica: diretorio '%s' inacessivel", dir);
        return -1;
    }

    while ((entry = readdir(dp)) != NULL) {
        char path[GIOCHI_TRACK_PATH_MAX];
        struct stat st;
        giochi_track *track;

        if (entry->d_name[0] == '.' || !has_music_extension(entry->d_name)) {
            continue;
        }
        if (snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name)
                >= (int)sizeof(path)) {
            GLOG_WARN("musica: caminho longo demais, ignorado");
            continue;
        }
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }

        if (p->count == p->capacity) {
            size_t newcap = p->capacity == 0 ? 32 : p->capacity * 2;
            giochi_track *grown = realloc(p->tracks, newcap * sizeof(*grown));

            if (grown == NULL) {
                (void)closedir(dp);
                GLOG_ERR("musica: sem memoria para a fila");
                return -1;
            }
            p->tracks = grown;
            p->capacity = newcap;
        }

        track = &p->tracks[p->count++];
        memset(track, 0, sizeof(*track));
        giochi_strlcpy(track->path, path, sizeof(track->path));
        title_from_filename(track->title, sizeof(track->title), entry->d_name);
        giochi_strlcpy(track->artist, "Desconhecido", sizeof(track->artist));
        giochi_strlcpy(track->album, "Desconhecido", sizeof(track->album));
        /* TODO(v1.2): duracao/metadados via libmpg123/libFLAC (APIC nao usado). */
    }
    (void)closedir(dp);

    if (p->count > 1) {
        qsort(p->tracks, p->count, sizeof(*p->tracks), track_compare);
    }

    GLOG_INFO("musica: %zu faixas em %s", p->count, dir);
    return 0;
}

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    int overflow;
} appender;

static void app_init(appender *a, char *buf, size_t cap)
{
    a->buf = buf;
    a->cap = cap;
    a->len = 0;
    a->overflow = 0;
    if (cap > 0) {
        buf[0] = '\0';
    }
}

static void app_raw(appender *a, const char *text)
{
    size_t n = strlen(text);
    size_t room;

    if (a->len + 1 >= a->cap) {
        a->overflow = 1;
        return;
    }
    room = a->cap - a->len - 1;
    if (n > room) {
        n = room;
        a->overflow = 1;
    }
    memcpy(a->buf + a->len, text, n);
    a->len += n;
    a->buf[a->len] = '\0';
}

static void app_fmt(appender *a, const char *fmt, ...)
{
    char tmp[512];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    app_raw(a, tmp);
}

static void app_json_string(appender *a, const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    char esc[8];

    app_raw(a, "\"");
    for (; *p != '\0'; p++) {
        switch (*p) {
        case '"':  app_raw(a, "\\\""); break;
        case '\\': app_raw(a, "\\\\"); break;
        case '\n': app_raw(a, "\\n");  break;
        case '\r': app_raw(a, "\\r");  break;
        case '\t': app_raw(a, "\\t");  break;
        default:
            if (*p < 0x20U) {
                (void)snprintf(esc, sizeof(esc), "\\u%04x", (unsigned)*p);
                app_raw(a, esc);
            } else {
                esc[0] = (char)*p;
                esc[1] = '\0';
                app_raw(a, esc);
            }
            break;
        }
    }
    app_raw(a, "\"");
}

static const char *status_name(const giochi_player *p)
{
    if (!p->decoder_ready) {
        return "unavailable";
    }
    if (p->playing) {
        return "playing";
    }
    return p->count > 0 ? "paused" : "stopped";
}

static const char *current_title(const giochi_player *p)
{
    if (p->count == 0 || p->current >= p->count) {
        return "";
    }
    return p->tracks[p->current].title;
}

static unsigned current_duration(const giochi_player *p)
{
    if (p->count == 0 || p->current >= p->count) {
        return 0;
    }
    return p->tracks[p->current].duration_s;
}

static void app_playing(appender *a, const giochi_player *p, const char *status)
{
    app_raw(a, "{\"status\":");
    app_json_string(a, status);
    app_raw(a, ",\"track\":");
    app_json_string(a, current_title(p));
    app_fmt(a, ",\"pos\":%u}", p->pos_s);
}

static void app_queue(appender *a, const giochi_player *p)
{
    size_t i;

    app_raw(a, "{\"queue\":[");
    for (i = 0; i < p->count; i++) {
        if (i > 0) {
            app_raw(a, ",");
        }
        app_raw(a, "{\"title\":");
        app_json_string(a, p->tracks[i].title);
        app_raw(a, ",\"artist\":");
        app_json_string(a, p->tracks[i].artist);
        app_fmt(a, ",\"dur\":%u}", p->tracks[i].duration_s);
    }
    app_raw(a, "]}");
}

static void app_status(appender *a, const giochi_player *p)
{
    size_t i;

    app_raw(a, "{\"status\":");
    app_json_string(a, status_name(p));
    app_raw(a, ",\"track\":");
    app_json_string(a, current_title(p));
    app_fmt(a, ",\"pos\":%u,\"dur\":%u,\"decoder_available\":%s,\"queue\":[",
            p->pos_s, current_duration(p),
            p->decoder_ready ? "true" : "false");
    for (i = 0; i < p->count; i++) {
        if (i > 0) {
            app_raw(a, ",");
        }
        app_raw(a, "{\"title\":");
        app_json_string(a, p->tracks[i].title);
        app_raw(a, ",\"artist\":");
        app_json_string(a, p->tracks[i].artist);
        app_fmt(a, ",\"dur\":%u}", p->tracks[i].duration_s);
    }
    app_raw(a, "]}");
}

static int parse_seek(const char *arg, unsigned *out)
{
    char *end = NULL;
    unsigned long value;

    if (arg == NULL || !isdigit((unsigned char)*arg)) {
        return -1;
    }
    value = strtoul(arg, &end, 10);
    if (end == NULL) {
        return -1;
    }
    while (*end == ' ') {
        end++;
    }
    if (*end != '\0' || value > 86400UL) {
        return -1;
    }
    *out = (unsigned)value;
    return 0;
}

int giochi_music_command(giochi_player *p, const char *cmd, char *out,
                         size_t outsz)
{
    appender a;
    char line[256];
    char *verb;
    char *arg;
    int rc = GIOCHI_MUSIC_OK;

    if (p == NULL || cmd == NULL || out == NULL || outsz == 0) {
        return GIOCHI_MUSIC_ERR_INTERNAL;
    }

    app_init(&a, out, outsz);
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
    giochi_log(GIOCHI_LOG_DEBUG, "music.sock <- '%s'", cmd);

    if (strcasecmp(verb, "PLAY") == 0) {
        if (p->count == 0) {
            app_raw(&a, "{\"status\":\"empty\"}");
            rc = GIOCHI_MUSIC_ERR_EMPTY;
        } else if (!p->decoder_ready) {
            app_raw(&a, "{\"status\":\"error\",\"reason\":\"decoder_unavailable\"}");
            rc = GIOCHI_MUSIC_ERR_DECODER;
        } else {
            p->playing = 1;
            app_playing(&a, p, "playing");
        }
    } else if (strcasecmp(verb, "PAUSE") == 0) {
        if (!p->decoder_ready) {
            app_raw(&a, "{\"status\":\"error\",\"reason\":\"decoder_unavailable\"}");
            rc = GIOCHI_MUSIC_ERR_DECODER;
        } else {
            p->playing = 0;
            app_fmt(&a, "{\"status\":\"paused\",\"pos\":%u}", p->pos_s);
        }
    } else if (strcasecmp(verb, "NEXT") == 0 ||
               strcasecmp(verb, "PREV") == 0) {
        if (p->count == 0) {
            app_raw(&a, "{\"status\":\"empty\"}");
            rc = GIOCHI_MUSIC_ERR_EMPTY;
        } else if (!p->decoder_ready) {
            app_raw(&a, "{\"status\":\"error\",\"reason\":\"decoder_unavailable\"}");
            rc = GIOCHI_MUSIC_ERR_DECODER;
        } else {
            if (strcasecmp(verb, "NEXT") == 0) {
                p->current = (p->current + 1) % p->count;
            } else {
                p->current = (p->current + p->count - 1) % p->count;
            }
            p->pos_s = 0;
            p->playing = 1;
            app_playing(&a, p, "playing");
        }
    } else if (strcasecmp(verb, "SEEK") == 0) {
        unsigned pos = 0;

        if (parse_seek(arg, &pos) != 0) {
            app_raw(&a, "{\"status\":\"error\",\"reason\":\"invalid_position\"}");
            rc = GIOCHI_MUSIC_ERR_COMMAND;
        } else if (!p->decoder_ready) {
            app_raw(&a, "{\"status\":\"error\",\"reason\":\"decoder_unavailable\"}");
            rc = GIOCHI_MUSIC_ERR_DECODER;
        } else {
            unsigned dur = current_duration(p);

            if (dur > 0 && pos > dur) {
                pos = dur;
            }
            p->pos_s = pos;
            app_playing(&a, p, p->playing ? "playing" : "paused");
        }
    } else if (strcasecmp(verb, "STATUS") == 0) {
        app_status(&a, p);
    } else if (strcasecmp(verb, "QUEUE") == 0) {
        app_queue(&a, p);
    } else {
        app_raw(&a, "{\"status\":\"error\",\"reason\":\"unknown_command\"}");
        rc = GIOCHI_MUSIC_ERR_COMMAND;
    }

    if (a.overflow) {
        GLOG_WARN("musica: resposta truncada (%zu bytes)", outsz);
    }
    return rc;
}
