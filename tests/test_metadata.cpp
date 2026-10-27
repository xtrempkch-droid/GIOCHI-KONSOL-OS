#include "metadata.hpp"
#include "test_util.h"

#include <string>

namespace {

const char *kValid =
    "{\"title\":\"Meu Jogo\",\"developer\":\"Studio X\",\"version\":\"1.0.0\","
    "\"arch\":\"aarch64\",\"runtime\":\"godot\",\"graphics_api\":\"gles3\","
    "\"graphics_api_fallback\":\"gles2\",\"buttons_map\":\"default\","
    "\"entrypoint\":\"bin/jogo.arm64\"}";

void test_valid(void)
{
    auto r = giochi::metadata_from_text(kValid, false);

    CHECK(r.ok());
    CHECK_STR_EQ(r.metadata.title.c_str(), "Meu Jogo");
    CHECK_STR_EQ(r.metadata.arch.c_str(), "aarch64");
    CHECK_STR_EQ(r.metadata.entrypoint.c_str(), "bin/jogo.arm64");
}

void test_arch_rejected(void)
{
    std::string json = kValid;

    json.replace(json.find("\"aarch64\""), 9, "\"x86_64\"");
    auto r = giochi::metadata_from_text(json, false);

    CHECK(!r.ok());
    CHECK(r.status == giochi::MetadataStatus::UnsupportedArch);
    CHECK(r.message.find("x86_64") != std::string::npos);
}

void test_proton_rejected(void)
{
    std::string json = kValid;

    json.replace(json.find("\"godot\""), 7, "\"proton\"");
    auto unsupported = giochi::metadata_from_text(json, false);
    CHECK(!unsupported.ok());
    CHECK(unsupported.status == giochi::MetadataStatus::ProtonUnsupported);

    auto supported = giochi::metadata_from_text(json, true);
    CHECK(supported.ok());
}

void test_bad_entrypoint(void)
{
    const char *cases[] = {
        "\"entrypoint\":\"../../etc/passwd\"}",
        "\"entrypoint\":\"/bin/sh\"}",
        "\"entrypoint\":\"jogo.arm64\"}"
    };

    for (const char *suffix : cases) {
        std::string json =
            "{\"title\":\"X\",\"arch\":\"aarch64\",\"runtime\":\"godot\",";
        json += suffix;
        auto r = giochi::metadata_from_text(json, false);

        CHECK(!r.ok());
        CHECK(r.status == giochi::MetadataStatus::BadEntrypoint);
    }
}

void test_invalid_json_and_missing_fields(void)
{
    auto bad = giochi::metadata_from_text("{oops", false);

    CHECK(bad.status == giochi::MetadataStatus::InvalidJson);

    auto missing = giochi::metadata_from_text(
        "{\"arch\":\"aarch64\",\"entrypoint\":\"bin/x\"}", false);
    CHECK(missing.status == giochi::MetadataStatus::MissingField);
}

void test_proton_build_flag(void)
{
    CHECK(giochi::device_proton_supported() == false);
}

}  // namespace

int main(void)
{
    test_valid();
    test_arch_rejected();
    test_proton_rejected();
    test_bad_entrypoint();
    test_invalid_json_and_missing_fields();
    test_proton_build_flag();
    TEST_SUMMARY("test_metadata");
}
