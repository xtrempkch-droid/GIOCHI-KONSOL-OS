/* Ver giochi_deviceconf.h. Este teste existe por causa de um bug real: o
 * Launcher pulava 20 caracteres depois de "GIOCHI_BUTTONS_MAP=" (a chave tem
 * 19) e lia a secao como "efault". Na X55 o log dizia
 * "buttons.map: secao [efault] ausente; usando o mapa compilado" — o mapa do
 * usuario era descartado sem nenhum erro visivel. */
#include "giochi_deviceconf.h"
#include "test_util.h"

#include <cstdio>
#include <string>

namespace {

void write_file(const std::string &path, const std::string &content)
{
    FILE *fp = fopen(path.c_str(), "wb");

    if (fp != nullptr) {
        fwrite(content.data(), 1, content.size(), fp);
        fclose(fp);
    }
}

std::string tmp_path(const char *name)
{
    return std::string("/tmp/giochi_deviceconf_") + name;
}

/* O conteudo real gerado por post-build.sh (trecho). */
const char *kReal =
    "# gerado no build\n"
    "GIOCHI_DEVICE=x55\n"
    "GIOCHI_NAME=\"Powkiddy X55\"\n"
    "GIOCHI_SOC=rk3566\n"
    "GIOCHI_PANEL=720x1280\n"
    "GIOCHI_BUTTONS_MAP=default\n"
    "GIOCHI_TESTED=yes\n";

void test_key_completa(void)
{
    std::string path = tmp_path("real.conf");
    char out[64];

    write_file(path, kReal);
    CHECK(giochi_deviceconf_get(path.c_str(), "GIOCHI_BUTTONS_MAP", out,
                                sizeof(out)) == 0);
    /* "efault" era o que o bug produzia. */
    CHECK(std::string(out) == "default");
    CHECK(giochi_deviceconf_get(path.c_str(), "GIOCHI_DEVICE", out,
                                sizeof(out)) == 0);
    CHECK(std::string(out) == "x55");
    CHECK(giochi_deviceconf_get(path.c_str(), "GIOCHI_SOC", out,
                                sizeof(out)) == 0);
    CHECK(std::string(out) == "rk3566");
    std::remove(path.c_str());
}

void test_chave_nao_casa_prefixo(void)
{
    std::string path = tmp_path("prefixo.conf");
    char out[64];

    write_file(path, "X_GIOCHI_SOC=rk3326\nGIOCHI_SOC=rk3566\n");
    CHECK(giochi_deviceconf_get(path.c_str(), "GIOCHI_SOC", out,
                                sizeof(out)) == 0);
    CHECK(std::string(out) == "rk3566");
    std::remove(path.c_str());
}

void test_aspas_e_espaco(void)
{
    std::string path = tmp_path("aspas.conf");
    char out[64];

    write_file(path, "GIOCHI_NAME=\"Powkiddy X55\"   \r\nGIOCHI_VAZIO=  \n");
    CHECK(giochi_deviceconf_get(path.c_str(), "GIOCHI_NAME", out,
                                sizeof(out)) == 0);
    /* Aspas fazem parte do valor: quem consome decide se tira. O que nao pode
     * e o espaco e o CR vazarem para dentro do nome. */
    CHECK(std::string(out) == "\"Powkiddy X55\"");
    CHECK(giochi_deviceconf_get(path.c_str(), "GIOCHI_VAZIO", out,
                                sizeof(out)) != 0);
    std::remove(path.c_str());
}

void test_ausente_e_fallback(void)
{
    char out[64];
    const char *value;

    CHECK(giochi_deviceconf_get("/nao/existe/device.conf", "GIOCHI_SOC", out,
                                sizeof(out)) != 0);
    CHECK(out[0] == '\0');

    value = giochi_deviceconf_get_or("/nao/existe/device.conf", "GIOCHI_SOC",
                                     out, sizeof(out), "rk3326");
    CHECK(std::string(value) == "rk3326");

    /* Valor maior que o buffer: nao trunca, devolve o fallback. Truncar um SoC
     * ou um nome de aparelho e pior que ignorar. */
    std::string path = tmp_path("grande.conf");

    write_file(path, "GIOCHI_DEVICE=abcdefghij\n");
    value = giochi_deviceconf_get_or(path.c_str(), "GIOCHI_DEVICE", out, 4,
                                     "generic");
    CHECK(std::string(value) == "generic");
    std::remove(path.c_str());
}

} /* namespace */

int main(void)
{
    test_key_completa();
    test_chave_nao_casa_prefixo();
    test_aspas_e_espaco();
    test_ausente_e_fallback();
    TEST_SUMMARY("deviceconf");
}
