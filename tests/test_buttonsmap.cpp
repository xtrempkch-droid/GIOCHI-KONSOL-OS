#include "input.hpp"
#include "test_util.h"

#include <cstdio>
#include <string>
#include <unistd.h>

namespace {

void write_file(const std::string &path, const std::string &content)
{
    FILE *fp = fopen(path.c_str(), "wb");

    if (fp != nullptr) {
        fwrite(content.data(), 1, content.size(), fp);
        fclose(fp);
    }
}

void test_defaults(void)
{
    giochi::InputState state;

    giochi::input_reset(&state);
    CHECK(state.codes[giochi::BtnA] == 0x130);
    CHECK(state.codes[giochi::BtnB] == 0x131);
    CHECK(state.codes[giochi::BtnHome] == 0x13c);
    CHECK(state.codes[giochi::BtnUp] == 0x220);
    CHECK(state.map_id[0] == '\0');
}

void test_names(void)
{
    unsigned short code = 0;

    CHECK(giochi::input_code_from_name("BTN_SOUTH", &code) == 0);
    CHECK(code == 0x130);
    CHECK(giochi::input_code_from_name("BTN_EAST", &code) == 0);
    CHECK(code == 0x131);
    CHECK(giochi::input_code_from_name("BTN_MODE", &code) == 0);
    CHECK(code == 0x13c);
    CHECK(giochi::input_code_from_name("BTN_INEXISTENTE", &code) != 0);
    CHECK(giochi::input_code_from_name(nullptr, &code) != 0);
}

void test_x35s_swaps_a_b(void)
{
    char dir[] = "/tmp/giochi-btnmap-XXXXXX";
    std::string path;
    giochi::InputState state;

    CHECK(mkdtemp(dir) != nullptr);
    path = std::string(dir) + "/buttons.map";
    write_file(path,
        "# comentario\n"
        "[default]\n"
        "a = BTN_EAST\n"
        "b = BTN_SOUTH\n"
        "\n"
        "[x35s]\n"
        "a = BTN_SOUTH\n"
        "b = BTN_EAST\n"
        "home = BTN_MODE\n");

    giochi::input_reset(&state);
    CHECK(giochi::input_load_button_map(&state, path, "x35s") == 0);
    CHECK(state.codes[giochi::BtnA] == 0x130);
    CHECK(state.codes[giochi::BtnB] == 0x131);
    CHECK(state.codes[giochi::BtnHome] == 0x13c);
    CHECK(std::string(state.map_id) == "x35s");

    giochi::input_reset(&state);
    CHECK(giochi::input_load_button_map(&state, path, "default") == 0);
    CHECK(state.codes[giochi::BtnA] == 0x131);
    CHECK(state.codes[giochi::BtnB] == 0x130);

    unlink(path.c_str());
    rmdir(dir);
}

void test_missing_section_keeps_compiled(void)
{
    char dir[] = "/tmp/giochi-btnmap-XXXXXX";
    std::string path;
    giochi::InputState state;

    CHECK(mkdtemp(dir) != nullptr);
    path = std::string(dir) + "/buttons.map";
    write_file(path, "[default]\na = BTN_SOUTH\n");

    giochi::input_reset(&state);
    CHECK(giochi::input_load_button_map(&state, path, "inexistente") == -1);
    CHECK(state.codes[giochi::BtnA] == 0x130); /* mapa compilado */

    CHECK(giochi::input_load_button_map(&state, "/nao/existe.map", "default") ==
          -1);
    CHECK(state.codes[giochi::BtnA] == 0x130);

    unlink(path.c_str());
    rmdir(dir);
}

void test_invalid_entries_ignored(void)
{
    char dir[] = "/tmp/giochi-btnmap-XXXXXX";
    std::string path;
    giochi::InputState state;

    CHECK(mkdtemp(dir) != nullptr);
    path = std::string(dir) + "/buttons.map";
    write_file(path,
        "[default]\n"
        "sem_igual_em\n"
        "nome_errado = BTN_SOUTH\n"
        "a = BTN_INEXISTENTE\n"
        "b = BTN_EAST\n");

    giochi::input_reset(&state);
    CHECK(giochi::input_load_button_map(&state, path, "default") == 0);
    CHECK(state.codes[giochi::BtnA] == 0x130); /* manteve o compilado */
    CHECK(state.codes[giochi::BtnB] == 0x131);

    unlink(path.c_str());
    rmdir(dir);
}

}  // namespace

int main(void)
{
    test_defaults();
    test_names();
    test_x35s_swaps_a_b();
    test_missing_section_keeps_compiled();
    test_invalid_entries_ignored();
    TEST_SUMMARY("test_buttonsmap");
}
