#include "layout.hpp"
#include "test_util.h"

#include <cstdio>
#include <cstring>
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
    giochi_layout layout;

    giochi_layout_defaults(&layout);
    CHECK(layout.tab_count == 5);
    CHECK_STR_EQ(giochi_layout_tab_id(&layout, 0), "music");
    CHECK_STR_EQ(giochi_layout_tab_id(&layout, 1), "library");
    CHECK_STR_EQ(giochi_layout_tab_id(&layout, 2), "home");
    CHECK_STR_EQ(giochi_layout_tab_id(&layout, 3), "store");
    CHECK_STR_EQ(giochi_layout_tab_id(&layout, 4), "ports");
    CHECK(layout.default_index == 2);
    CHECK(layout.center_index == 2);
    CHECK(giochi_layout_index_of(&layout, "ports") == 4);
    CHECK(giochi_layout_index_of(&layout, "inexistente") == -1);
}

void test_wrap(void)
{
    giochi_layout layout;

    giochi_layout_defaults(&layout);
    CHECK(giochi_layout_next(&layout, 4) == 0);
    CHECK(giochi_layout_prev(&layout, 0) == 4);
    CHECK(giochi_layout_next(&layout, 2) == 3);
}

void test_load_assets(void)
{
    giochi_layout layout;
    char err[256];
    int rc = giochi_layout_load(&layout, GIOCHI_ASSET_DIR, "layout.json", err,
                                sizeof(err));

    CHECK(rc == 0);
    CHECK(layout.tab_count == 5);
    CHECK_STR_EQ(giochi_layout_tab_id(&layout, layout.center_index), "home");
    CHECK_STR_EQ(layout.theme_id, "dark_default");
    CHECK(layout.tabs[3].is_app == 1);
    CHECK_STR_EQ(layout.tabs[3].target, "APP:giochi-store");
}

void test_invalid_falls_back(void)
{
    char dir[] = "/tmp/giochi-layout-XXXXXX";
    giochi_layout layout;
    char err[256];

    CHECK(mkdtemp(dir) != nullptr);
    write_file(std::string(dir) + "/layout.json", "{ nao e json }");

    CHECK(giochi_layout_load(&layout, dir, "layout.json", err, sizeof(err)) ==
          -1);
    CHECK(layout.tab_count == 5);
    CHECK(layout.default_index == 2);

    unlink((std::string(dir) + "/layout.json").c_str());
    CHECK(giochi_layout_load(&layout, dir, "layout.json", err, sizeof(err)) ==
          -1);
    CHECK(layout.tab_count == 5);
    rmdir(dir);
}

void test_unknown_tab_skipped(void)
{
    char dir[] = "/tmp/giochi-layout-XXXXXX";
    giochi_layout layout;
    char err[256];

    CHECK(mkdtemp(dir) != nullptr);
    write_file(std::string(dir) + "/layout.json",
        "{\"schema_version\":\"1.6\",\"theme_id\":\"dark_default\","
        "\"tabs\":{\"order\":[\"home\",\"misteriosa\"],"
        "\"default\":\"home\",\"visual_center\":\"home\","
        "\"layouts\":{\"home\":\"home.json\"}}}");

    CHECK(giochi_layout_load(&layout, dir, "layout.json", err, sizeof(err)) ==
          0);
    CHECK(layout.tab_count == 1);
    CHECK_STR_EQ(giochi_layout_tab_id(&layout, 0), "home");
    unlink((std::string(dir) + "/layout.json").c_str());
    rmdir(dir);
}

}  // namespace

int main(void)
{
    test_defaults();
    test_wrap();
    test_load_assets();
    test_invalid_falls_back();
    test_unknown_tab_skipped();
    TEST_SUMMARY("test_layout");
}
