#include "giochi_hotkey.h"
#include "test_util.h"

static void test_short_home(void)
{
    giochi_hotkey_state s;

    giochi_hotkey_reset(&s);
    CHECK(giochi_hotkey_event(&s, GIOCHI_HK_KEY_HOME, 1, 0) ==
          GIOCHI_HK_EVENT_NONE);
    CHECK(giochi_hotkey_event(&s, GIOCHI_HK_KEY_HOME, 0, 200) ==
          GIOCHI_HK_EVENT_SHORT_HOME);
}

static void test_long_home_alone_does_nothing(void)
{
    giochi_hotkey_state s;

    giochi_hotkey_reset(&s);
    (void)giochi_hotkey_event(&s, GIOCHI_HK_KEY_HOME, 1, 0);
    CHECK(giochi_hotkey_event(&s, GIOCHI_HK_KEY_HOME, 0, 900) ==
          GIOCHI_HK_EVENT_NONE);
}

static void test_combo_home_then_start_held(void)
{
    giochi_hotkey_state s;

    giochi_hotkey_reset(&s);
    (void)giochi_hotkey_event(&s, GIOCHI_HK_KEY_HOME, 1, 0);
    /* Start pressionado antes dos 500 ms: ainda nao dispara. */
    CHECK(giochi_hotkey_event(&s, GIOCHI_HK_KEY_START, 1, 100) ==
          GIOCHI_HK_EVENT_NONE);
    /* Soltar Start depois da janela com Home ainda pressionado dispara. */
    CHECK(giochi_hotkey_event(&s, GIOCHI_HK_KEY_START, 0, 600) ==
          GIOCHI_HK_EVENT_EXIT_COMBO);
    /* Soltar Home depois nao gera Home. */
    CHECK(giochi_hotkey_event(&s, GIOCHI_HK_KEY_HOME, 0, 620) ==
          GIOCHI_HK_EVENT_NONE);
}

static void test_combo_start_after_home_hold(void)
{
    giochi_hotkey_state s;

    giochi_hotkey_reset(&s);
    (void)giochi_hotkey_event(&s, GIOCHI_HK_KEY_HOME, 1, 0);
    CHECK(giochi_hotkey_event(&s, GIOCHI_HK_KEY_START, 1, 500) ==
          GIOCHI_HK_EVENT_EXIT_COMBO);
}

static void test_start_alone(void)
{
    giochi_hotkey_state s;

    giochi_hotkey_reset(&s);
    CHECK(giochi_hotkey_event(&s, GIOCHI_HK_KEY_START, 1, 0) ==
          GIOCHI_HK_EVENT_NONE);
    CHECK(giochi_hotkey_event(&s, GIOCHI_HK_KEY_START, 0, 1000) ==
          GIOCHI_HK_EVENT_NONE);
}

int main(void)
{
    test_short_home();
    test_long_home_alone_does_nothing();
    test_combo_home_then_start_held();
    test_combo_start_after_home_hold();
    test_start_alone();
    TEST_SUMMARY("test_hotkey");
}
