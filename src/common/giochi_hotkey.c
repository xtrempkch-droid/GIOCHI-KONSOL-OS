#include "giochi_hotkey.h"

#include <string.h>

void giochi_hotkey_reset(giochi_hotkey_state *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

static int handle_home(giochi_hotkey_state *state, int pressed,
                       unsigned long long now_ms)
{
    if (pressed) {
        state->home_down = 1;
        state->home_down_ms = now_ms;
        state->combo_fired = 0;
        return GIOCHI_HK_EVENT_NONE;
    }

    if (!state->home_down) {
        return GIOCHI_HK_EVENT_NONE;
    }
    state->home_down = 0;

    if (state->combo_fired) {
        state->combo_fired = 0;
        return GIOCHI_HK_EVENT_NONE;
    }
    if (now_ms - state->home_down_ms < GIOCHI_HOME_SHORT_PRESS_MS) {
        return GIOCHI_HK_EVENT_SHORT_HOME;
    }
    /* Home segurado alem de 500 ms sem Start: sem acao. */
    return GIOCHI_HK_EVENT_NONE;
}

static int handle_start(giochi_hotkey_state *state, int pressed,
                        unsigned long long now_ms)
{
    if (pressed) {
        state->start_down = 1;
        state->start_down_ms = now_ms;

        if (state->home_down && !state->combo_fired &&
            now_ms - state->home_down_ms >= GIOCHI_EXIT_COMBO_HOLD_MS) {
            state->combo_fired = 1;
            return GIOCHI_HK_EVENT_EXIT_COMBO;
        }
        return GIOCHI_HK_EVENT_NONE;
    }

    if (!state->start_down) {
        return GIOCHI_HK_EVENT_NONE;
    }
    state->start_down = 0;

    if (state->home_down && !state->combo_fired &&
        now_ms - state->start_down_ms >= GIOCHI_EXIT_COMBO_HOLD_MS &&
        now_ms - state->home_down_ms >= GIOCHI_EXIT_COMBO_HOLD_MS) {
        state->combo_fired = 1;
        return GIOCHI_HK_EVENT_EXIT_COMBO;
    }
    return GIOCHI_HK_EVENT_NONE;
}

int giochi_hotkey_event(giochi_hotkey_state *state, int key, int pressed,
                        unsigned long long now_ms)
{
    if (state == NULL) {
        return GIOCHI_HK_EVENT_NONE;
    }
    if (key == GIOCHI_HK_KEY_HOME) {
        return handle_home(state, pressed, now_ms);
    }
    if (key == GIOCHI_HK_KEY_START) {
        return handle_start(state, pressed, now_ms);
    }
    return GIOCHI_HK_EVENT_NONE;
}
