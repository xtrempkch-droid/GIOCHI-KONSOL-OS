/* Giochi Konsol OS v1.2 — theme.json (secao 4.1.8). */
#ifndef GIOCHI_THEME_H
#define GIOCHI_THEME_H

#include <stddef.h>

typedef struct {
    char id[64];
    char background[16];
    char surface[16];
    char surface_elevated[16];
    char text_primary[16];
    char text_secondary[16];
    char hint_bar_bg[16];
    char divider[16];
    char accent[16];
} giochi_theme;

void giochi_theme_defaults(giochi_theme *theme);

/* 0 = ok; -1 = falha (tema recebe o default compilado). */
int giochi_theme_load(giochi_theme *theme, const char *dir, const char *file,
                      char *err, size_t errsz);

/* "#RRGGBB" -> 0xRRGGBB. */
unsigned giochi_theme_rgb(const char *hex, unsigned fallback);

#endif /* GIOCHI_THEME_H */
