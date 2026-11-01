/* Giochi Konsol OS v1.2 — /user/settings.json (secao 4.9.5). */
#ifndef GIOCHI_SETTINGS_H
#define GIOCHI_SETTINGS_H

#include <stddef.h>

typedef struct {
    char schema_version[8];
    char theme_id[64];
    char accent_color[16];
    double font_scale;
    double ui_scale;

    char language[16];
    char timezone[64];
    int brightness;
    int suspend_timeout_s;
    char usb_mode[8];

    int master_volume;
    int game_volume;
    int music_volume;
    int muted;

    char store_region[8];
    char store_source[16];

    int wifi_enabled;
    char wifi_last_ssid[64];
    int bluetooth_enabled;

    /* Adendo 4.6.1: SSH via USB (Modo Desenvolvedor). Desativado por padrao;
     * com 0 nao ha IP nem dropbear (0 MB em repouso). */
    int developer_ssh_usb_enabled;
} giochi_settings;

void giochi_settings_defaults(giochi_settings *settings);

/* 0 = ok (arquivo ausente => defaults); -1 = corrompido (renomeado .bak). */
int giochi_settings_load(giochi_settings *settings, const char *user_dir,
                         char *err, size_t errsz);

int giochi_settings_save(const giochi_settings *settings, const char *user_dir,
                         char *err, size_t errsz);

#endif /* GIOCHI_SETTINGS_H */
