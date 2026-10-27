#include "settings.hpp"
#include "test_util.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

void test_defaults(void)
{
    giochi_settings s;

    giochi_settings_defaults(&s);
    CHECK_STR_EQ(s.theme_id, "dark_default");
    CHECK_STR_EQ(s.language, "pt-BR");
    CHECK_STR_EQ(s.usb_mode, "mtp");
    CHECK(s.master_volume == 70);
    CHECK(s.font_scale > 0.99 && s.font_scale < 1.01);
    CHECK(s.wifi_enabled == 0);
    /* Adendo 4.6.1: Modo Desenvolvedor desativado por padrao. */
    CHECK(s.developer_ssh_usb_enabled == 0);
}

void test_roundtrip(void)
{
    char dir[] = "/tmp/giochi-settings-XXXXXX";
    giochi_settings saved;
    giochi_settings loaded;
    char err[256];

    CHECK(mkdtemp(dir) != nullptr);
    giochi_settings_defaults(&saved);
    saved.master_volume = 42;
    saved.wifi_enabled = 1;
    saved.developer_ssh_usb_enabled = 1;
    snprintf(saved.wifi_last_ssid, sizeof(saved.wifi_last_ssid), "Casa \"5G\"");
    snprintf(saved.language, sizeof(saved.language), "en-US");

    CHECK(giochi_settings_save(&saved, dir, err, sizeof(err)) == 0);
    CHECK(giochi_settings_load(&loaded, dir, err, sizeof(err)) == 0);
    CHECK(loaded.master_volume == 42);
    CHECK(loaded.wifi_enabled == 1);
    CHECK(loaded.developer_ssh_usb_enabled == 1);
    CHECK_STR_EQ(loaded.wifi_last_ssid, "Casa \"5G\"");
    CHECK_STR_EQ(loaded.language, "en-US");

    unlink((std::string(dir) + "/settings.json").c_str());
    rmdir(dir);
}

void test_out_of_range_clamped(void)
{
    char dir[] = "/tmp/giochi-settings-XXXXXX";
    giochi_settings s;
    char err[256];
    FILE *fp;

    CHECK(mkdtemp(dir) != nullptr);
    fp = fopen((std::string(dir) + "/settings.json").c_str(), "wb");
    CHECK(fp != nullptr);
    if (fp != nullptr) {
        fputs("{\"system\":{\"brightness\":500,\"usb_mode\":\"invalido\"},"
              "\"sound\":{\"master_volume\":250},"
              "\"appearance\":{\"font_scale\":99}}", fp);
        fclose(fp);
    }

    CHECK(giochi_settings_load(&s, dir, err, sizeof(err)) == 0);
    CHECK(s.brightness == 100);
    CHECK(s.master_volume == 100);
    CHECK(s.font_scale <= 2.0);
    CHECK_STR_EQ(s.usb_mode, "mtp");
    CHECK(s.developer_ssh_usb_enabled == 0);

    unlink((std::string(dir) + "/settings.json").c_str());
    rmdir(dir);
}

void test_corrupt_renamed_to_bak(void)
{
    char dir[] = "/tmp/giochi-settings-XXXXXX";
    giochi_settings s;
    char err[256];
    std::string path;
    std::string bak;
    struct stat st;
    FILE *fp;

    CHECK(mkdtemp(dir) != nullptr);
    path = std::string(dir) + "/settings.json";
    bak = path + ".bak";
    fp = fopen(path.c_str(), "wb");
    CHECK(fp != nullptr);
    if (fp != nullptr) {
        fputs("{ isso nao e json", fp);
        fclose(fp);
    }

    CHECK(giochi_settings_load(&s, dir, err, sizeof(err)) == -1);
    CHECK(stat(bak.c_str(), &st) == 0);
    CHECK(access(path.c_str(), F_OK) != 0);
    CHECK_STR_EQ(s.theme_id, "dark_default");

    unlink(bak.c_str());
    rmdir(dir);
}

}  // namespace

int main(void)
{
    test_defaults();
    test_roundtrip();
    test_out_of_range_clamped();
    test_corrupt_renamed_to_bak();
    TEST_SUMMARY("test_settings");
}
