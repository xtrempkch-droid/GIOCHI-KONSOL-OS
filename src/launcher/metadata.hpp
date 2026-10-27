/* Giochi Konsol OS v1.2 — metadata.json de pacotes .giochi (secao 4.2). */
#ifndef GIOCHI_METADATA_HPP
#define GIOCHI_METADATA_HPP

#include <string>
#include <vector>

#include "giochi_json.h"

namespace giochi {

struct Metadata {
    std::string title;
    std::string developer;
    std::string version;
    std::string arch;
    std::string runtime;
    std::string graphics_api;
    std::string graphics_api_fallback;
    std::string buttons_map;
    std::string entrypoint;
};

enum class MetadataStatus {
    Ok = 0,
    InvalidJson,
    MissingField,
    UnsupportedArch,
    UnsupportedRuntime,
    ProtonUnsupported,
    BadEntrypoint
};

struct MetadataResult {
    MetadataStatus status = MetadataStatus::InvalidJson;
    Metadata metadata;
    std::string message;

    bool ok() const { return status == MetadataStatus::Ok; }
};

/* Parse + validacao (arch aarch64; proton rejeitado se !proton_supported). */
MetadataResult metadata_from_text(const std::string &json,
                                 bool proton_supported);
MetadataResult metadata_from_file(const std::string &path,
                                 bool proton_supported);

/* Configuracao compilada do device (device_profile.proton.supported). */
bool device_proton_supported();

const char *metadata_status_name(MetadataStatus status);

}  // namespace giochi

#endif /* GIOCHI_METADATA_HPP */
