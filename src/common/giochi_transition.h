/* Giochi Konsol OS v1.2 — FIFO /run/giochi/transition (S4).
 *
 * Struct binaria versionada: cmd + path + metadata JSON inline. Usada pelo
 * Launcher para pedir ao supervisor que lance um .giochi ou volte ao Launcher.
 */
#ifndef GIOCHI_TRANSITION_H
#define GIOCHI_TRANSITION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GIOCHI_TRANSITION_MAGIC 0x31435447U /* "GTC1" little-endian */
#define GIOCHI_TRANSITION_MAX_PATH 256
#define GIOCHI_TRANSITION_MAX_META 1024

typedef enum {
    GIOCHI_TRANS_LAUNCH = 1,
    GIOCHI_TRANS_OPEN_APP = 2,
    GIOCHI_TRANS_EXIT_GAME = 3,
    GIOCHI_TRANS_RETURN_HOME = 4
} giochi_transition_cmd;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t cmd;
    char path[GIOCHI_TRANSITION_MAX_PATH];
    char metadata[GIOCHI_TRANSITION_MAX_META];
} giochi_transition_msg;

/* Cria o FIFO (idempotente) e abre em O_RDWR|O_NONBLOCK. Retorna fd ou -1. */
int giochi_transition_open(const char *path);

/* Serializa e escreve a mensagem. 0/-1. */
int giochi_transition_send(int fd, const giochi_transition_msg *msg);

/* Le uma mensagem completa. 1 = lida, 0 = vazia, -1 = erro/formato invalido. */
int giochi_transition_recv(int fd, giochi_transition_msg *msg);

void giochi_transition_fill(giochi_transition_msg *msg,
                            giochi_transition_cmd cmd, const char *path,
                            const char *metadata);

#ifdef __cplusplus
}
#endif

#endif /* GIOCHI_TRANSITION_H */
