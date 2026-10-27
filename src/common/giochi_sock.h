/* Giochi Konsol OS v1.2 — helpers de socket UNIX (control.sock, music.sock). */
#ifndef GIOCHI_SOCK_H
#define GIOCHI_SOCK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Cria, faz bind e listen em um socket UNIX de stream. Remove o no antes. */
int giochi_sock_listen_unix(const char *path, int backlog, char *err,
                            size_t errsz);

/* Le uma linha (sem \r\n). Retorna bytes lidos, 0 em timeout/EOF, -1 em erro. */
int giochi_sock_read_line(int fd, char *buf, size_t bufsz, int timeout_ms);

int giochi_sock_write_all(int fd, const char *data, size_t len);

/* Conecta a um socket UNIX. Retorna fd ou -1. */
int giochi_sock_connect_unix(const char *path, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* GIOCHI_SOCK_H */
