#include "control.h"

#include "giochi_log.h"
#include "giochi_sock.h"
#include "giochi_util.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>

#define GIOCHI_CONTROL_MAX_CMD 512

int giochi_control_open(giochi_control *ctl, const char *path)
{
    char err[128];
    int fd;

    if (ctl == NULL || path == NULL) {
        return -1;
    }
    memset(ctl, 0, sizeof(*ctl));
    ctl->fd = -1;

    fd = giochi_sock_listen_unix(path, 4, err, sizeof(err));
    if (fd < 0) {
        GLOG_ERR("control.sock (%s): %s", path, err);
        return -1;
    }

    ctl->fd = fd;
    ctl->active = 1;
    giochi_strlcpy(ctl->path, path, sizeof(ctl->path));
    GLOG_INFO("control.sock ativo em %s", path);
    return 0;
}

void giochi_control_close(giochi_control *ctl)
{
    if (ctl == NULL) {
        return;
    }
    if (ctl->fd >= 0) {
        (void)close(ctl->fd);
    }
    if (ctl->active) {
        (void)unlink(ctl->path);
    }
    ctl->fd = -1;
    ctl->active = 0;
}

int giochi_control_accept(giochi_control *ctl, giochi_control_handler handler,
                          void *ctx)
{
    char cmd[GIOCHI_CONTROL_MAX_CMD];
    char reply[4096];
    ssize_t len;
    int client;

    if (ctl == NULL || !ctl->active || ctl->fd < 0) {
        return -1;
    }

    client = accept(ctl->fd, NULL, NULL);
    if (client < 0) {
        if (errno == EINTR || errno == EAGAIN) {
            return 0;
        }
        GLOG_ERR("accept(control.sock): %s", strerror(errno));
        return -1;
    }

    len = giochi_sock_read_line(client, cmd, sizeof(cmd), 1000);
    if (len <= 0) {
        (void)close(client);
        return 1;
    }

    reply[0] = '\0';
    if (handler == NULL || handler(cmd, reply, sizeof(reply), ctx) != 0) {
        if (reply[0] == '\0') {
            (void)snprintf(reply, sizeof(reply), "ERR internal");
        }
    }

    (void)giochi_sock_write_all(client, reply, strlen(reply));
    (void)giochi_sock_write_all(client, "\n", 1);
    (void)close(client);
    return 1;
}

int giochi_control_serve(giochi_control *ctl, giochi_control_handler handler,
                         void *ctx, int timeout_ms)
{
    struct pollfd pfd;
    int rc;

    if (ctl == NULL || !ctl->active || ctl->fd < 0) {
        return -1;
    }

    pfd.fd = ctl->fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    do {
        rc = poll(&pfd, 1, timeout_ms);
    } while (rc < 0 && errno == EINTR);

    if (rc < 0) {
        GLOG_ERR("poll(control.sock): %s", strerror(errno));
        return -1;
    }
    if (rc == 0) {
        return 0;
    }
    return giochi_control_accept(ctl, handler, ctx);
}
