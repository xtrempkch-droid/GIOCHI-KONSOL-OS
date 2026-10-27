/* Giochi Konsol OS v1.2 — utilidades POSIX compartilhadas.
 *
 * Todos os caminhos absolutos do sistema passam por giochi_path() para que os
 * testes de host possam redirecionar a raiz via GIOCHI_ROOT sem root/rede.
 */
#ifndef GIOCHI_UTIL_H
#define GIOCHI_UTIL_H

#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* "" no dispositivo; "<GIOCHI_ROOT>" nos testes de host. Nunca NULL. */
const char *giochi_root(void);

/* Copia root+suffix para buf. Retorna 0 em sucesso, -1 se truncou. */
int giochi_path(char *buf, size_t bufsz, const char *suffix);

int giochi_mkdir_p(const char *path, mode_t mode);

/* Le arquivo inteiro. Retorna buffer NUL-terminado (free() pelo chamador) ou
 * NULL. len_out recebe o tamanho em bytes (sem o NUL). */
char *giochi_read_file(const char *path, size_t *len_out);

/* Grava de forma atomica (tmp + fsync + rename). */
int giochi_write_file_atomic(const char *path, const char *data, size_t len);

unsigned long long giochi_now_ms(void);

/* GIOCHI_DRY_RUN=1 desliga efeitos colaterais de hardware (default em host). */
int giochi_is_dry_run(void);

/* fork+execvp+waitpid. Retorna exit code (>=0) ou -1. */
int giochi_run(const char *const argv[]);

/* Copia segura com truncamento. Sempre NUL-termina (se bufsz > 0). */
void giochi_strlcpy(char *dst, const char *src, size_t bufsz);

#ifdef __cplusplus
}
#endif

#endif /* GIOCHI_UTIL_H */
