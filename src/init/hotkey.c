#include "hotkey.h"

#include "giochi_hotkey.h"
#include "giochi_log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__has_include)
#  if __has_include(<linux/input.h>)
#    include <linux/input.h>
#    define GIOCHI_HAVE_LINUX_INPUT 1
#  endif
#endif

#if !defined(GIOCHI_HAVE_LINUX_INPUT)
#  define EV_KEY 0x01
#  define BTN_START 0x13b
#  define BTN_MODE 0x13c

struct input_event {
    struct timeval time;
    unsigned short type;
    unsigned short code;
    int value;
};
#endif

#define GIOCHI_EVDEV_SCAN 32

int giochi_hotkey_open(giochi_hotkey_device *dev, char *err, size_t errsz)
{
    giochi_hotkey_state *state;
    int i;

    if (err != NULL && errsz > 0) {
        err[0] = '\0';
    }
    if (dev == NULL) {
        return -1;
    }
    memset(dev, 0, sizeof(*dev));
    for (i = 0; i < GIOCHI_HOTKEY_MAX_FDS; i++) {
        dev->fds[i] = -1;
    }

    state = calloc(1, sizeof(*state));
    if (state == NULL) {
        return -1;
    }
    giochi_hotkey_reset(state);
    dev->state = state;

    for (i = 0; i < GIOCHI_EVDEV_SCAN && dev->count < GIOCHI_HOTKEY_MAX_FDS;
         i++) {
        char path[64];
        int fd;

        (void)snprintf(path, sizeof(path), "/dev/input/event%d", i);
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        dev->fds[dev->count++] = fd;
    }

    if (dev->count == 0) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "nenhum /dev/input/event*");
        }
        return -1;
    }
    GLOG_INFO("hotkey: %d device(s) evdev", dev->count);
    return 0;
}

void giochi_hotkey_close(giochi_hotkey_device *dev)
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
    free(dev->state);
    dev->state = NULL;
    dev->count = 0;
}

int giochi_hotkey_pollfds(giochi_hotkey_device *dev, struct pollfd *pfds,
                          int offset, int max)
{
    int added = 0;
    int i;

    if (dev == NULL || pfds == NULL) {
        return 0;
    }
    for (i = 0; i < dev->count && offset + added < max; i++) {
        pfds[offset + added].fd = dev->fds[i];
        pfds[offset + added].events = POLLIN;
        pfds[offset + added].revents = 0;
        added++;
    }
    return added;
}

int giochi_hotkey_process(giochi_hotkey_device *dev, struct pollfd *pfds,
                          int offset, int count, unsigned long long now_ms)
{
    giochi_hotkey_state *state;
    int fired = 0;
    int i;

    if (dev == NULL || pfds == NULL || dev->state == NULL) {
        return 0;
    }
    state = (giochi_hotkey_state *)dev->state;

    for (i = 0; i < dev->count && i < count; i++) {
        if ((pfds[offset + i].revents & POLLIN) == 0) {
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
            if (event.code == BTN_MODE) {
                if ((giochi_hotkey_event(state, GIOCHI_HK_KEY_HOME,
                                         event.value != 0, now_ms) &
                     GIOCHI_HK_EVENT_EXIT_COMBO) != 0) {
                    fired = 1;
                }
            } else if (event.code == BTN_START) {
                if ((giochi_hotkey_event(state, GIOCHI_HK_KEY_START,
                                         event.value != 0, now_ms) &
                     GIOCHI_HK_EVENT_EXIT_COMBO) != 0) {
                    fired = 1;
                }
            }
        }
    }
    return fired;
}
