#include "layout.hpp"

#include "giochi_json.h"
#include "giochi_log.h"
#include "giochi_util.h"

#include <stdio.h>
#include <string.h>

#define GIOCHI_LAYOUT_DEFAULT_FILE "layout.json"

static void set_tab(giochi_tab *tab, const char *id, const char *target)
{
    giochi_strlcpy(tab->id, id, sizeof(tab->id));
    giochi_strlcpy(tab->target, target, sizeof(tab->target));
    tab->is_app = strncmp(target, "APP:", 4) == 0;
}

void giochi_layout_defaults(giochi_layout *layout)
{
    if (layout == NULL) {
        return;
    }
    memset(layout, 0, sizeof(*layout));

    giochi_strlcpy(layout->schema_version, "1.6",
                   sizeof(layout->schema_version));
    giochi_strlcpy(layout->theme_id, "dark_default", sizeof(layout->theme_id));
    layout->width = 640;
    layout->height = 480;
    giochi_strlcpy(layout->orientation, "landscape",
                   sizeof(layout->orientation));
    giochi_strlcpy(layout->switch_transition, "fade_150ms",
                   sizeof(layout->switch_transition));
    layout->home_short_press_ms = 500;
    layout->exit_hold_ms = 500;

    /* Ordem visual da secao 4.1.2 — Home no centro. */
    set_tab(&layout->tabs[0], "music", "music.json");
    set_tab(&layout->tabs[1], "library", "library.json");
    set_tab(&layout->tabs[2], "home", "home.json");
    set_tab(&layout->tabs[3], "store", "APP:giochi-store");
    set_tab(&layout->tabs[4], "ports", "APP:portmaster");
    layout->tab_count = 5;
    layout->default_index = 2;
    layout->center_index = 2;
}

int giochi_layout_index_of(const giochi_layout *layout, const char *id)
{
    int i;

    if (layout == NULL || id == NULL) {
        return -1;
    }
    for (i = 0; i < layout->tab_count; i++) {
        if (strcmp(layout->tabs[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

const char *giochi_layout_tab_id(const giochi_layout *layout, int index)
{
    if (layout == NULL || index < 0 || index >= layout->tab_count) {
        return "";
    }
    return layout->tabs[index].id;
}

int giochi_layout_next(const giochi_layout *layout, int index)
{
    if (layout == NULL || layout->tab_count == 0) {
        return 0;
    }
    if (index < 0 || index >= layout->tab_count) {
        return 0;
    }
    return (index + 1) % layout->tab_count;
}

int giochi_layout_prev(const giochi_layout *layout, int index)
{
    if (layout == NULL || layout->tab_count == 0) {
        return 0;
    }
    if (index < 0 || index >= layout->tab_count) {
        return 0;
    }
    return (index + layout->tab_count - 1) % layout->tab_count;
}

int giochi_layout_validate(giochi_layout *layout, char *err, size_t errsz)
{
    int adjusted = 0;
    int home;

    if (err != NULL && errsz > 0) {
        err[0] = '\0';
    }
    if (layout == NULL || layout->tab_count <= 0) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "layout sem abas");
        }
        return -1;
    }

    home = giochi_layout_index_of(layout, "home");

    if (layout->default_index < 0 || layout->default_index >= layout->tab_count) {
        layout->default_index = home >= 0 ? home : layout->tab_count / 2;
        adjusted = 1;
    }
    if (layout->center_index < 0 || layout->center_index >= layout->tab_count) {
        layout->center_index = home >= 0 ? home : layout->tab_count / 2;
        adjusted = 1;
    }
    if (layout->home_short_press_ms <= 0) {
        layout->home_short_press_ms = 500;
        adjusted = 1;
    }
    if (layout->exit_hold_ms <= 0) {
        layout->exit_hold_ms = 500;
        adjusted = 1;
    }

    if (adjusted && err != NULL && errsz > 0) {
        (void)snprintf(err, errsz,
                       "layout ajustado: default=%d center=%d",
                       layout->default_index, layout->center_index);
    }
    return adjusted ? 1 : 0;
}

static int append_tab(giochi_layout *layout, const char *id,
                      const char *target)
{
    if (layout->tab_count >= GIOCHI_MAX_TABS) {
        return -1;
    }
    set_tab(&layout->tabs[layout->tab_count], id, target);
    layout->tab_count++;
    return 0;
}

int giochi_layout_load(giochi_layout *layout, const char *dir,
                       const char *file, char *err, size_t errsz)
{
    char path[512];
    char parse_err[160];
    gj_value *doc;
    const gj_value *tabs;
    const gj_value *order;
    const gj_value *layouts;
    const gj_value *value;
    size_t order_len;
    size_t i;
    int rc;

    if (err != NULL && errsz > 0) {
        err[0] = '\0';
    }
    giochi_layout_defaults(layout);

    if (dir == NULL || file == NULL) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "diretorio/arquivo de layout invalido");
        }
        return -1;
    }
    if (snprintf(path, sizeof(path), "%s/%s", dir, file) >= (int)sizeof(path)) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "caminho de layout longo demais");
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

    value = gj_object_get(doc, "schema_version");
    if (value != NULL) {
        giochi_strlcpy(layout->schema_version,
                       gj_as_string(value, layout->schema_version),
                       sizeof(layout->schema_version));
    }
    value = gj_object_get(doc, "theme_id");
    if (value != NULL) {
        giochi_strlcpy(layout->theme_id,
                       gj_as_string(value, layout->theme_id),
                       sizeof(layout->theme_id));
    }
    value = gj_object_get(doc, "screen");
    if (value != NULL) {
        layout->width = gj_as_int(gj_object_get(value, "width"), layout->width);
        layout->height = gj_as_int(gj_object_get(value, "height"), layout->height);
        giochi_strlcpy(layout->orientation,
                       gj_as_string(gj_object_get(value, "orientation"),
                                    layout->orientation),
                       sizeof(layout->orientation));
    }

    tabs = gj_object_get(doc, "tabs");
    order = tabs != NULL ? gj_object_get(tabs, "order") : NULL;
    layouts = tabs != NULL ? gj_object_get(tabs, "layouts") : NULL;

    if (order == NULL || layouts == NULL || gj_array_len(order) == 0) {
        gj_free(doc);
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz,
                           "layout sem 'tabs.order' ou 'tabs.layouts' validos");
        }
        return -1;
    }

    layout->tab_count = 0;
    order_len = gj_array_len(order);
    for (i = 0; i < order_len && layout->tab_count < GIOCHI_MAX_TABS; i++) {
        const gj_value *id_value = gj_array_at(order, i);
        const char *id = gj_as_string(id_value, NULL);
        const char *target;

        if (id == NULL || *id == '\0') {
            continue;
        }
        target = gj_as_string(gj_object_get(layouts, id), NULL);
        if (target == NULL || *target == '\0') {
            GLOG_WARN("layout: aba '%s' sem layout, ignorada", id);
            continue;
        }
        (void)append_tab(layout, id, target);
    }

    value = gj_object_get(tabs, "default");
    layout->default_index = giochi_layout_index_of(
        layout, gj_as_string(value, "home"));
    value = gj_object_get(tabs, "visual_center");
    layout->center_index = giochi_layout_index_of(
        layout, gj_as_string(value, "home"));

    value = gj_object_get(tabs, "switch_transition");
    if (value != NULL) {
        giochi_strlcpy(layout->switch_transition,
                       gj_as_string(value, layout->switch_transition),
                       sizeof(layout->switch_transition));
    }

    value = gj_object_get(doc, "buttons");
    if (value != NULL) {
        layout->home_short_press_ms = gj_as_int(
            gj_object_get(value, "home_short_press_ms"),
            layout->home_short_press_ms);
        layout->exit_hold_ms = gj_as_int(
            gj_object_get(value, "exit_game_combo_hold_ms"),
            layout->exit_hold_ms);
    }

    gj_free(doc);

    rc = giochi_layout_validate(layout, err, errsz);
    if (rc < 0) {
        giochi_layout_defaults(layout);
        return -1;
    }
    if (rc > 0) {
        GLOG_WARN("layout: fallback aplicado (%s)",
                  (err != NULL && err[0] != '\0') ? err : "ajustes");
    } else if (err != NULL && errsz > 0) {
        err[0] = '\0';
    }
    return 0;
}
