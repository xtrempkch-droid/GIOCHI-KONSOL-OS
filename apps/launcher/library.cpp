#include "library.hpp"

#include "giochi_log.h"
#include "giochi_sha256.h"
#include "giochi_util.h"

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace giochi {

static bool ends_with(const std::string &text, const char *suffix)
{
    size_t n = std::strlen(suffix);

    return text.size() >= n &&
           text.compare(text.size() - n, n, suffix) == 0;
}

std::vector<LibraryEntry> library_scan(const std::string &games_dir,
                                       const std::string &meta_cache_dir,
                                       const std::string &thumb_cache_dir,
                                       bool proton_supported)
{
    std::vector<LibraryEntry> entries;
    DIR *dir = opendir(games_dir.c_str());

    if (dir == nullptr) {
        GLOG_WARN("biblioteca: '%s' inacessivel", games_dir.c_str());
        return entries;
    }

    struct dirent *item;
    while ((item = readdir(dir)) != nullptr) {
        std::string name(item->d_name);

        if (name.empty() || name[0] == '.' || !ends_with(name, ".giochi")) {
            continue;
        }

        LibraryEntry entry;
        entry.package_path = games_dir + "/" + name;

        struct stat st;
        if (stat(entry.package_path.c_str(), &st) != 0 ||
            !S_ISREG(st.st_mode)) {
            continue;
        }

        unsigned char digest[GIOCHI_SHA256_DIGEST_SIZE];
        if (giochi_sha256_file(entry.package_path.c_str(), digest) == 0) {
            char hex[GIOCHI_SHA256_HEX_SIZE];
            giochi_sha256_hex(digest, hex);
            entry.sha256 = hex;
        }

        if (!entry.sha256.empty()) {
            std::string meta_path = meta_cache_dir + "/" + entry.sha256 + ".json";
            MetadataResult meta =
                metadata_from_file(meta_path, proton_supported);

            entry.valid = meta.ok();
            entry.error = meta.message;
            entry.metadata = meta.metadata;

            std::string thumb = thumb_cache_dir + "/" + entry.sha256 + ".rgba";
            if (stat(thumb.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                entry.thumb_path = thumb;
            }
        } else {
            entry.error = "falha ao calcular SHA-256";
        }

        if (entry.metadata.title.empty()) {
            entry.title = name.substr(0, name.size() - 7);
        } else {
            entry.title = entry.metadata.title;
        }

        entries.push_back(entry);
    }
    (void)closedir(dir);

    std::sort(entries.begin(), entries.end(),
              [](const LibraryEntry &a, const LibraryEntry &b) {
                  return a.title < b.title;
              });

    GLOG_INFO("biblioteca: %zu pacotes em %s", entries.size(),
              games_dir.c_str());
    return entries;
}

}  // namespace giochi
