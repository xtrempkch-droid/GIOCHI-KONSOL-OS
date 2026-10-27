/* Giochi Konsol OS v1.2 — hotkey de saida do jogo via evdev.
 *
 * O supervisor observa BTN_MODE + BTN_START (>=500 ms) e encerra o jogo com
 * SIGTERM, escalando para SIGKILL depois de 3 s (secao 4.1.1 / S4).
 */
#ifndef GIOCHI_INIT_HOTKEY_H
#define GIOCHI_INIT_HOTKEY_H

#include <poll.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GIOCHI_HOTKEY_MAX_FDS 8

typedef struct {
    int fds[GIOCHI_HOTKEY_MAX_FDS];
    int count;
    void *state; /* giochi_hotkey_state, opaco neste header */
} giochi_hotkey_device;

/* 0 = ao menos um device aberto; -1 = nenhum (nao e fatal). */
int giochi_hotkey_open(giochi_hotkey_device *dev, char *err, size_t errsz);
void giochi_hotkey_close(giochi_hotkey_device *dev);

/* Preenche pfds[offset..] com os fds observados. Retorna quantos adicionou. */
int giochi_hotkey_pollfds(giochi_hotkey_device *dev, struct pollfd *pfds,
                          int offset, int max);

/* Le os eventos prontos. Retorna 1 se o combo de saida disparou. */
int giochi_hotkey_process(giochi_hotkey_device *dev, struct pollfd *pfds,
                          int offset, int count, unsigned long long now_ms);

#ifdef __cplusplus
}
#endif

#endif /* GIOCHI_INIT_HOTKEY_H */
