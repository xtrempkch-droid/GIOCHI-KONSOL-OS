/* Giochi Konsol OS v1.2 — servidor de comandos em /run/giochi/control.sock.
 *
 * Protocolo: texto simples, uma linha por comando, resposta em uma linha
 * (secao 4.9.4 da especificacao).
 */
#ifndef GIOCHI_CONTROL_H
#define GIOCHI_CONTROL_H

#include <stddef.h>

typedef int (*giochi_control_handler)(const char *cmd, char *reply,
                                      size_t replysz, void *ctx);

typedef struct {
    int fd;
    int active;
    char path[128];
} giochi_control;

int giochi_control_open(giochi_control *ctl, const char *path);
void giochi_control_close(giochi_control *ctl);

/* Retorna 1 se atendeu um comando, 0 em timeout, -1 em erro fatal. */
int giochi_control_serve(giochi_control *ctl, giochi_control_handler handler,
                         void *ctx, int timeout_ms);

/* Atende uma conexao ja sinalizada como legivel por poll(). */
int giochi_control_accept(giochi_control *ctl, giochi_control_handler handler,
                          void *ctx);

#endif /* GIOCHI_CONTROL_H */
