/* giochi_deviceconf.c — ver giochi_deviceconf.h para o porque deste modulo. */
#include "giochi_deviceconf.h"

#include <stdio.h>
#include <string.h>

static void trim(char *s)
{
    size_t len = strlen(s);
    size_t start = 0;

    while (s[start] == ' ' || s[start] == '\t') {
        start++;
    }
    if (start != 0) {
        memmove(s, s + start, len - start + 1);
        len -= start;
    }
    while (len > 0) {
        char last = s[len - 1];

        if (last != ' ' && last != '\t' && last != '\r' && last != '\n') {
            break;
        }
        s[--len] = '\0';
    }
}

int giochi_deviceconf_get(const char *path, const char *key, char *out,
                          size_t out_size)
{
    FILE *fp;
    char line[256];
    size_t key_len;

    if (out == NULL || out_size == 0) {
        return -1;
    }
    out[0] = '\0';
    if (path == NULL || key == NULL || key[0] == '\0') {
        return -1;
    }

    key_len = strlen(key);
    fp = fopen(path, "r");
    if (fp == NULL) {
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *value;

        if (strncmp(line, key, key_len) != 0 || line[key_len] != '=') {
            continue;
        }
        value = line + key_len + 1;
        trim(value);
        if (value[0] == '\0') {
            continue;
        }
        if (strlen(value) + 1 > out_size) {
            continue;
        }
        memcpy(out, value, strlen(value) + 1);
        fclose(fp);
        return 0;
    }

    fclose(fp);
    return -1;
}

const char *giochi_deviceconf_get_or(const char *path, const char *key,
                                     char *out, size_t out_size,
                                     const char *fallback)
{
    if (giochi_deviceconf_get(path, key, out, out_size) != 0) {
        return fallback;
    }
    return out;
}
