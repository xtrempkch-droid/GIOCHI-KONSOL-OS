#include "giochi_sha256.h"

#include <stdio.h>
#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_transform(giochi_sha256 *ctx, const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24)
             | ((uint32_t)block[i * 4 + 1] << 16)
             | ((uint32_t)block[i * 4 + 2] << 8)
             | (uint32_t)block[i * 4 + 3];
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = ROTR(w[i - 15], 7) ^ ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROTR(w[i - 2], 17) ^ ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        uint32_t s1 = ROTR(e, 6) ^ ROTR(e, 11) ^ ROTR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + s1 + ch + K[i] + w[i];
        uint32_t s0 = ROTR(a, 2) ^ ROTR(a, 13) ^ ROTR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void giochi_sha256_init(giochi_sha256 *ctx)
{
    ctx->state[0] = 0x6a09e667U;
    ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U;
    ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU;
    ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU;
    ctx->state[7] = 0x5be0cd19U;
    ctx->bitlen = 0;
    ctx->buffer_len = 0;
}

void giochi_sha256_update(giochi_sha256 *ctx, const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;

    if (data == NULL || len == 0) {
        return;
    }
    ctx->bitlen += (uint64_t)len * 8U;

    if (ctx->buffer_len != 0) {
        size_t room = 64 - ctx->buffer_len;
        size_t take = len < room ? len : room;

        memcpy(ctx->buffer + ctx->buffer_len, bytes, take);
        ctx->buffer_len += take;
        bytes += take;
        len -= take;
        if (ctx->buffer_len == 64) {
            sha256_transform(ctx, ctx->buffer);
            ctx->buffer_len = 0;
        }
    }

    while (len >= 64) {
        sha256_transform(ctx, bytes);
        bytes += 64;
        len -= 64;
    }
    if (len != 0) {
        memcpy(ctx->buffer, bytes, len);
        ctx->buffer_len = len;
    }
}

void giochi_sha256_final(giochi_sha256 *ctx, uint8_t out[GIOCHI_SHA256_DIGEST_SIZE])
{
    uint64_t bitlen = ctx->bitlen;
    size_t i;

    ctx->buffer[ctx->buffer_len++] = 0x80U;
    if (ctx->buffer_len > 56) {
        while (ctx->buffer_len < 64) {
            ctx->buffer[ctx->buffer_len++] = 0x00U;
        }
        sha256_transform(ctx, ctx->buffer);
        ctx->buffer_len = 0;
    }
    while (ctx->buffer_len < 56) {
        ctx->buffer[ctx->buffer_len++] = 0x00U;
    }
    for (i = 0; i < 8; i++) {
        ctx->buffer[56 + i] = (uint8_t)((bitlen >> (56 - 8 * i)) & 0xFFU);
    }
    sha256_transform(ctx, ctx->buffer);

    for (i = 0; i < 8; i++) {
        out[i * 4]     = (uint8_t)((ctx->state[i] >> 24) & 0xFFU);
        out[i * 4 + 1] = (uint8_t)((ctx->state[i] >> 16) & 0xFFU);
        out[i * 4 + 2] = (uint8_t)((ctx->state[i] >> 8) & 0xFFU);
        out[i * 4 + 3] = (uint8_t)(ctx->state[i] & 0xFFU);
    }
}

void giochi_sha256_hex(const uint8_t digest[GIOCHI_SHA256_DIGEST_SIZE],
                       char out[GIOCHI_SHA256_HEX_SIZE])
{
    static const char hex[] = "0123456789abcdef";
    int i;

    for (i = 0; i < GIOCHI_SHA256_DIGEST_SIZE; i++) {
        out[i * 2] = hex[(digest[i] >> 4) & 0x0FU];
        out[i * 2 + 1] = hex[digest[i] & 0x0FU];
    }
    out[GIOCHI_SHA256_DIGEST_SIZE * 2] = '\0';
}

int giochi_sha256_file(const char *path, uint8_t out[GIOCHI_SHA256_DIGEST_SIZE])
{
    giochi_sha256 ctx;
    unsigned char buf[16384];
    FILE *fp;
    size_t got;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }
    giochi_sha256_init(&ctx);
    while ((got = fread(buf, 1, sizeof(buf), fp)) > 0) {
        giochi_sha256_update(&ctx, buf, got);
    }
    if (ferror(fp) != 0) {
        (void)fclose(fp);
        return -1;
    }
    (void)fclose(fp);
    giochi_sha256_final(&ctx, out);
    return 0;
}
