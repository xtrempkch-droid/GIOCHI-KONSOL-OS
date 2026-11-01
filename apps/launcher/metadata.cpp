#include "metadata.hpp"

#include "giochi_log.h"
#include "giochi_util.h"

#include <cstdlib>
#include <cstring>
#include <string>

#ifndef GIOCHI_ENABLE_PROTON
#define GIOCHI_ENABLE_PROTON 0
#endif

namespace giochi {

bool device_proton_supported()
{
#if GIOCHI_ENABLE_PROTON
    return true;
#else
    return false;
#endif
}

const char *metadata_status_name(MetadataStatus status)
{
    switch (status) {
    case MetadataStatus::Ok:               return "ok";
    case MetadataStatus::InvalidJson:      return "invalid_json";
    case MetadataStatus::MissingField:     return "missing_field";
    case MetadataStatus::UnsupportedArch:  return "unsupported_arch";
    case MetadataStatus::UnsupportedRuntime: return "unsupported_runtime";
    case MetadataStatus::ProtonUnsupported: return "proton_unsupported";
    case MetadataStatus::BadEntrypoint:    return "bad_entrypoint";
    }
    return "unknown";
}

static std::string get_string(const gj_value *root, const char *key)
{
    const char *value = gj_as_string(gj_object_get(root, key), nullptr);

    return value != nullptr ? std::string(value) : std::string();
}

static bool entrypoint_is_safe(const std::string &entry)
{
    if (entry.empty() || entry[0] == '/') {
        return false;
    }
    if (entry.compare(0, 4, "bin/") != 0) {
        return false;
    }
    if (entry.find("..") != std::string::npos) {
        return false;
    }
    return true;
}

MetadataResult metadata_from_text(const std::string &json, bool proton_supported)
{
    MetadataResult result;
    char err[160] = {0};
    gj_value *root;

    root = gj_parse(json.c_str(), err, sizeof(err));
    if (root == nullptr) {
        result.status = MetadataStatus::InvalidJson;
        result.message = std::string("metadata.json invalido: ") + err;
        return result;
    }

    Metadata &m = result.metadata;
    m.title = get_string(root, "title");
    m.developer = get_string(root, "developer");
    m.version = get_string(root, "version");
    m.arch = get_string(root, "arch");
    m.runtime = get_string(root, "runtime");
    m.graphics_api = get_string(root, "graphics_api");
    m.graphics_api_fallback = get_string(root, "graphics_api_fallback");
    m.buttons_map = get_string(root, "buttons_map");
    m.entrypoint = get_string(root, "entrypoint");

    if (m.arch.empty()) {
        m.arch = "aarch64";
    }
    if (m.runtime.empty()) {
        m.runtime = "godot";
    }

    if (m.title.empty() || m.entrypoint.empty()) {
        result.status = MetadataStatus::MissingField;
        result.message = "metadata.json sem 'title' ou 'entrypoint'";
        gj_free(root);
        return result;
    }

    if (m.runtime != "godot" && m.runtime != "retroarch" &&
        m.runtime != "proton") {
        result.status = MetadataStatus::UnsupportedRuntime;
        result.message = "runtime desconhecido: " + m.runtime;
        gj_free(root);
        return result;
    }

    if (m.arch != "aarch64") {
        result.status = MetadataStatus::UnsupportedArch;
        result.message = "arquitetura incompativel: " + m.arch +
                         " (esperado aarch64)";
        gj_free(root);
        return result;
    }

    if (m.runtime == "proton" && !proton_supported) {
        result.status = MetadataStatus::ProtonUnsupported;
        result.message = "runtime 'proton' nao suportado neste dispositivo";
        gj_free(root);
        return result;
    }

    if (!entrypoint_is_safe(m.entrypoint)) {
        result.status = MetadataStatus::BadEntrypoint;
        result.message = "entrypoint invalido: " + m.entrypoint;
        gj_free(root);
        return result;
    }

    result.status = MetadataStatus::Ok;
    gj_free(root);
    return result;
}

MetadataResult metadata_from_file(const std::string &path,
                                 bool proton_supported)
{
    MetadataResult result;
    size_t len = 0;
    char *text = giochi_read_file(path.c_str(), &len);

    if (text == nullptr) {
        result.status = MetadataStatus::InvalidJson;
        result.message = "metadata.json ilegivel: " + path;
        return result;
    }
    std::string json(text, len);
    free(text);
    return metadata_from_text(json, proton_supported);
}

}  // namespace giochi
