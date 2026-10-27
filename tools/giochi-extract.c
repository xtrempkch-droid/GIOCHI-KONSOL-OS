/* Giochi Konsol OS v1.2 — giochi-extract (S9, ADR-11).
 *
 * Extrai APENAS metadata.json, capa.png, thumb.png e bin/ de um pacote .giochi
 * (tar puro) para /run/giochi/<sha256>/. Streaming: nunca carrega o pacote
 * inteiro em memoria nem extrai para a particao C.
 *
 * Dois backends:
 *   - libarchive (GIOCHI_HAVE_LIBARCHIVE), quando disponivel no Buildroot;
 *   - leitor ustar embutido (sempre presente), para que a instalacao nunca
 *     dependa de uma biblioteca externa (ADR-11: tar puro e o formato).
 *
 * Exit codes: 0 ok | 1 erro | 2 uso.
 */
#include "giochi_log.h"
#include "giochi_sha256.h"
#include "giochi_util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define GIOCHI_EXIT_OK 0
#define GIOCHI_EXIT_ERROR 1
#define GIOCHI_EXIT_USAGE 2

#define TAR_BLOCK 512
#define MAX_ENTRY_BYTES (256UL * 1024UL * 1024UL)

static void usage(FILE *out)
{
    (void)fprintf(out,
        "uso: giochi-extract --package PATH [--out-dir DIR]\n"
        "  --package PATH   pacote .giochi (tar puro)\n"
        "  --out-dir DIR    destino (default: /run/giochi/<sha256>)\n");
}

/* Subconjunto de membros permitido pelo Launcher (secao 4.2). */
static int is_wanted(const char *path)
{
    return strcmp(path, "metadata.json") == 0 ||
           strcmp(path, "capa.png") == 0 ||
           strcmp(path, "thumb.png") == 0 ||
           strncmp(path, "bin/", 4) == 0;
}

static int path_is_safe(const char *path)
{
    if (path == NULL || path[0] == '\0' || path[0] == '/') {
        return 0;
    }
    if (strstr(path, "..") != NULL) {
        return 0;
    }
    return 1;
}

static int mkdir_parents(const char *full_path)
{
    char dir[1024];
    char *slash;

    giochi_strlcpy(dir, full_path, sizeof(dir));
    slash = strrchr(dir, '/');
    if (slash == NULL || slash == dir) {
        return 0;
    }
    *slash = '\0';
    return giochi_mkdir_p(dir, 0755);
}

#ifdef GIOCHI_HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>

static int extract_with_libarchive(const char *package, const char *out_dir)
{
    struct archive *ar = archive_read_new();
    struct archive *writer = archive_write_disk_new();
    struct archive_entry *entry;
    int rc;
    int status = GIOCHI_EXIT_OK;

    archive_read_support_filter_all(ar);
    archive_read_support_format_tar(ar);
    archive_write_disk_set_options(writer, ARCHIVE_EXTRACT_TIME |
                                           ARCHIVE_EXTRACT_PERM |
                                           ARCHIVE_EXTRACT_SECURE_NODOTDOT);

    if (archive_read_open_filename(ar, package, 64 * 1024) != ARCHIVE_OK) {
        GLOG_ERR("extract: %s", archive_error_string(ar));
        archive_read_free(ar);
        archive_write_free(writer);
        return GIOCHI_EXIT_ERROR;
    }

    while ((rc = archive_read_next_header(ar, &entry)) == ARCHIVE_OK) {
        const char *path = archive_entry_pathname(entry);
        char full[1024];

        if (path == NULL || !is_wanted(path) || !path_is_safe(path)) {
            archive_read_data_skip(ar);
            continue;
        }
        if (snprintf(full, sizeof(full), "%s/%s", out_dir, path) >=
            (int)sizeof(full)) {
            archive_read_data_skip(ar);
            continue;
        }
        archive_entry_set_pathname(entry, full);
        if (archive_write_header(writer, entry) != ARCHIVE_OK) {
            GLOG_ERR("extract: %s", archive_error_string(writer));
            status = GIOCHI_EXIT_ERROR;
            break;
        }

        {
            const void *block;
            size_t size;
            la_int64_t offset;

            while (archive_read_data_block(ar, &block, &size, &offset) ==
                   ARCHIVE_OK) {
                if (archive_write_data_block(writer, block, size, offset) !=
                    ARCHIVE_OK) {
                    GLOG_ERR("extract: %s", archive_error_string(writer));
                    status = GIOCHI_EXIT_ERROR;
                    break;
                }
            }
        }
        archive_write_finish_entry(writer);
        if (status != GIOCHI_EXIT_OK) {
            break;
        }
    }

    if (rc != ARCHIVE_EOF && status == GIOCHI_EXIT_OK) {
        GLOG_ERR("extract: tar truncado: %s", archive_error_string(ar));
        status = GIOCHI_EXIT_ERROR;
    }

    archive_read_free(ar);
    archive_write_free(writer);
    return status;
}
#endif /* GIOCHI_HAVE_LIBARCHIVE */

/* --- backend ustar embutido ------------------------------------------- */

static int tar_is_zero_block(const unsigned char *block)
{
    for (int i = 0; i < TAR_BLOCK; i++) {
        if (block[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static unsigned long long tar_parse_octal(const unsigned char *field, size_t len)
{
    unsigned long long value = 0;
    size_t i = 0;

    while (i < len && (field[i] == ' ' || field[i] == '\0')) {
        i++;
    }
    for (; i < len; i++) {
        if (field[i] < '0' || field[i] > '7') {
            break;
        }
        value = (value << 3) + (unsigned long long)(field[i] - '0');
    }
    return value;
}

static int tar_read_full(int fd, void *buf, size_t len)
{
    unsigned char *p = buf;
    size_t done = 0;

    while (done < len) {
        ssize_t n = read(fd, p + done, len - done);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return done == 0 ? 0 : -1; /* 0 = EOF limpo */
        }
        done += (size_t)n;
    }
    return 1;
}

static int tar_skip(int fd, unsigned long long bytes)
{
    unsigned char scratch[4096];
    unsigned long long left = bytes;

    while (left > 0) {
        size_t chunk = left > sizeof(scratch) ? sizeof(scratch)
                                              : (size_t)left;
        int rc = tar_read_full(fd, scratch, chunk);

        if (rc <= 0) {
            return -1;
        }
        left -= chunk;
    }
    return 0;
}

static int write_full(int fd, const unsigned char *buf, size_t len)
{
    size_t done = 0;

    while (done < len) {
        ssize_t n = write(fd, buf + done, len - done);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        done += (size_t)n;
    }
    return 0;
}

static int tar_copy(int fd_in, int fd_out, unsigned long long bytes)
{
    unsigned char buf[8192];
    unsigned long long left = bytes;

    while (left > 0) {
        size_t chunk = left > sizeof(buf) ? sizeof(buf) : (size_t)left;
        int rc = tar_read_full(fd_in, buf, chunk);

        if (rc <= 0) {
            return -1;
        }
        if (write_full(fd_out, buf, chunk) != 0) {
            return -1;
        }
        left -= chunk;
    }
    return 0;
}

static int extract_with_ustar(const char *package, const char *out_dir)
{
    unsigned char header[TAR_BLOCK];
    char output[1024];
    char name[256];
    int fd;
    int status = GIOCHI_EXIT_OK;
    int entries = 0;

    fd = open(package, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        GLOG_ERR("extract: open(%s): %s", package, strerror(errno));
        return GIOCHI_EXIT_ERROR;
    }

    for (;;) {
        unsigned long long size;
        char typeflag;
        int rc;
        int wanted;

        rc = tar_read_full(fd, header, TAR_BLOCK);
        if (rc == 0) {
            break; /* EOF */
        }
        if (rc < 0) {
            GLOG_ERR("extract: tar truncado");
            status = GIOCHI_EXIT_ERROR;
            break;
        }
        if (tar_is_zero_block(header)) {
            break; /* fim do arquivo (bloco zero) */
        }

        /* ustar: name(100) + prefix(155) para caminhos longos. */
        giochi_strlcpy(name, (const char *)header, 101);
        if (memcmp(header + 257, "ustar", 5) == 0) {
            char prefix[156];
            char joined[420];

            giochi_strlcpy(prefix, (const char *)header + 345, 156);
            if (prefix[0] != '\0') {
                if (snprintf(joined, sizeof(joined), "%s/%s", prefix, name) <
                    (int)sizeof(joined)) {
                    giochi_strlcpy(name, joined, sizeof(name));
                } else {
                    GLOG_WARN("extract: nome ustar longo demais; ignorado");
                    name[0] = '\0';
                }
            }
        }

        size = tar_parse_octal(header + 124, 12);
        typeflag = (char)header[156];

        if (size > MAX_ENTRY_BYTES) {
            GLOG_ERR("extract: membro '%s' grande demais (%llu bytes)", name,
                     size);
            status = GIOCHI_EXIT_ERROR;
            break;
        }

        wanted = (typeflag == '0' || typeflag == '\0') && is_wanted(name) &&
                 path_is_safe(name);

        if (!wanted) {
            unsigned long long pad = (TAR_BLOCK - (size % TAR_BLOCK)) % TAR_BLOCK;

            if (tar_skip(fd, size + pad) != 0) {
                GLOG_ERR("extract: tar truncado em '%s'", name);
                status = GIOCHI_EXIT_ERROR;
                break;
            }
            continue;
        }

        if (snprintf(output, sizeof(output), "%s/%s", out_dir, name) >=
            (int)sizeof(output)) {
            GLOG_WARN("extract: caminho longo demais '%s'; ignorado", name);
            status = GIOCHI_EXIT_ERROR;
            break;
        }
        if (mkdir_parents(output) != 0) {
            GLOG_ERR("extract: nao foi possivel criar o diretorio de '%s'",
                     output);
            status = GIOCHI_EXIT_ERROR;
            break;
        }

        {
            int out = open(output, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                           0755);

            if (out < 0) {
                GLOG_ERR("extract: open(%s): %s", output, strerror(errno));
                status = GIOCHI_EXIT_ERROR;
                break;
            }
            if (tar_copy(fd, out, size) != 0) {
                GLOG_ERR("extract: escrita incompleta em '%s'", output);
                (void)close(out);
                status = GIOCHI_EXIT_ERROR;
                break;
            }
            if (fsync(out) != 0) {
                GLOG_WARN("extract: fsync(%s) falhou", output);
            }
            (void)close(out);
        }

        {
            unsigned long long pad = (TAR_BLOCK - (size % TAR_BLOCK)) % TAR_BLOCK;

            if (tar_skip(fd, pad) != 0) {
                GLOG_ERR("extract: tar truncado apos '%s'", name);
                status = GIOCHI_EXIT_ERROR;
                break;
            }
        }
        entries++;
    }

    (void)close(fd);

    if (status == GIOCHI_EXIT_OK && entries == 0) {
        GLOG_ERR("extract: nenhum membro esperado encontrado (metadata.json, "
                 "capa.png, thumb.png, bin/)");
        return GIOCHI_EXIT_ERROR;
    }
    return status;
}

int main(int argc, char **argv)
{
    const char *package = NULL;
    const char *out_dir = NULL;
    unsigned char digest[GIOCHI_SHA256_DIGEST_SIZE];
    char hex[GIOCHI_SHA256_HEX_SIZE];
    char derived[1024];
    int rc;

    giochi_log_set_level(giochi_log_level_from_env());

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return GIOCHI_EXIT_OK;
        }
        if (strcmp(argv[i], "--package") == 0 && i + 1 < argc) {
            package = argv[++i];
        } else if (strcmp(argv[i], "--out-dir") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
        } else {
            (void)fprintf(stderr, "giochi-extract: opcao invalida '%s'\n",
                          argv[i]);
            return GIOCHI_EXIT_USAGE;
        }
    }
    if (package == NULL) {
        usage(stderr);
        return GIOCHI_EXIT_USAGE;
    }

    if (giochi_sha256_file(package, digest) != 0) {
        GLOG_ERR("extract: pacote ilegivel: %s", package);
        return GIOCHI_EXIT_ERROR;
    }
    giochi_sha256_hex(digest, hex);

    if (out_dir == NULL) {
        char run_dir[512];
        size_t base;

        if (giochi_path(run_dir, sizeof(run_dir), "/run/giochi/") != 0) {
            return GIOCHI_EXIT_ERROR;
        }
        base = strlen(run_dir);
        if (snprintf(derived, sizeof(derived), "%s%.16s", run_dir, hex) >=
            (int)sizeof(derived)) {
            return GIOCHI_EXIT_ERROR;
        }
        (void)base;
        out_dir = derived;
    }

    if (giochi_mkdir_p(out_dir, 0755) != 0) {
        GLOG_ERR("extract: nao foi possivel criar %s", out_dir);
        return GIOCHI_EXIT_ERROR;
    }

#ifdef GIOCHI_HAVE_LIBARCHIVE
    rc = extract_with_libarchive(package, out_dir);
#else
    rc = extract_with_ustar(package, out_dir);
#endif

    if (rc == GIOCHI_EXIT_OK) {
        GLOG_INFO("extract: %s -> %s", package, out_dir);
    }
    return rc;
}
