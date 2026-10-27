#include "giochi_util.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

const char *giochi_root(void)
{
    static char root[PATH_MAX];
    static int initialized = 0;

    if (!initialized) {
        const char *env = getenv("GIOCHI_ROOT");
        size_t len;

        if (env == NULL) {
            env = "";
        }
        giochi_strlcpy(root, env, sizeof(root));
        len = strlen(root);
        while (len > 0 && root[len - 1] == '/') {
            root[--len] = '\0';
        }
        initialized = 1;
    }
    return root;
}

int giochi_path(char *buf, size_t bufsz, const char *suffix)
{
    int n;

    if (buf == NULL || bufsz == 0 || suffix == NULL) {
        return -1;
    }
    n = snprintf(buf, bufsz, "%s%s", giochi_root(), suffix);
    if (n < 0 || (size_t)n >= bufsz) {
        buf[bufsz - 1] = '\0';
        return -1;
    }
    return 0;
}

int giochi_mkdir_p(const char *path, mode_t mode)
{
    char tmp[PATH_MAX];
    size_t len;
    char *p;

    if (path == NULL || *path == '\0') {
        return -1;
    }
    giochi_strlcpy(tmp, path, sizeof(tmp));
    len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/') {
        tmp[--len] = '\0';
    }

    for (p = tmp + 1; *p != '\0'; p++) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
            return -1;
        }
        *p = '/';
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

char *giochi_read_file(const char *path, size_t *len_out)
{
    FILE *fp;
    char *buf;
    long size;
    size_t got;

    if (len_out != NULL) {
        *len_out = 0;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        (void)fclose(fp);
        return NULL;
    }
    size = ftell(fp);
    if (size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        (void)fclose(fp);
        return NULL;
    }

    buf = malloc((size_t)size + 1);
    if (buf == NULL) {
        (void)fclose(fp);
        return NULL;
    }
    got = fread(buf, 1, (size_t)size, fp);
    (void)fclose(fp);
    if (got != (size_t)size) {
        free(buf);
        return NULL;
    }
    buf[got] = '\0';
    if (len_out != NULL) {
        *len_out = got;
    }
    return buf;
}

int giochi_write_file_atomic(const char *path, const char *data, size_t len)
{
    char tmp[PATH_MAX];
    int fd;
    size_t written = 0;
    int rc = 0;

    if (path == NULL || (data == NULL && len != 0)) {
        return -1;
    }
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        return -1;
    }

    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return -1;
    }
    while (written < len) {
        ssize_t n = write(fd, data + written, len - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            rc = -1;
            break;
        }
        written += (size_t)n;
    }
    if (rc == 0 && fsync(fd) != 0) {
        rc = -1;
    }
    (void)close(fd);
    if (rc != 0) {
        (void)unlink(tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        (void)unlink(tmp);
        return -1;
    }
    return 0;
}

unsigned long long giochi_now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (unsigned long long)ts.tv_sec * 1000ULL
         + (unsigned long long)(ts.tv_nsec / 1000000L);
}

int giochi_is_dry_run(void)
{
    const char *env = getenv("GIOCHI_DRY_RUN");

    if (env == NULL) {
        /* Sem privilegio nao ha como tocar hardware: trata como dry-run. */
        return geteuid() != 0;
    }
    return strcmp(env, "1") == 0 || strcmp(env, "yes") == 0;
}

int giochi_run(const char *const argv[])
{
    pid_t pid;
    int status = 0;

    if (argv == NULL || argv[0] == NULL) {
        return -1;
    }
    if (giochi_is_dry_run()) {
        return 0;
    }

    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

void giochi_strlcpy(char *dst, const char *src, size_t bufsz)
{
    size_t len;

    if (dst == NULL || bufsz == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    len = strlen(src);
    if (len >= bufsz) {
        len = bufsz - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}
