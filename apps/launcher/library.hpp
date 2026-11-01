/* Giochi Konsol OS v1.2 — biblioteca de pacotes .giochi instalados. */
#ifndef GIOCHI_LIBRARY_HPP
#define GIOCHI_LIBRARY_HPP

#include <string>
#include <vector>

#include "metadata.hpp"

namespace giochi {

struct LibraryEntry {
    std::string package_path;
    std::string sha256;
    std::string title;
    std::string thumb_path; /* .rgba 152x85, vazio se ausente */
    Metadata metadata;
    bool valid = false;
    std::string error;
};

/* Lista os pacotes .giochi de <games_dir>. Metadados vem do cache do install
 * (<meta_cache_dir>/<sha256>.json); ausente => entrada invalida, ainda listada. */
std::vector<LibraryEntry> library_scan(const std::string &games_dir,
                                       const std::string &meta_cache_dir,
                                       const std::string &thumb_cache_dir,
                                       bool proton_supported);

}  // namespace giochi

#endif /* GIOCHI_LIBRARY_HPP */
