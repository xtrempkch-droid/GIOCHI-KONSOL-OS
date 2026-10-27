#include "theme.hpp"

#include "giochi_json.h"
#include "giochi_util.h"

#include <stdio.h>
#include <string.h>

static void copy_hex(char *dst, size_t dstsz, const gj_value *obj,
                     const char *key)
{
    const char *value = gj_as_string(gj_object_get(obj, key), NULL);

    if (value != NULL) {
        giochi_strlcpy(dst, value, dstsz);
    }
}

void giochi_theme_defaults(giochi_theme *theme)
{
    if (theme == NULL) {
        return;
    }
    memset(theme, 0, sizeof(*theme));
    giochi_strlcpy(theme->id, "dark_default", sizeof(theme->id));
    giochi_strlcpy(theme->background, "#0F0F0F", sizeof(theme->background));
    giochi_strlcpy(theme->surface, "#1E1E1E", sizeof(theme->surface));
    giochi_strlcpy(theme->surface_elevated, "#2A2A2A",
                   sizeof(theme->surface_elevated));
    giochi_strlcpy(theme->text_primary, "#E5E5E5",
                   sizeof(theme->text_primary));
    giochi_strlcpy(theme->text_secondary, "#A0A0A0",
                   sizeof(theme->text_secondary));
    giochi_strlcpy(theme->hint_bar_bg, "#181818",
                   sizeof(theme->hint_bar_bg));
    giochi_strlcpy(theme->divider, "#333333", sizeof(theme->divider));
    giochi_strlcpy(theme->accent, "#E50914", sizeof(theme->accent));
}

unsigned giochi_theme_rgb(const char *hex, unsigned fallback)
{
    unsigned value = 0;
    int i;

    if (hex == NULL || hex[0] != '#') {
        return fallback;
    }
    for (i = 1; i <= 6; i++) {
        int c = (unsigned char)hex[i];
        int digit;

        if (c == '\0') {
            return fallback;
        }
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        } else {
            return fallback;
        }
        value = (value << 4) | (unsigned)digit;
    }
    return value;
}

int giochi_theme_load(giochi_theme *theme, const char *dir, const char *file,
                      char *err, size_t errsz)
{
    char path[512];
    char parse_err[160];
    gj_value *doc;
    const gj_value *colors;

    if (err != NULL && errsz > 0) {
        err[0] = '\0';
    }
    giochi_theme_defaults(theme);

    if (dir == NULL || file == NULL) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "tema: diretorio/arquivo invalido");
        }
        return -1;
    }
    if (snprintf(path, sizeof(path), "%s/%s", dir, file) >= (int)sizeof(path)) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "tema: caminho longo demais");
        }
        return -1;
    }

    doc = gj_parse_file(path, parse_err, sizeof(parse_err));
    if (doc == NULL) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "%s", parse_err);
        }
        return -1;
    }

    giochi_strlcpy(theme->id, gj_as_string(gj_object_get(doc, "theme_id"),
                                           theme->id),
                   sizeof(theme->id));
    giochi_strlcpy(theme->id, gj_as_string(gj_object_get(doc, "id"), theme->id),
                   sizeof(theme->id));

    colors = gj_object_get(doc, "colors");
    if (colors != NULL) {
        copy_hex(theme->background, sizeof(theme->background), colors,
                 "background");
        copy_hex(theme->surface, sizeof(theme->surface), colors, "surface");
        copy_hex(theme->surface_elevated, sizeof(theme->surface_elevated),
                 colors, "surface_elevated");
        copy_hex(theme->text_primary, sizeof(theme->text_primary), colors,
                 "text_primary");
        copy_hex(theme->text_secondary, sizeof(theme->text_secondary), colors,
                 "text_secondary");
        copy_hex(theme->hint_bar_bg, sizeof(theme->hint_bar_bg), colors,
                 "hint_bar_bg");
        copy_hex(theme->divider, sizeof(theme->divider), colors, "divider");
        copy_hex(theme->accent, sizeof(theme->accent), colors, "accent");
    }

    gj_free(doc);
    return 0;
}
