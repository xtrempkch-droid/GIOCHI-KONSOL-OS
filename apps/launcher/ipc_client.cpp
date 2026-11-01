#include "ipc_client.hpp"

#include "giochi_sock.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>

int giochi_ipc_request(const char *sock_path, const char *command,
                       char *reply, size_t replysz, int timeout_ms)
{
    char line[512];
    int fd;
    int len;
    int rc = -1;

    if (sock_path == NULL || command == NULL || reply == NULL || replysz == 0) {
        return -1;
    }
    reply[0] = '\0';

    fd = giochi_sock_connect_unix(sock_path, timeout_ms);
    if (fd < 0) {
        return -1;
    }

    if (snprintf(line, sizeof(line), "%s\n", command) < (int)sizeof(line)) {
        if (giochi_sock_write_all(fd, line, strlen(line)) == 0) {
            len = giochi_sock_read_line(fd, reply, replysz, timeout_ms);
            rc = len >= 0 ? 0 : -1;
        }
    }

    (void)close(fd);
    return rc;
}
