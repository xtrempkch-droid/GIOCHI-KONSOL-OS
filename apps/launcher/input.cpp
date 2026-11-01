#include "input.hpp"

#include "giochi_log.h"

#include <cstdio>
#include <cstring>

namespace giochi {

static_assert(GIOCHI_HOME_SHORT_PRESS_MS == 500ULL,
              "secao 4.1.1 fixa a janela de 500 ms");
static_assert(GIOCHI_EXIT_COMBO_HOLD_MS == 500ULL,
              "secao 4.1.1 fixa a janela de 500 ms");

namespace {

/* Nomes canonicos -> codigo evdev (linux/input-event-codes.h). */
struct NameCode {
    const char *name;
    unsigned short code;
};

const NameCode kDaemonCodes[] = {
    { "BTN_SOUTH", 0x130 }, { "BTN_A", 0x130 },
    { "BTN_EAST", 0x131 },  { "BTN_B", 0x131 },
    { "BTN_C", 0x132 },
    { "BTN_NORTH", 0x133 }, { "BTN_X", 0x133 },
    { "BTN_WEST", 0x134 },  { "BTN_Y", 0x134 },
    { "BTN_Z", 0x135 },
    { "BTN_TL", 0x136 },
    { "BTN_TR", 0x137 },
    { "BTN_TL2", 0x138 },
    { "BTN_TR2", 0x139 },
    { "BTN_SELECT", 0x13a },
    { "BTN_START", 0x13b },
    { "BTN_MODE", 0x13c },
    { "BTN_THUMBL", 0x13d },
    { "BTN_THUMBR", 0x13e },
    { "BTN_DPAD_UP", 0x220 },
    { "BTN_DPAD_DOWN", 0x221 },
    { "BTN_DPAD_LEFT", 0x222 },
    { "BTN_DPAD_RIGHT", 0x223 },
};

/* Mapa compilado (usado quando buttons.map falta) — buttons.default.json. */
const unsigned short kDefaultCodes[GIOCHI_BTN_CODE_COUNT] = {
    0,      /* BtnNone */
    0x220,  /* BtnUp */
    0x221,  /* BtnDown */
    0x222,  /* BtnLeft */
    0x223,  /* BtnRight */
    0x130,  /* BtnA */
    0x131,  /* BtnB */
    0x133,  /* BtnX */
    0x134,  /* BtnY */
    0x136,  /* BtnL */
    0x137,  /* BtnR */
    0x13b,  /* BtnStart */
    0x13a,  /* BtnSelect */
    0x13c,  /* BtnHome */
};

const char *const kLogicalNames[GIOCHI_BTN_CODE_COUNT] = {
    nullptr, "up", "down", "left", "right", "a", "b", "x",
    "y", "l", "r", "start", "select", "home"
};

int logical_from_name(const char *name)
{
    for (int i = 1; i < GIOCHI_BTN_CODE_COUNT; i++) {
        if (kLogicalNames[i] != nullptr &&
            std::strcmp(kLogicalNames[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

}  // namespace

int input_code_from_name(const char *name, unsigned short *code)
{
    if (name == nullptr || code == nullptr) {
        return -1;
    }
    for (const NameCode &entry : kDaemonCodes) {
        if (std::strcmp(entry.name, name) == 0) {
            *code = entry.code;
            return 0;
        }
    }
    return -1;
}

void input_reset(InputState *state)
{
    if (state == nullptr) {
        return;
    }
    giochi_hotkey_reset(&state->hotkey);
    state->map_id[0] = '\0';
    std::memcpy(state->codes, kDefaultCodes, sizeof(state->codes));
}

Action input_handle(InputState *state, Button button, bool pressed,
                    unsigned long long now_ms)
{
    if (state == nullptr) {
        return ActionNone;
    }

    int key = button == BtnHome ? GIOCHI_HK_KEY_HOME
            : button == BtnStart ? GIOCHI_HK_KEY_START
            : -1;

    if (key >= 0) {
        int events = giochi_hotkey_event(&state->hotkey, key,
                                         pressed ? 1 : 0, now_ms);

        if ((events & GIOCHI_HK_EVENT_EXIT_COMBO) != 0) {
            return ActionExitGame;
        }
        if ((events & GIOCHI_HK_EVENT_SHORT_HOME) != 0) {
            return ActionHome;
        }
        return ActionNone;
    }

    if (!pressed) {
        return ActionNone;
    }

    switch (button) {
    case BtnL:        return ActionTabPrev;
    case BtnR:        return ActionTabNext;
    case BtnUp:       return ActionMoveUp;
    case BtnDown:     return ActionMoveDown;
    case BtnA:        return ActionActivate;
    case BtnB:        return ActionBack;
    case BtnX:        return ActionNextTrack;
    case BtnY:        return ActionPrevTrack;
    case BtnSelect:   return ActionSettingsToggle;
    default:          return ActionNone;
    }
}

}  // namespace giochi

namespace giochi {

/* --- buttons.map (secao 2.2) -----------------------------------------
 *
 * Formato: uma secao "[id]" seguida de "nome = BTN_*". Aceita '#' como
 * comentario. Codigos desconhecidos sao registrados e mapeados em
 * evdev.cpp (mapa nome->codigo); aqui so guardamos a escolha do usuario.
 */
int input_load_button_map(InputState *state, const std::string &path,
                          const std::string &map_id)
{
    FILE *fp;
    char line[256];
    int in_section = 0;
    int found = 0;

    if (state == nullptr) {
        return -1;
    }
    std::memcpy(state->codes, kDefaultCodes, sizeof(state->codes));

    fp = std::fopen(path.c_str(), "r");
    if (fp == nullptr) {
        GLOG_WARN("buttons.map: '%s' inacessivel", path.c_str());
        return -1;
    }

    while (std::fgets(line, sizeof(line), fp) != nullptr) {
        char *p = line;

        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '#' || *p == '\n' || *p == '\0') {
            continue;
        }
        if (*p == '[') {
            char *end = std::strchr(p, ']');

            if (end != nullptr) {
                *end = '\0';
                in_section = std::strcmp(p + 1, map_id.c_str()) == 0;
                if (in_section) {
                    found = 1;
                }
            }
            continue;
        }
        if (!in_section) {
            continue;
        }
        char *eq = std::strchr(p, '=');
        if (eq == nullptr) {
            GLOG_WARN("buttons.map: linha invalida em [%s]: %s", map_id.c_str(),
                      p);
            continue;
        }
        *eq = '\0';

        /* nome */
        char *name_end = eq;
        while (name_end > p && (name_end[-1] == ' ' || name_end[-1] == '\t')) {
            name_end--;
        }
        *name_end = '\0';

        /* codigo */
        char *code_str = eq + 1;
        while (*code_str == ' ' || *code_str == '\t') {
            code_str++;
        }
        char *code_end = code_str;
        while (*code_end != '\0' && *code_end != '\n' && *code_end != '\r' &&
               *code_end != ' ' && *code_end != '\t') {
            code_end++;
        }
        *code_end = '\0';

        int logical = logical_from_name(p);
        if (logical < 0) {
            GLOG_WARN("buttons.map: nome desconhecido '%s' em [%s]", p,
                      map_id.c_str());
            continue;
        }
        unsigned short code = 0;
        if (input_code_from_name(code_str, &code) != 0) {
            GLOG_WARN("buttons.map: codigo evdev desconhecido '%s' para '%s'",
                      code_str, p);
            continue;
        }
        state->codes[logical] = code;
    }
    std::fclose(fp);

    if (!found) {
        GLOG_WARN("buttons.map: secao [%s] ausente; usando o mapa compilado",
                  map_id.c_str());
        return -1;
    }
    std::snprintf(state->map_id, sizeof(state->map_id), "%s", map_id.c_str());
    GLOG_INFO("buttons.map: secao [%s] carregada de %s", map_id.c_str(),
              path.c_str());
    return 0;
}

}  // namespace giochi
