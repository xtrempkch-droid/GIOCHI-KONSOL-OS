/* Giochi Konsol OS v1.2 — overlay modal de Configuracoes (secao 4.9). */
#ifndef GIOCHI_OVERLAY_H
#define GIOCHI_OVERLAY_H

typedef struct {
    int open;
    int index;
} giochi_overlay;

#define GIOCHI_OVERLAY_ITEM_COUNT 7

void giochi_overlay_init(giochi_overlay *overlay);
void giochi_overlay_open(giochi_overlay *overlay);
void giochi_overlay_close(giochi_overlay *overlay);
int giochi_overlay_toggle(giochi_overlay *overlay);
void giochi_overlay_move(giochi_overlay *overlay, int delta);

int giochi_overlay_count(void);
const char *giochi_overlay_item(int index);
const char *giochi_overlay_current(const giochi_overlay *overlay);

#endif /* GIOCHI_OVERLAY_H */
