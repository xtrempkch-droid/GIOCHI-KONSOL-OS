/* Giochi Konsol OS v1.2 — giochi-verify (S12, ADR-12).
 *
 * Valida uma imagem de update: assinatura Ed25519 + SHA-256 declarado no
 * manifesto. Usado por S05update antes de escrever no slot inativo.
 *
 * Exit codes: 0 ok | 1 invalido | 2 uso/IO | 3 nao compilado sem OpenSSL.
 */
#include "giochi_ed25519.h"
#include "giochi_json.h"
#include "giochi_log.h"
#include "giochi_sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GIOCHI_EXIT_OK 0
#define GIOCHI_EXIT_INVALID 1
#define GIOCHI_EXIT_USAGE 2

static void usage(FILE *out)
{
    (void)fprintf(out,
        "uso: giochi-verify --image PATH --sig PATH --pubkey PATH "
        "[--manifest PATH]\n"
        "  --image PATH     imagem .img assinada\n"
        "  --sig PATH       assinatura Ed25519 crua (64 bytes)\n"
        "  --pubkey PATH    chave publica crua (32 bytes)\n"
        "  --manifest PATH  manifesto JSON com {\"sha256\":\"...\"}\n");
}

static int load_expected_sha(const char *manifest, char *out, size_t outsz)
{
    char err[160];
    gj_value *doc;
    const char *value;

    doc = gj_parse_file(manifest, err, sizeof(err));
    if (doc == NULL) {
        (void)fprintf(stderr, "giochi-verify: manifesto: %s\n", err);
        return -1;
    }
    value = gj_as_string(gj_object_get(doc, "sha256"), NULL);
    if (value == NULL || strlen(value) != 64) {
        (void)fprintf(stderr, "giochi-verify: manifesto sem 'sha256' valido\n");
        gj_free(doc);
        return -1;
    }
    snprintf(out, outsz, "%s", value);
    gj_free(doc);
    return 0;
}

int main(int argc, char **argv)
{
    const char *image = NULL;
    const char *sig_path = NULL;
    const char *pubkey_path = NULL;
    const char *manifest = NULL;
    unsigned char sig[128];
    unsigned char pubkey[64];
    unsigned char digest[GIOCHI_SHA256_DIGEST_SIZE];
    char actual_hex[GIOCHI_SHA256_HEX_SIZE];
    char expected_hex[GIOCHI_SHA256_HEX_SIZE];
    char bound_hex[GIOCHI_SHA256_HEX_SIZE];
    char bound_sig[256];
    FILE *fp;
    size_t sig_len;
    size_t pk_len;
    int rc;

    giochi_log_set_level(giochi_log_level_from_env());

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return GIOCHI_EXIT_OK;
        }
        if (strcmp(argv[i], "--image") == 0 && i + 1 < argc) {
            image = argv[++i];
        } else if (strcmp(argv[i], "--sig") == 0 && i + 1 < argc) {
            sig_path = argv[++i];
        } else if (strcmp(argv[i], "--pubkey") == 0 && i + 1 < argc) {
            pubkey_path = argv[++i];
        } else if (strcmp(argv[i], "--manifest") == 0 && i + 1 < argc) {
            manifest = argv[++i];
        } else {
            (void)fprintf(stderr, "giochi-verify: opcao invalida '%s'\n",
                          argv[i]);
            return GIOCHI_EXIT_USAGE;
        }
    }

    if (image == NULL || sig_path == NULL || pubkey_path == NULL) {
        usage(stderr);
        return GIOCHI_EXIT_USAGE;
    }

    fp = fopen(sig_path, "rb");
    if (fp == NULL) {
        (void)fprintf(stderr, "giochi-verify: assinatura ilegivel\n");
        return GIOCHI_EXIT_USAGE;
    }
    sig_len = fread(sig, 1, sizeof(sig), fp);
    (void)fclose(fp);

    if (giochi_ed25519_load_pubkey(pubkey_path, pubkey, sizeof(pubkey)) != 0) {
        (void)fprintf(stderr, "giochi-verify: chave publica invalida "
                              "(esperado 32 bytes)\n");
        return GIOCHI_EXIT_USAGE;
    }
    pk_len = 32;

    if (manifest != NULL) {
        if (load_expected_sha(manifest, expected_hex,
                              sizeof(expected_hex)) != 0) {
            return GIOCHI_EXIT_INVALID;
        }
        if (giochi_sha256_file(image, digest) != 0) {
            (void)fprintf(stderr, "giochi-verify: imagem ilegivel\n");
            return GIOCHI_EXIT_USAGE;
        }
        giochi_sha256_hex(digest, actual_hex);
        if (strcmp(actual_hex, expected_hex) != 0) {
            (void)fprintf(stderr, "giochi-verify: SHA-256 divergente\n"
                                  "  esperado %s\n  obtido   %s\n",
                          expected_hex, actual_hex);
            return GIOCHI_EXIT_INVALID;
        }
    }

    rc = giochi_ed25519_verify_file(image, sig, sig_len, pubkey, pk_len);
    if (rc == GIOCHI_SIG_OK) {
        (void)printf("OK\n");
        return GIOCHI_EXIT_OK;
    }
    if (rc == GIOCHI_SIG_NOT_COMPILED) {
        (void)fprintf(stderr,
            "giochi-verify: binario sem OpenSSL; update recusado\n");
        return 3;
    }

    (void)fprintf(stderr, "giochi-verify: assinatura invalida");
    if (giochi_sha256_file(sig_path, digest) == 0) {
        giochi_sha256_hex(digest, bound_hex);
        (void)snprintf(bound_sig, sizeof(bound_sig), " (sig sha256 %s)",
                       bound_hex);
        (void)fprintf(stderr, "%s", bound_sig);
    }
    (void)fprintf(stderr, "\n");
    return GIOCHI_EXIT_INVALID;
}
