/* Giochi Konsol OS v1.2 — preparo do jogo (secoes 2.2 e 4.2).
 *
 * O Launcher envia o caminho do pacote .giochi + metadata.json inline pela
 * FIFO. O supervisor extrai APENAS bin/ (ADR-11, via giochi-extract) para
 * /run/giochi/<sha256>/ e resolve o entrypoint antes do fork/execve.
 */
#ifndef GIOCHI_GAME_H
#define GIOCHI_GAME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GIOCHI_MAX_PATH 512
#define GIOCHI_MAX_ENTRY 256

/* 0 = ok (out = binario a executar). -1 = erro (err preenchido). */
int giochi_game_prepare(const char *package, const char *metadata_json,
                        char *out, size_t outsz, char *err, size_t errsz);

#ifdef __cplusplus
}
#endif

#endif /* GIOCHI_GAME_H */
