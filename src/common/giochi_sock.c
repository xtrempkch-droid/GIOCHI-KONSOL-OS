#include "giochi_sock.h"

#include "giochi_util.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

int giochi_sock_listen_unix(const char *path, int backlog, char *err,
                            size_t errsz)
{
    struct sockaddr_un addr;
    int fd;

    if (err != NULL && errsz > 0) {
        err[0] = '\0';
    }
    if (path == NULL || strlen(path) >= sizeof(addr.sun_path)) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "caminho de socket invalido");
        }
        return -1;
    }

    (void)unlink(path);

    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "socket: %s", strerror(errno));
        }
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    giochi_strlcpy(addr.sun_path, path, sizeof(addr.sun_path));

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "bind: %s", strerror(errno));
        }
        (void)close(fd);
        return -1;
    }
    (void)chmod(path, 0660);

    if (listen(fd, backlog) != 0) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "listen: %s", strerror(errno));
        }
        (void)close(fd);
        (void)unlink(path);
        return -1;
    }
    return fd;
}

int giochi_sock_read_line(int fd, char *buf, size_t bufsz, int timeout_ms)
{
    size_t len = 0;

    if (buf == NULL || bufsz == 0) {
        return -1;
    }

    while (len + 1 < bufsz) {
        struct pollfd pfd;
        char ch;
        ssize_t n;
        int rc;

        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        do {
            rc = poll(&pfd, 1, timeout_ms);
        } while (rc < 0 && errno == EINTR);

        if (rc < 0) {
            return -1;
        }
        if (rc == 0) {
            break;
        }

        n = read(fd, &ch, 1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            break;
        }
        if (ch == '\n') {
            break;
        }
        if (ch == '\r') {
            continue;
        }
        buf[len++] = ch;
    }

    buf[len] = '\0';
    return (int)len;
}

int giochi_sock_write_all(int fd, const char *data, size_t len)
{
    size_t done = 0;

    while (done < len) {
        ssize_t n = write(fd, data + done, len - done);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        done += (size_t)n;
    }
    return 0;
}

int giochi_sock_connect_unix(const char *path, int timeout_ms)
{
    struct sockaddr_un addr;
    struct pollfd pfd;
    int fd;
    int rc;

    if (path == NULL || strlen(path) >= sizeof(addr.sun_path)) {
        return -1;
    }

    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    giochi_strlcpy(addr.sun_path, path, sizeof(addr.sun_path));

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        return fd;
    }
    if (errno != EINPROGRESS) {
        (void)close(fd);
        return -1;
    }

    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    do {
        rc = poll(&pfd, 1, timeout_ms);
    } while (rc < 0 && errno == EINTR);

    if (rc <= 0) {
        (void)close(fd);
        return -1;
    }
    return fd;
}
