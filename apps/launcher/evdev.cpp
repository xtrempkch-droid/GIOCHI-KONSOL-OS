#include "evdev.hpp"

#include "giochi_log.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#if defined(__has_include)
#  if __has_include(<linux/input.h>)
#    include <linux/input.h>
#    define GIOCHI_HAVE_LINUX_INPUT 1
#  endif
#endif

#if !defined(GIOCHI_HAVE_LINUX_INPUT)
#  define EV_KEY 0x01
#  define BTN_A 0x130
#  define BTN_B 0x131
#  define BTN_X 0x133
#  define BTN_Y 0x134
#  define BTN_TL 0x136
#  define BTN_TR 0x137
#  define BTN_SELECT 0x13a
#  define BTN_START 0x13b
#  define BTN_MODE 0x13c
#  define BTN_DPAD_UP 0x220
#  define BTN_DPAD_DOWN 0x221
#  define BTN_DPAD_LEFT 0x222
#  define BTN_DPAD_RIGHT 0x223

struct input_event {
    struct timeval time;
    unsigned short type;
    unsigned short code;
    int value;
};
#endif /* GIOCHI_HAVE_LINUX_INPUT */

#define GIOCHI_EVDEV_MAX 8
#define GIOCHI_EVDEV_SCAN 32

struct giochi_evdev {
    int fds[GIOCHI_EVDEV_MAX];
    int count;
};

/* Traduz o codigo evdev pelo mapa ativo (secao 2.2). Sem mapa => compilado. */
static int map_button(const giochi::InputState *map, unsigned short code,
                      giochi::Button *button)
{
    /* Sem mapa carregado, usa o mapa compilado (mesmos defaults). */
    static giochi::InputState fallback = []() {
        giochi::InputState state;

        giochi::input_reset(&state);
        return state;
    }();

    const giochi::InputState *active = map != nullptr ? map : &fallback;

    for (int i = 1; i < GIOCHI_BTN_CODE_COUNT; i++) {
        if (active->codes[i] != 0 && active->codes[i] == code) {
            *button = static_cast<giochi::Button>(i);
            return 1;
        }
    }
    return 0;
}

giochi_evdev *giochi_evdev_open(char *err, size_t errsz)
{
    giochi_evdev *dev;
    int i;

    if (err != NULL && errsz > 0) {
        err[0] = '\0';
    }

    dev = static_cast<giochi_evdev *>(calloc(1, sizeof(*dev)));
    if (dev == NULL) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "sem memoria");
        }
        return NULL;
    }
    for (i = 0; i < GIOCHI_EVDEV_MAX; i++) {
        dev->fds[i] = -1;
    }

    for (i = 0; i < GIOCHI_EVDEV_SCAN && dev->count < GIOCHI_EVDEV_MAX; i++) {
        char path[64];
        int fd;

        (void)snprintf(path, sizeof(path), "/dev/input/event%d", i);
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        GLOG_INFO("evdev: %s aberto", path);
        dev->fds[dev->count++] = fd;
    }

    if (dev->count == 0) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "nenhum /dev/input/event* disponivel");
        }
        free(dev);
        return NULL;
    }
    return dev;
}

void giochi_evdev_close(giochi_evdev *dev)
{
    int i;

    if (dev == NULL) {
        return;
    }
    for (i = 0; i < dev->count; i++) {
        if (dev->fds[i] >= 0) {
            (void)close(dev->fds[i]);
        }
    }
    free(dev);
}

int giochi_evdev_poll(giochi_evdev *dev, int timeout_ms,
                      const giochi::InputState *map,
                      giochi::Button *button, bool *pressed)
{
    struct pollfd pfds[GIOCHI_EVDEV_MAX];
    int rc;
    int i;

    if (dev == NULL || button == NULL || pressed == NULL) {
        return -1;
    }

    for (i = 0; i < dev->count; i++) {
        pfds[i].fd = dev->fds[i];
        pfds[i].events = POLLIN;
        pfds[i].revents = 0;
    }

    do {
        rc = poll(pfds, (nfds_t)dev->count, timeout_ms);
    } while (rc < 0 && errno == EINTR);

    if (rc < 0) {
        return -1;
    }
    if (rc == 0) {
        return 0;
    }

    for (i = 0; i < dev->count; i++) {
        if ((pfds[i].revents & POLLIN) == 0) {
            continue;
        }
        for (;;) {
            struct input_event event;
            ssize_t n = read(dev->fds[i], &event, sizeof(event));

            if (n < (ssize_t)sizeof(event)) {
                break;
            }
            if (event.type != EV_KEY || event.value == 2) {
                continue;
            }
            if (map_button(map, event.code, button)) {
                *pressed = event.value != 0;
                return 1;
            }
        }
    }
    return 0;
}
