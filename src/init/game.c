#include "game.h"

#include "giochi_json.h"
#include "giochi_log.h"
#include "giochi_sha256.h"
#include "giochi_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef GIOCHI_LIBEXECDIR
#define GIOCHI_LIBEXECDIR "/usr/bin"
#endif

#define GIOCHI_GAME_EXTRACT_TIMEOUT_MS 15000

static int ends_with(const char *text, const char *suffix)
{
    size_t lt = strlen(text);
    size_t ls = strlen(suffix);

    return lt >= ls && strcmp(text + lt - ls, suffix) == 0;
}

static int file_exists(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static const char *entrypoint_from(const char *json)
{
    static char entry[GIOCHI_MAX_ENTRY];
    char err[128];
    gj_value *doc;
    const char *value;

    doc = gj_parse(json, err, sizeof(err));
    if (doc == NULL) {
        GLOG_WARN("game: metadata inline invalido (%s)", err);
        return NULL;
    }
    value = gj_as_string(gj_object_get(doc, "entrypoint"), NULL);
    if (value == NULL || strncmp(value, "bin/", 4) != 0 ||
        strstr(value, "..") != NULL || value[0] == '/') {
        gj_free(doc);
        return NULL;
    }
    giochi_strlcpy(entry, value, sizeof(entry));
    gj_free(doc);
    return entry;
}

int giochi_game_prepare(const char *package, const char *metadata_json,
                        char *out, size_t outsz, char *err, size_t errsz)
{
    unsigned char digest[GIOCHI_SHA256_DIGEST_SIZE];
    char hex[GIOCHI_SHA256_HEX_SIZE];
    char extract_dir[GIOCHI_MAX_PATH];
    char extractor[GIOCHI_MAX_PATH];
    char meta_path[GIOCHI_MAX_PATH];
    char entry_path[GIOCHI_MAX_PATH];
    const char *entry = NULL;
    char *meta_text = NULL;
    size_t meta_len = 0;

    if (err != NULL && errsz > 0) {
        err[0] = '\0';
    }
    if (package == NULL || out == NULL || outsz == 0) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "pacote ou destino invalido");
        }
        return -1;
    }

    /* Nao e um pacote: executa direto (RetroArch/port ja extraido). */
    if (!ends_with(package, ".giochi")) {
        giochi_strlcpy(out, package, outsz);
        if (!file_exists(out)) {
            if (err != NULL && errsz > 0) {
                (void)snprintf(err, errsz, "binario inexistente: %s", out);
            }
            return -1;
        }
        return 0;
    }

    if (giochi_sha256_file(package, digest) != 0) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "pacote ilegivel: %s", package);
        }
        return -1;
    }
    giochi_sha256_hex(digest, hex);

    if (snprintf(extract_dir, sizeof(extract_dir), "/run/giochi/%.16s", hex)
            >= (int)sizeof(extract_dir)) {
        return -1;
    }
    if (giochi_root()[0] != '\0') {
        if (giochi_path(extract_dir, sizeof(extract_dir), "/run/giochi/") != 0) {
            return -1;
        }
        {
            size_t base = strlen(extract_dir);

            (void)snprintf(extract_dir + base, sizeof(extract_dir) - base,
                           "%.16s", hex);
        }
    }

    if (giochi_mkdir_p(extract_dir, 0755) != 0) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "nao foi possivel criar %s",
                           extract_dir);
        }
        return -1;
    }

    if (snprintf(meta_path, sizeof(meta_path), "%s/metadata.json", extract_dir)
            >= (int)sizeof(meta_path)) {
        return -1;
    }

    /* Extrai somente se ainda nao houver metadata (cache por sha256). */
    if (!file_exists(meta_path)) {
        const char *argv_extract[8];
        int rc;

        if (giochi_path(extractor, sizeof(extractor),
                        GIOCHI_LIBEXECDIR "/giochi-extract") != 0) {
            return -1;
        }
        argv_extract[0] = extractor;
        argv_extract[1] = "--package";
        argv_extract[2] = package;
        argv_extract[3] = "--out-dir";
        argv_extract[4] = extract_dir;
        argv_extract[5] = NULL;

        GLOG_INFO("game: extraindo %.16s de %s", hex, package);
        rc = giochi_run(argv_extract);
        if (rc != 0) {
            GLOG_WARN("game: giochi-extract retornou %d (dry-run em host?)", rc);
        }
    }

    if (metadata_json != NULL && *metadata_json != '\0') {
        entry = entrypoint_from(metadata_json);
    }
    if (entry == NULL) {
        meta_text = giochi_read_file(meta_path, &meta_len);
        if (meta_text != NULL) {
            entry = entrypoint_from(meta_text);
            free(meta_text);
        }
    }
    if (entry == NULL) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz,
                           "metadata.json sem entrypoint valido em %s",
                           extract_dir);
        }
        return -1;
    }

    if (snprintf(entry_path, sizeof(entry_path), "%s/%s", extract_dir, entry)
            >= (int)sizeof(entry_path)) {
        return -1;
    }
    if (!file_exists(entry_path)) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "entrypoint ausente apos extracao: %s",
                           entry_path);
        }
        return -1;
    }
    (void)chmod(entry_path, 0755);

    giochi_strlcpy(out, entry_path, outsz);
    return 0;
}
