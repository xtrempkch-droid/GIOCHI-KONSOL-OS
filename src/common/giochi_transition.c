#include "giochi_transition.h"

#include "giochi_log.h"
#include "giochi_util.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int giochi_transition_open(const char *path)
{
    int fd;

    if (path == NULL) {
        return -1;
    }
    if (mkfifo(path, 0660) != 0 && errno != EEXIST) {
        GLOG_ERR("mkfifo(%s): %s", path, strerror(errno));
        return -1;
    }

    fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        GLOG_ERR("open(%s): %s", path, strerror(errno));
    }
    return fd;
}

void giochi_transition_fill(giochi_transition_msg *msg,
                            giochi_transition_cmd cmd, const char *path,
                            const char *metadata)
{
    if (msg == NULL) {
        return;
    }
    memset(msg, 0, sizeof(*msg));
    msg->magic = GIOCHI_TRANSITION_MAGIC;
    msg->version = 1;
    msg->cmd = (uint16_t)cmd;
    giochi_strlcpy(msg->path, path, sizeof(msg->path));
    giochi_strlcpy(msg->metadata, metadata, sizeof(msg->metadata));
}

static int write_full(int fd, const void *data, size_t len)
{
    const char *bytes = data;
    size_t done = 0;

    while (done < len) {
        ssize_t n = write(fd, bytes + done, len - done);

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

int giochi_transition_send(int fd, const giochi_transition_msg *msg)
{
    if (fd < 0 || msg == NULL) {
        return -1;
    }
    if (msg->magic != GIOCHI_TRANSITION_MAGIC) {
        return -1;
    }
    if (write_full(fd, msg, sizeof(*msg)) != 0) {
        GLOG_WARN("transition: escrita falhou: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int giochi_transition_recv(int fd, giochi_transition_msg *msg)
{
    ssize_t n;

    if (fd < 0 || msg == NULL) {
        return -1;
    }
    n = read(fd, msg, sizeof(*msg));
    if (n < 0) {
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
    }
    if (n == 0) {
        return 0;
    }
    if (n != (ssize_t)sizeof(*msg) || msg->magic != GIOCHI_TRANSITION_MAGIC ||
        msg->version != 1) {
        GLOG_WARN("transition: mensagem invalida (%ld bytes)", (long)n);
        return -1;
    }
    msg->path[sizeof(msg->path) - 1] = '\0';
    msg->metadata[sizeof(msg->metadata) - 1] = '\0';
    return 1;
}
