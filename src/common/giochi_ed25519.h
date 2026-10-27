/* Giochi Konsol OS v1.2 — verificacao Ed25519 de imagens de update (ADR-12).
 *
 * DECISAO: a verificacao usa OpenSSL (libcrypto) quando disponivel
 * (GIOCHI_HAVE_OPENSSL). Sem OpenSSL o binario responde NOT_COMPILED e
 * S05update aborta o update — nunca aceita imagem nao verificada.
 */
#ifndef GIOCHI_ED25519_H
#define GIOCHI_ED25519_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    GIOCHI_SIG_OK = 0,
    GIOCHI_SIG_MISMATCH = 1,
    GIOCHI_SIG_BAD_ARGUMENT = 2,
    GIOCHI_SIG_NOT_COMPILED = 3
};

/* payload: arquivo assinado; sig: 64 bytes; pubkey: 32 bytes (raw). */
int giochi_ed25519_verify_file(const char *payload_path,
                               const unsigned char *sig, size_t sig_len,
                               const unsigned char *pubkey, size_t pubkey_len);

/* Le chave publica raw (32 bytes) de /etc/giochi/pubkey.ed25519. */
int giochi_ed25519_load_pubkey(const char *path, unsigned char *out,
                               size_t outsz);

#ifdef __cplusplus
}
#endif

#endif /* GIOCHI_ED25519_H */
