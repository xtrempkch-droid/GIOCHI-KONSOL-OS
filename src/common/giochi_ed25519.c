#include "giochi_ed25519.h"

#include "giochi_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef GIOCHI_HAVE_OPENSSL

#include <openssl/evp.h>

int giochi_ed25519_verify_file(const char *payload_path,
                               const unsigned char *sig, size_t sig_len,
                               const unsigned char *pubkey, size_t pubkey_len)
{
    EVP_PKEY *key = NULL;
    EVP_MD_CTX *md = NULL;
    unsigned char buf[16384];
    FILE *fp;
    size_t got;
    int ok = 0;
    int rc = GIOCHI_SIG_MISMATCH;

    if (payload_path == NULL || sig == NULL || pubkey == NULL) {
        return GIOCHI_SIG_BAD_ARGUMENT;
    }
    if (sig_len != 64 || pubkey_len != 32) {
        return GIOCHI_SIG_BAD_ARGUMENT;
    }

    key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, pubkey,
                                      pubkey_len);
    md = EVP_MD_CTX_new();
    fp = fopen(payload_path, "rb");
    if (key == NULL || md == NULL || fp == NULL) {
        rc = GIOCHI_SIG_BAD_ARGUMENT;
        goto done;
    }
    if (EVP_DigestVerifyInit(md, NULL, NULL, NULL, key) != 1) {
        rc = GIOCHI_SIG_BAD_ARGUMENT;
        goto done;
    }

    /* Ed25519 e one-shot; o payload e carregado em blocos em um buffer
     * acumulado. TODO(v1.2): usar EVP_DigestVerify com streaming quando a
     * API do OpenSSL escolhido suportar. */
    {
        size_t cap = 1U << 20;
        size_t len = 0;
        unsigned char *whole = NULL;

        while ((got = fread(buf, 1, sizeof(buf), fp)) > 0) {
            if (len + got > cap) {
                unsigned char *grown = realloc(whole, cap * 2);

                if (grown == NULL) {
                    free(whole);
                    rc = GIOCHI_SIG_BAD_ARGUMENT;
                    goto done;
                }
                whole = grown;
                cap *= 2;
            }
            if (whole == NULL) {
                whole = malloc(cap);
                if (whole == NULL) {
                    rc = GIOCHI_SIG_BAD_ARGUMENT;
                    goto done;
                }
            }
            memcpy(whole + len, buf, got);
            len += got;
        }
        ok = EVP_DigestVerify(md, sig, sig_len, whole, len) == 1;
        free(whole);
    }
    rc = ok ? GIOCHI_SIG_OK : GIOCHI_SIG_MISMATCH;

done:
    if (fp != NULL) {
        (void)fclose(fp);
    }
    if (md != NULL) {
        EVP_MD_CTX_free(md);
    }
    if (key != NULL) {
        EVP_PKEY_free(key);
    }
    return rc;
}

#else /* !GIOCHI_HAVE_OPENSSL */

int giochi_ed25519_verify_file(const char *payload_path,
                               const unsigned char *sig, size_t sig_len,
                               const unsigned char *pubkey, size_t pubkey_len)
{
    (void)payload_path;
    (void)sig;
    (void)sig_len;
    (void)pubkey;
    (void)pubkey_len;
    GLOG_ERR("ed25519: binario compilado sem OpenSSL; update recusado");
    return GIOCHI_SIG_NOT_COMPILED;
}

#endif /* GIOCHI_HAVE_OPENSSL */

int giochi_ed25519_load_pubkey(const char *path, unsigned char *out,
                               size_t outsz)
{
    FILE *fp;
    size_t got;

    if (path == NULL || out == NULL || outsz < 32) {
        return -1;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }
    got = fread(out, 1, outsz, fp);
    (void)fclose(fp);
    return got == 32 ? 0 : -1;
}
