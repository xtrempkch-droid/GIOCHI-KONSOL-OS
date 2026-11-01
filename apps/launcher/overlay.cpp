#include "overlay.hpp"

#include <stddef.h>
#include <string.h>

static const char *const items[GIOCHI_OVERLAY_ITEM_COUNT] = {
    "Aparência",
    "Sistema",
    "Som",
    "Loja",
    "Rede",
    "Sobre",
    "Desligar"
};

/* Submenu de Sistema (adendo 4.6.1 inclui o Modo Desenvolvedor). */
static const char *const system_items[GIOCHI_SYSTEM_ITEM_COUNT] = {
    "Idioma",
    "Fuso horário",
    "Brilho",
    "Suspensão",
    "Modo USB",
    "Modo Desenvolvedor",
    "Tema"
};

/* A MESMA ordem de `items`, em chaves de i18n. */
static const char *const item_keys[GIOCHI_OVERLAY_ITEM_COUNT] = {
    "settings.appearance",
    "settings.system",
    "settings.sound",
    "settings.store",
    "settings.network",
    "settings.about",
    "settings.poweroff"
};

const char *giochi_overlay_key(int index)
{
    if (index < 0 || index >= GIOCHI_OVERLAY_ITEM_COUNT) {
        return "";
    }
    return item_keys[index];
}

/* A MESMA ordem de `system_items`, em chaves de i18n. Quem precisa do texto
 * traduzido usa `giochi_overlay_system_key`. */
static const char *const system_keys[GIOCHI_SYSTEM_ITEM_COUNT] = {
    "settings.language",
    "settings.timezone",
    "settings.brightness",
    "settings.suspend",
    "settings.usb_mode",
    "settings.dev_mode",
    "settings.theme"
};

const char *giochi_overlay_system_key(int index)
{
    if (index < 0 || index >= GIOCHI_SYSTEM_ITEM_COUNT) {
        return "";
    }
    return system_keys[index];
}

/* Indice do item "Sistema" no nivel 0. */
#define SYSTEM_INDEX 1

void giochi_overlay_init(giochi_overlay *overlay)
{
    if (overlay == NULL) {
        return;
    }
    memset(overlay, 0, sizeof(*overlay));
}

void giochi_overlay_open(giochi_overlay *overlay)
{
    if (overlay != NULL) {
        overlay->open = 1;
    }
}

void giochi_overlay_close(giochi_overlay *overlay)
{
    if (overlay != NULL) {
        overlay->open = 0;
        overlay->level = 0;
        overlay->parent_index = 0;
    }
}

int giochi_overlay_toggle(giochi_overlay *overlay)
{
    if (overlay == NULL) {
        return 0;
    }
    if (overlay->open) {
        giochi_overlay_close(overlay);
        return 0;
    }
    overlay->open = 1;
    overlay->level = 0;
    return 1;
}

int giochi_overlay_count(const giochi_overlay *overlay)
{
    if (overlay != NULL && overlay->level == 1) {
        return GIOCHI_SYSTEM_ITEM_COUNT;
    }
    return GIOCHI_OVERLAY_ITEM_COUNT;
}

const char *giochi_overlay_item_at(const giochi_overlay *overlay, int index)
{
    int count = giochi_overlay_count(overlay);

    if (index < 0 || index >= count) {
        return "";
    }
    if (overlay != NULL && overlay->level == 1) {
        return system_items[index];
    }
    return items[index];
}

int giochi_overlay_enter(giochi_overlay *overlay)
{
    if (overlay == NULL || !overlay->open || overlay->level != 0) {
        return 0;
    }
    if (overlay->index != SYSTEM_INDEX) {
        return 0;
    }
    overlay->level = 1;
    overlay->parent_index = overlay->index;
    overlay->index = 0;
    return 1;
}

int giochi_overlay_back(giochi_overlay *overlay)
{
    if (overlay == NULL || !overlay->open) {
        return 0;
    }
    if (overlay->level == 1) {
        overlay->level = 0;
        overlay->index = overlay->parent_index;
        return 1;
    }
    giochi_overlay_close(overlay);
    return 0;
}

void giochi_overlay_move(giochi_overlay *overlay, int delta)
{
    int count;
    int next;

    if (overlay == NULL || delta == 0) {
        return;
    }
    count = giochi_overlay_count(overlay);
    if (count <= 0) {
        return;
    }
    next = overlay->index + delta;
    while (next < 0) {
        next += count;
    }
    overlay->index = next % count;
}

const char *giochi_overlay_item(int index)
{
    if (index < 0 || index >= GIOCHI_OVERLAY_ITEM_COUNT) {
        return "";
    }
    return items[index];
}

const char *giochi_overlay_current(const giochi_overlay *overlay)
{
    if (overlay == NULL) {
        return "";
    }
    return giochi_overlay_item_at(overlay, overlay->index);
}
