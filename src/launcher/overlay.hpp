/* Giochi Konsol OS v1.2 — overlay modal de Configuracoes (secao 4.9).
 *
 * Dois niveis desde o adendo 4.6.1:
 *   nivel 0: os 7 itens da secao 4.9.3
 *   nivel 1: submenu de "Sistema" (idioma, fuso, brilho, suspensao,
 *            modo USB e Modo Desenvolvedor)
 * B volta um nivel (4.1.2: "sobe um nivel") e, no nivel 0, fecha o overlay.
 */
#ifndef GIOCHI_OVERLAY_H
#define GIOCHI_OVERLAY_H

#define GIOCHI_OVERLAY_ITEM_COUNT 7
#define GIOCHI_SYSTEM_ITEM_COUNT 6

typedef struct {
    int open;
    int level; /* 0 = menu principal, 1 = submenu Sistema */
    int index;
    int parent_index; /* item do nivel 0 de onde o submenu foi aberto */
} giochi_overlay;

void giochi_overlay_init(giochi_overlay *overlay);
void giochi_overlay_open(giochi_overlay *overlay);
void giochi_overlay_close(giochi_overlay *overlay);
int giochi_overlay_toggle(giochi_overlay *overlay);
void giochi_overlay_move(giochi_overlay *overlay, int delta);

/* Entra no submenu do item atual, se ele tiver um. Retorna 1 se entrou. */
int giochi_overlay_enter(giochi_overlay *overlay);

/* Volta um nivel. Retorna 1 se ainda aberto, 0 se fechou. */
int giochi_overlay_back(giochi_overlay *overlay);

int giochi_overlay_count(const giochi_overlay *overlay);
const char *giochi_overlay_item_at(const giochi_overlay *overlay, int index);
const char *giochi_overlay_item(int index);
const char *giochi_overlay_current(const giochi_overlay *overlay);

/* Indice de "Modo Desenvolvedor" dentro do submenu Sistema (adendo 4.6.1). */
#define GIOCHI_SYSTEM_DEV_MODE_INDEX 5
#define GIOCHI_SYSTEM_USB_MODE_INDEX 4

#endif /* GIOCHI_OVERLAY_H */
