/* Giochi Konsol OS v1.2 — layout.json (schema 1.6) e abas (secao 4.1). */
#ifndef GIOCHI_LAYOUT_H
#define GIOCHI_LAYOUT_H

#include <stddef.h>

#define GIOCHI_MAX_TABS 8
#define GIOCHI_TAB_ID_MAX 32
#define GIOCHI_TAB_TARGET_MAX 256

typedef struct {
    char id[GIOCHI_TAB_ID_MAX];
    char target[GIOCHI_TAB_TARGET_MAX];
    int is_app;
} giochi_tab;

typedef struct {
    char schema_version[16];
    char theme_id[64];
    int width;
    int height;
    char orientation[16];
    giochi_tab tabs[GIOCHI_MAX_TABS];
    int tab_count;
    int default_index;
    int center_index;
    char switch_transition[32];
    int home_short_press_ms;
    int exit_hold_ms;
} giochi_layout;

void giochi_layout_defaults(giochi_layout *layout);

/* 0 = carregado; -1 = falha (layout recebe o default compilado).
 * Ajustes de fallback (order/visual_center/default invalidos) sao aplicados
 * e reportados em err, mas nao falham o carregamento. */
int giochi_layout_load(giochi_layout *layout, const char *dir,
                       const char *file, char *err, size_t errsz);

/* 0 = valido, 1 = ajustado, -1 = inutilizavel. */
int giochi_layout_validate(giochi_layout *layout, char *err, size_t errsz);

int giochi_layout_index_of(const giochi_layout *layout, const char *id);
const char *giochi_layout_tab_id(const giochi_layout *layout, int index);
int giochi_layout_next(const giochi_layout *layout, int index);
int giochi_layout_prev(const giochi_layout *layout, int index);

#endif /* GIOCHI_LAYOUT_H */
