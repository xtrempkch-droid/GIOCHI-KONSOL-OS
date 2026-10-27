/* Giochi Konsol OS v1.2 — SHA-256 (S9 thumb cache key, S12 update verify). */
#ifndef GIOCHI_SHA256_H
#define GIOCHI_SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GIOCHI_SHA256_DIGEST_SIZE 32
#define GIOCHI_SHA256_HEX_SIZE 65

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t buffer[64];
    size_t buffer_len;
} giochi_sha256;

void giochi_sha256_init(giochi_sha256 *ctx);
void giochi_sha256_update(giochi_sha256 *ctx, const void *data, size_t len);
void giochi_sha256_final(giochi_sha256 *ctx, uint8_t out[GIOCHI_SHA256_DIGEST_SIZE]);

void giochi_sha256_hex(const uint8_t digest[GIOCHI_SHA256_DIGEST_SIZE],
                       char out[GIOCHI_SHA256_HEX_SIZE]);

/* Hash de arquivo em streaming (nao carrega o arquivo inteiro). 0/-1. */
int giochi_sha256_file(const char *path,
                       uint8_t out[GIOCHI_SHA256_DIGEST_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* GIOCHI_SHA256_H */
