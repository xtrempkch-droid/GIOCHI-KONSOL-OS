#include "theme.hpp"

#include "giochi_json.h"
#include "giochi_util.h"

#include <stdio.h>
#include <string.h>

/* Definidos mais abaixo; usados por giochi_theme_load. */
static int giochi_density_parse(const char *nome);
static int giochi_hints_parse(const char *nome);
static const char *themes_file(const char *id);
static int theme_display_name(char *out, size_t outsz, const char *dir,
                              const char *id);

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

    /* Metricas iguais as dos tokens compilados: um tema sem `metrics` desenha
     * exatamente o que desenhava antes desta mudanca. */
    theme->corner_px = 4;
    theme->border_px = 0;
    theme->focus_ring_px = 2;
    theme->topbar_h_px = 40;
    theme->hint_bar_h_px = 30;
    theme->density = GIOCHI_DENSITY_NORMAL;
    theme->hints = GIOCHI_HINTS_SO_GAMEPAD;
    theme->cover_pct = 100;
    theme->title_scale_pct = 100;
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
    const gj_value *metrics;

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

    metrics = gj_object_get(doc, "metrics");
    if (metrics != NULL) {
        theme->corner_px = gj_as_int(
            gj_object_get(metrics, "tile_corner_radius"), theme->corner_px);
        theme->border_px =
            gj_as_int(gj_object_get(metrics, "tile_border"), theme->border_px);
        theme->focus_ring_px = gj_as_int(
            gj_object_get(metrics, "focus_ring"), theme->focus_ring_px);
        theme->topbar_h_px = gj_as_int(
            gj_object_get(metrics, "topbar_height"), theme->topbar_h_px);
        theme->hint_bar_h_px = gj_as_int(
            gj_object_get(metrics, "hint_bar_height"), theme->hint_bar_h_px);
        theme->cover_pct =
            gj_as_int(gj_object_get(metrics, "cover_pct"), theme->cover_pct);
        theme->title_scale_pct = gj_as_int(
            gj_object_get(metrics, "title_scale_pct"), theme->title_scale_pct);

        const char *dens = gj_as_string(gj_object_get(metrics, "density"), NULL);

        if (dens != NULL) {
            theme->density = giochi_density_parse(dens);
        }
    }

    const gj_value *features = gj_object_get(doc, "features");

    if (features != NULL) {
        const char *hints =
            gj_as_string(gj_object_get(features, "hints"), NULL);

        if (hints != NULL) {
            theme->hints = giochi_hints_parse(hints);
        }
    }

    gj_free(doc);
    return 0;
}

/* --- Catalogo e resolucao ------------------------------------------------
 *
 * O catalogo e' dado: assets/themes/index.json. A ordem dele e' a ordem do
 * menu Sistema > Tema. Se o arquivo sumir, o Launcher ainda funciona com o
 * tema unico antigo (dir/theme.json) — recusar subir por causa de um JSON de
 * tema seria trocar "cor errada" por "sem interface".
 */
/* "themes/<id>.json" num buffer estatico: o formato so' aparece aqui. */
static const char *themes_file(const char *id)
{
    static char buf[GIOCHI_THEME_ID_LEN + 16];

    (void)snprintf(buf, sizeof(buf), "themes/%s.json", id);
    return buf;
}

/* Nome de exibicao do tema, lido do proprio arquivo (o indice guarda so' a
 * ordem). Sem nome legivel, devolve 0 e o chamador mantem o id. */
static int theme_display_name(char *out, size_t outsz, const char *dir,
                              const char *id)
{
    char path[512];
    char parse_err[160];
    gj_value *doc;
    const char *nome;

    if (out == NULL || outsz == 0) {
        return -1;
    }
    out[0] = '\0';

    if (snprintf(path, sizeof(path), "%s/themes/%s.json", dir, id) >=
        (int)sizeof(path)) {
        return -1;
    }
    doc = gj_parse_file(path, parse_err, sizeof(parse_err));
    if (doc == NULL) {
        return -1;
    }
    nome = gj_as_string(gj_object_get(doc, "name"), NULL);
    if (nome != NULL && nome[0] != '\0') {
        giochi_strlcpy(out, nome, outsz);
    }
    gj_free(doc);
    return out[0] != '\0' ? 0 : -1;
}

static int giochi_density_parse(const char *nome)
{
    if (nome == NULL) {
        return GIOCHI_DENSITY_NORMAL;
    }
    if (strcmp(nome, "compacta") == 0) {
        return GIOCHI_DENSITY_COMPACTA;
    }
    if (strcmp(nome, "espacosa") == 0 || strcmp(nome, "espaçosa") == 0) {
        return GIOCHI_DENSITY_ESPACOSA;
    }
    return GIOCHI_DENSITY_NORMAL;
}

static int giochi_hints_parse(const char *nome)
{
    if (nome == NULL) {
        return GIOCHI_HINTS_SO_GAMEPAD;
    }
    if (strcmp(nome, "completo") == 0) {
        return GIOCHI_HINTS_COMPLETO;
    }
    return GIOCHI_HINTS_SO_GAMEPAD;
}

const char *giochi_density_name(int density)
{
    switch (density) {
    case GIOCHI_DENSITY_COMPACTA:
        return "compacta";
    case GIOCHI_DENSITY_ESPACOSA:
        return "espacosa";
    default:
        return "normal";
    }
}

const char *giochi_hints_name(int mode)
{
    return mode == GIOCHI_HINTS_COMPLETO ? "completo" : "so_gamepad";
}

int giochi_theme_catalog_load(giochi_theme_catalog *cat, const char *dir,
                              char *err, size_t errsz)
{
    char path[512];
    char parse_err[160];
    gj_value *doc;
    const gj_value *list;

    if (err != NULL && errsz > 0) {
        err[0] = '\0';
    }
    if (cat == NULL) {
        return -1;
    }
    cat->count = 0;
    cat->default_id[0] = '\0';

    if (dir == NULL) {
        return -1;
    }
    if (snprintf(path, sizeof(path), "%s/themes/index.json", dir) >=
        (int)sizeof(path)) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "catalogo de temas: caminho longo");
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

    giochi_strlcpy(cat->default_id,
                   gj_as_string(gj_object_get(doc, "default"), ""),
                   sizeof(cat->default_id));

    list = gj_object_get(doc, "themes");

    const size_t n = gj_array_len(list);

    for (size_t i = 0; i < n && cat->count < GIOCHI_THEME_MAX_CATALOG; i++) {
        const gj_value *item = gj_array_at(list, i);
        const char *id = gj_as_string(gj_object_get(item, "theme_id"), NULL);

        if (id == NULL || id[0] == '\0') {
            continue;
        }
        giochi_strlcpy(cat->entry[cat->count].id, id, GIOCHI_THEME_ID_LEN);
        /* Nome do indice se houver; senao o do proprio tema; senao o id. */
        giochi_strlcpy(cat->entry[cat->count].name, id,
                       GIOCHI_THEME_NAME_LEN);
        (void)theme_display_name(cat->entry[cat->count].name,
                                 GIOCHI_THEME_NAME_LEN, dir, id);
        cat->count++;
    }

    gj_free(doc);
    return cat->count > 0 ? 0 : -1;
}

int giochi_theme_catalog_find(const giochi_theme_catalog *cat, const char *id)
{
    if (cat == NULL || id == NULL || id[0] == '\0') {
        return -1;
    }
    for (int i = 0; i < cat->count; i++) {
        if (strcmp(cat->entry[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

int giochi_theme_device_default(char *out, size_t outsz, const char *dir)
{
    char path[512];
    char parse_err[160];
    gj_value *doc;
    const char *id;

    if (out == NULL || outsz == 0) {
        return -1;
    }
    out[0] = '\0';

    if (dir == NULL) {
        return -1;
    }
    if (snprintf(path, sizeof(path), "%s/device-theme.json", dir) >=
        (int)sizeof(path)) {
        return -1;
    }

    doc = gj_parse_file(path, parse_err, sizeof(parse_err));
    if (doc == NULL) {
        return -1;
    }
    id = gj_as_string(gj_object_get(doc, "theme_id"), NULL);
    if (id != NULL && id[0] != '\0') {
        giochi_strlcpy(out, id, outsz);
    }
    gj_free(doc);
    return out[0] != '\0' ? 0 : -1;
}

int giochi_theme_resolve(giochi_theme *theme, const char *dir,
                         const char *requested, const char *device_default,
                         char *used_id, size_t used_sz, char *err, size_t errsz)
{
    giochi_theme_catalog cat;
    const int tem_catalogo =
        giochi_theme_catalog_load(&cat, dir, NULL, 0) == 0;

    if (err != NULL && errsz > 0) {
        err[0] = '\0';
    }
    if (used_id != NULL && used_sz > 0) {
        used_id[0] = '\0';
    }

    const char *candidatos[3];
    int n = 0;

    candidatos[n++] = requested;
    candidatos[n++] = device_default;
    if (tem_catalogo) {
        candidatos[n++] = cat.default_id;
    }

    for (int i = 0; i < n; i++) {
        const char *id = candidatos[i];

        if (id == NULL || id[0] == '\0') {
            continue;
        }
        if (tem_catalogo && giochi_theme_catalog_find(&cat, id) < 0) {
            /* Pedido explicito de tema inexistente aparece no log: o
             * settings.json pode estar velho e a UI mudaria sozinha. */
            if (i == 0 && err != NULL && errsz > 0) {
                (void)snprintf(err, errsz,
                               "'%s' nao existe no catalogo (%d temas); "
                               "usando o padrao do aparelho",
                               id, cat.count);
            }
            continue;
        }
        if (giochi_theme_load(theme, dir, themes_file(id), NULL, 0) == 0) {
            if (used_id != NULL && used_sz > 0) {
                giochi_strlcpy(used_id, id, used_sz);
            }
            return 0;
        }
        if (i == 0 && err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "tema '%s': arquivo ilegivel ou ausente",
                           id);
        }
    }

    /* Ultimo recurso: o tema padrao do catalogo, mesmo sem index.json. */
    if (giochi_theme_load(theme, dir, themes_file("dark_default"), err, errsz) ==
        0) {
        if (used_id != NULL && used_sz > 0) {
            giochi_strlcpy(used_id, theme->id, used_sz);
        }
        return 0;
    }
    return -1;
}
