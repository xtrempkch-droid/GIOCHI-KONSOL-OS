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

void giochi_overlay_init(giochi_overlay *overlay)
{
    if (overlay == NULL) {
        return;
    }
    memset(overlay, 0, sizeof(*overlay));
    overlay->index = 0;
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
    }
}

int giochi_overlay_toggle(giochi_overlay *overlay)
{
    if (overlay == NULL) {
        return 0;
    }
    overlay->open = !overlay->open;
    return overlay->open;
}

void giochi_overlay_move(giochi_overlay *overlay, int delta)
{
    const int count = GIOCHI_OVERLAY_ITEM_COUNT;
    int next;

    if (overlay == NULL || delta == 0) {
        return;
    }
    next = overlay->index + delta;
    while (next < 0) {
        next += count;
    }
    next %= count;
    overlay->index = next;
}

int giochi_overlay_count(void)
{
    return GIOCHI_OVERLAY_ITEM_COUNT;
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
    return giochi_overlay_item(overlay->index);
}
