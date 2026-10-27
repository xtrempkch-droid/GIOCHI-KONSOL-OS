/* Giochi Konsol OS v1.2 — combinacao Home/Start (secao 4.1.1).
 *
 * Logica unica compartilhada entre giochi-launcher e giochi-init (hotkey de
 * saida do jogo), para nao divergir os tempos de 500 ms.
 */
#ifndef GIOCHI_HOTKEY_H
#define GIOCHI_HOTKEY_H

#ifdef __cplusplus
extern "C" {
#endif

#define GIOCHI_HOME_SHORT_PRESS_MS 500ULL
#define GIOCHI_EXIT_COMBO_HOLD_MS 500ULL

enum {
    GIOCHI_HK_KEY_HOME = 0,
    GIOCHI_HK_KEY_START = 1
};

enum {
    GIOCHI_HK_EVENT_NONE = 0,
    GIOCHI_HK_EVENT_SHORT_HOME = 1 << 0,
    GIOCHI_HK_EVENT_EXIT_COMBO = 1 << 1
};

typedef struct {
    int home_down;
    int start_down;
    int combo_fired;
    unsigned long long home_down_ms;
    unsigned long long start_down_ms;
} giochi_hotkey_state;

void giochi_hotkey_reset(giochi_hotkey_state *state);

/* Retorna mascara de GIOCHI_HK_EVENT_*. */
int giochi_hotkey_event(giochi_hotkey_state *state, int key, int pressed,
                        unsigned long long now_ms);

#ifdef __cplusplus
}
#endif

#endif /* GIOCHI_HOTKEY_H */
