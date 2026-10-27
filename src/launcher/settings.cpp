#include "settings.hpp"

#include "giochi_json.h"
#include "giochi_log.h"
#include "giochi_util.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define GIOCHI_SETTINGS_FILE "settings.json"

static int clamp_int(int value, int lo, int hi)
{
    if (value < lo) {
        return lo;
    }
    return value > hi ? hi : value;
}

static double clamp_double(double value, double lo, double hi)
{
    if (value < lo) {
        return lo;
    }
    return value > hi ? hi : value;
}

void giochi_settings_defaults(giochi_settings *settings)
{
    if (settings == NULL) {
        return;
    }
    memset(settings, 0, sizeof(*settings));

    giochi_strlcpy(settings->schema_version, "1.1",
                   sizeof(settings->schema_version));
    giochi_strlcpy(settings->theme_id, "dark_default",
                   sizeof(settings->theme_id));
    giochi_strlcpy(settings->accent_color, "#E50914",
                   sizeof(settings->accent_color));
    settings->font_scale = 1.0;
    settings->ui_scale = 1.0;

    giochi_strlcpy(settings->language, "pt-BR", sizeof(settings->language));
    giochi_strlcpy(settings->timezone, "America/Sao_Paulo",
                   sizeof(settings->timezone));
    settings->brightness = 80;
    settings->suspend_timeout_s = 300;
    giochi_strlcpy(settings->usb_mode, "mtp", sizeof(settings->usb_mode));

    settings->master_volume = 70;
    settings->game_volume = 80;
    settings->music_volume = 60;
    settings->muted = 0;

    giochi_strlcpy(settings->store_region, "BR", sizeof(settings->store_region));
    giochi_strlcpy(settings->store_source, "curated",
                   sizeof(settings->store_source));

    settings->wifi_enabled = 0;
    settings->bluetooth_enabled = 0;
}

static void copy_string(char *dst, size_t dstsz, const gj_value *obj,
                        const char *key)
{
    const char *value = gj_as_string(gj_object_get(obj, key), NULL);

    if (value != NULL) {
        giochi_strlcpy(dst, value, dstsz);
    }
}

static void read_appearance(giochi_settings *s, const gj_value *section)
{
    copy_string(s->theme_id, sizeof(s->theme_id), section, "theme_id");
    copy_string(s->accent_color, sizeof(s->accent_color), section,
                "accent_color");
    s->font_scale = clamp_double(
        gj_as_number(gj_object_get(section, "font_scale"), s->font_scale),
        0.5, 2.0);
    s->ui_scale = clamp_double(
        gj_as_number(gj_object_get(section, "ui_scale"), s->ui_scale),
        0.5, 2.0);
}

static void read_system(giochi_settings *s, const gj_value *section)
{
    copy_string(s->language, sizeof(s->language), section, "language");
    copy_string(s->timezone, sizeof(s->timezone), section, "timezone");
    copy_string(s->usb_mode, sizeof(s->usb_mode), section, "usb_mode");
    s->brightness = clamp_int(
        gj_as_int(gj_object_get(section, "brightness"), s->brightness), 0, 100);
    s->suspend_timeout_s = clamp_int(
        gj_as_int(gj_object_get(section, "suspend_timeout_s"),
                  s->suspend_timeout_s), 0, 86400);
    if (strcmp(s->usb_mode, "mtp") != 0 && strcmp(s->usb_mode, "mass") != 0) {
        giochi_strlcpy(s->usb_mode, "mtp", sizeof(s->usb_mode));
    }
}

static void read_sound(giochi_settings *s, const gj_value *section)
{
    s->master_volume = clamp_int(
        gj_as_int(gj_object_get(section, "master_volume"), s->master_volume),
        0, 100);
    s->game_volume = clamp_int(
        gj_as_int(gj_object_get(section, "game_volume"), s->game_volume),
        0, 100);
    s->music_volume = clamp_int(
        gj_as_int(gj_object_get(section, "music_volume"), s->music_volume),
        0, 100);
    s->muted = gj_as_bool(gj_object_get(section, "muted"), s->muted);
}

static void read_store(giochi_settings *s, const gj_value *section)
{
    copy_string(s->store_region, sizeof(s->store_region), section, "region");
    copy_string(s->store_source, sizeof(s->store_source), section, "source");
}

static void read_network(giochi_settings *s, const gj_value *section)
{
    s->wifi_enabled = gj_as_bool(gj_object_get(section, "wifi_enabled"),
                                 s->wifi_enabled);
    s->bluetooth_enabled = gj_as_bool(
        gj_object_get(section, "bluetooth_enabled"), s->bluetooth_enabled);
    copy_string(s->wifi_last_ssid, sizeof(s->wifi_last_ssid), section,
                "wifi_last_ssid");
    /* TODO(v1.2): wifi_credentials[] e bluetooth_paired[] nao sao carregados
     * nem reescritos por este esqueleto. */
}

int giochi_settings_load(giochi_settings *settings, const char *user_dir,
                         char *err, size_t errsz)
{
    char path[512];
    char parse_err[160];
    gj_value *doc;

    if (err != NULL && errsz > 0) {
        err[0] = '\0';
    }
    giochi_settings_defaults(settings);

    if (user_dir == NULL) {
        return 0;
    }
    if (snprintf(path, sizeof(path), "%s/%s", user_dir, GIOCHI_SETTINGS_FILE)
            >= (int)sizeof(path)) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "caminho de settings.json longo demais");
        }
        return -1;
    }
    if (access(path, F_OK) != 0) {
        GLOG_INFO("settings.json ausente; usando defaults");
        return 0;
    }

    doc = gj_parse_file(path, parse_err, sizeof(parse_err));
    if (doc == NULL) {
        char backup[520];

        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "%s", parse_err);
        }
        if (snprintf(backup, sizeof(backup), "%s.bak", path)
                < (int)sizeof(backup)) {
            if (rename(path, backup) == 0) {
                GLOG_WARN("settings.json corrompido; renomeado para .bak");
            }
        }
        return -1;
    }

    copy_string(settings->schema_version, sizeof(settings->schema_version),
                doc, "schema_version");

    read_appearance(settings, gj_object_get(doc, "appearance"));
    read_system(settings, gj_object_get(doc, "system"));
    read_sound(settings, gj_object_get(doc, "sound"));
    read_store(settings, gj_object_get(doc, "store"));
    read_network(settings, gj_object_get(doc, "network"));

    gj_free(doc);
    return 0;
}

static void json_escape(char *dst, size_t dstsz, const char *src)
{
    size_t out = 0;

    if (dstsz == 0) {
        return;
    }
    for (; *src != '\0' && out + 2 < dstsz; src++) {
        unsigned char c = (unsigned char)*src;

        if (c == '"' || c == '\\') {
            dst[out++] = '\\';
            dst[out++] = (char)c;
        } else if (c < 0x20U) {
            dst[out++] = '?';
        } else {
            dst[out++] = (char)c;
        }
    }
    dst[out] = '\0';
}

int giochi_settings_save(const giochi_settings *settings, const char *user_dir,
                         char *err, size_t errsz)
{
    char path[512];
    char buf[2048];
    char ssid_escaped[128];
    char ssid_json[160];
    int n;

    if (err != NULL && errsz > 0) {
        err[0] = '\0';
    }
    if (settings == NULL || user_dir == NULL) {
        return -1;
    }
    if (giochi_mkdir_p(user_dir, 0755) != 0) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "nao foi possivel criar '%s'", user_dir);
        }
        return -1;
    }
    if (snprintf(path, sizeof(path), "%s/%s", user_dir, GIOCHI_SETTINGS_FILE)
            >= (int)sizeof(path)) {
        return -1;
    }

    if (settings->wifi_last_ssid[0] != '\0') {
        json_escape(ssid_escaped, sizeof(ssid_escaped),
                    settings->wifi_last_ssid);
        (void)snprintf(ssid_json, sizeof(ssid_json), "\"%s\"", ssid_escaped);
    } else {
        giochi_strlcpy(ssid_json, "null", sizeof(ssid_json));
    }

    n = snprintf(buf, sizeof(buf),
        "{\n"
        "  \"schema_version\": \"%s\",\n"
        "  \"appearance\": { \"theme_id\": \"%s\", \"accent_color\": \"%s\", "
        "\"font_scale\": %.2f, \"ui_scale\": %.2f },\n"
        "  \"system\": { \"language\": \"%s\", \"timezone\": \"%s\", "
        "\"brightness\": %d, \"suspend_timeout_s\": %d, \"usb_mode\": \"%s\" },\n"
        "  \"sound\": { \"master_volume\": %d, \"game_volume\": %d, "
        "\"music_volume\": %d, \"muted\": %s },\n"
        "  \"store\": { \"region\": \"%s\", \"source\": \"%s\" },\n"
        "  \"network\": {\n"
        "    \"wifi_enabled\": %s,\n"
        "    \"wifi_last_ssid\": %s,\n"
        "    \"wifi_credentials\": [],\n"
        "    \"bluetooth_enabled\": %s,\n"
        "    \"bluetooth_paired\": []\n"
        "  }\n"
        "}\n",
        settings->schema_version, settings->theme_id, settings->accent_color,
        settings->font_scale, settings->ui_scale,
        settings->language, settings->timezone, settings->brightness,
        settings->suspend_timeout_s, settings->usb_mode,
        settings->master_volume, settings->game_volume, settings->music_volume,
        settings->muted ? "true" : "false",
        settings->store_region, settings->store_source,
        settings->wifi_enabled ? "true" : "false",
        ssid_json,
        settings->bluetooth_enabled ? "true" : "false");

    if (n < 0 || (size_t)n >= sizeof(buf)) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "settings.json excede o buffer");
        }
        return -1;
    }

    if (giochi_write_file_atomic(path, buf, strlen(buf)) != 0) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "nao foi possivel gravar '%s'", path);
        }
        return -1;
    }
    return 0;
}
