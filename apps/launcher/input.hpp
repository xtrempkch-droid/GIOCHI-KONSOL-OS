/* Giochi Konsol OS v1.2 — semantica de botoes (secao 4.1.1).
 *
 * Mapeamento puro sobre (botao, pressionado, timestamp): sem evdev, sem SDL.
 * A combinacao Home/Start vem de common/giochi_hotkey (mesma dosagem usada
 * pelo giochi-init ao encerrar jogos).
 */
#ifndef GIOCHI_INPUT_HPP
#define GIOCHI_INPUT_HPP

#include <string>

#include "giochi_hotkey.h"

namespace giochi {

enum Button {
    BtnNone = 0,
    BtnUp,
    BtnDown,
    BtnLeft,
    BtnRight,
    BtnA,
    BtnB,
    BtnX,
    BtnY,
    BtnL,
    BtnR,
    BtnStart,
    BtnSelect,
    BtnHome
};

enum Action {
    ActionNone = 0,
    ActionTabPrev,
    ActionTabNext,
    ActionMoveUp,
    ActionMoveDown,
    ActionActivate,
    ActionBack,
    ActionHome,
    ActionSettingsToggle,
    ActionExitGame,
    ActionNextTrack,
    ActionPrevTrack
};

#define GIOCHI_BTN_CODE_COUNT 14

struct InputState {
    giochi_hotkey_state hotkey{};
    /* Mapa ativo (secao 2.2). Vazio => nomes evdev padrao do backend. */
    char map_id[32];
    /* Codigo evdev de cada Button, na ordem do enum. */
    unsigned short codes[GIOCHI_BTN_CODE_COUNT];
};

/* Codigo evdev a partir do nome canonico ("BTN_SOUTH", "BTN_DPAD_UP", ...). */
int input_code_from_name(const char *name, unsigned short *code);

/* Carrega <asset_dir>/buttons.map, secao <map_id> (ex.: "default", "x35s").
 * Preenche state->codes. Retorna 0 em sucesso; -1 mantem o mapa compilado. */
int input_load_button_map(InputState *state, const std::string &path,
                          const std::string &map_id);

void input_reset(InputState *state);

Action input_handle(InputState *state, Button button, bool pressed,
                    unsigned long long now_ms);

}  // namespace giochi

#endif /* GIOCHI_INPUT_HPP */
