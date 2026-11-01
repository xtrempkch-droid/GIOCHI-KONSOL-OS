/* Giochi Konsol OS v1.2 — catalogo de temas (secao 4.1.8).
 *
 * Um tema e' COR + METRICA. Ate' 2026-11 ele so' trocava cor, e as cinco
 * propostas de layout do mantenedor (docs/TEMAS.md) pediam mais: raio de canto,
 * borda, espessura do anel de foco, densidade da grade, escala da capa do album
 * e modo da barra de dicas. Todos esses numeros sao DADO do tema; quem os
 * traduz para pixel e' ui_geometry_apply_theme() e o render.cpp.
 *
 * Catalogo: assets/themes/index.json lista os temas e qual e' o padrao. O tema
 * ativo vem de settings.json (theme_id, §4.9.5). O padrao do APARELHO pode
 * sobrescrever o do catalogo por /usr/share/giochi/device-theme.json.
 */
#ifndef GIOCHI_THEME_H
#define GIOCHI_THEME_H

#include <stddef.h>

#define GIOCHI_THEME_MAX_CATALOG 16
#define GIOCHI_THEME_ID_LEN 32
#define GIOCHI_THEME_NAME_LEN 48

/* Densidade da grade. Muda gap, altura do titulo e recuo — e, por tabela,
 * quantas colunas e linhas cabem (docs/LAYOUT-PROPOSTA.md §3). */
enum giochi_density {
    GIOCHI_DENSITY_COMPACTA = 0,
    GIOCHI_DENSITY_NORMAL = 1,
    GIOCHI_DENSITY_ESPACOSA = 2
};

/* O que a barra de dicas mostra. `so_gamepad` existe porque num portatil as
 * dicas de teclado do layout4 nao tem tecla nenhuma para apertar. */
enum giochi_hints_mode {
    GIOCHI_HINTS_SO_GAMEPAD = 0, /* padrao: so' botao do aparelho */
    GIOCHI_HINTS_COMPLETO = 1    /* + atalho de teclado */
};

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

    /* Metricas em px do PAINEL BASE (640x480) — escaladas como os demais
     * tokens, nunca usadas cruas no painel grande. */
    int corner_px;
    int border_px;
    int focus_ring_px;
    int topbar_h_px;
    int hint_bar_h_px;
    int density;
    int hints;
    int cover_pct;       /* 100 = token base da capa (120x120) */
    int title_scale_pct; /* 100 = 14/12/12 do painel base */
} giochi_theme;

typedef struct {
    char id[GIOCHI_THEME_ID_LEN];
    char name[GIOCHI_THEME_NAME_LEN];
} giochi_theme_entry;

typedef struct {
    int count;
    char default_id[GIOCHI_THEME_ID_LEN];
    giochi_theme_entry entry[GIOCHI_THEME_MAX_CATALOG];
} giochi_theme_catalog;

void giochi_theme_defaults(giochi_theme *theme);

/* 0 = ok; -1 = falha (tema recebe o default compilado). */
int giochi_theme_load(giochi_theme *theme, const char *dir, const char *file,
                      char *err, size_t errsz);

/* Catalogo em `dir/themes/index.json`. 0 = ok; -1 = ausente/ilegivel (o
 * chamador cai para o tema unico `dir/theme.json`). */
int giochi_theme_catalog_load(giochi_theme_catalog *cat, const char *dir,
                              char *err, size_t errsz);

/* Indice do id no catalogo, -1 se nao existe. */
int giochi_theme_catalog_find(const giochi_theme_catalog *cat, const char *id);

/* Tema padrao do aparelho: `dir/device-theme.json` -> theme_id.
 * 0 = ok; -1 = ausente. */
int giochi_theme_device_default(char *out, size_t outsz, const char *dir);

/* Resolve e carrega o tema ativo.
 *
 * Ordem: `requested` (CLI/env/settings) -> `device_default` -> catalogo ->
 * `dir/theme.json` (compatibilidade). Id pedido que nao existe no catalogo e'
 * RECUSADO com mensagem, nao silenciado: um settings.json apontando para tema
 * apagado tem de aparecer no log, senao a UI muda sozinha e ninguem sabe por
 * que (mesma classe do §3.47).
 *
 * `used_id` recebe o id que valeu. 0 = ok; -1 = nenhum tema utilizavel (o
 * chamador fica com o default compilado). */
int giochi_theme_resolve(giochi_theme *theme, const char *dir,
                         const char *requested, const char *device_default,
                         char *used_id, size_t used_sz, char *err, size_t errsz);

const char *giochi_density_name(int density);
const char *giochi_hints_name(int mode);

/* "#RRGGBB" -> 0xRRGGBB. */
unsigned giochi_theme_rgb(const char *hex, unsigned fallback);

#endif /* GIOCHI_THEME_H */
