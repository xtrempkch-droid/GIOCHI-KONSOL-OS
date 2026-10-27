#include "giochi_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_level = GIOCHI_LOG_INFO;

static const char *level_name(int level)
{
    switch (level) {
    case GIOCHI_LOG_ERROR: return "E";
    case GIOCHI_LOG_WARN:  return "W";
    case GIOCHI_LOG_INFO:  return "I";
    case GIOCHI_LOG_DEBUG: return "D";
    default:               return "?";
    }
}

void giochi_log_set_level(int level)
{
    if (level < GIOCHI_LOG_ERROR) {
        level = GIOCHI_LOG_ERROR;
    }
    if (level > GIOCHI_LOG_DEBUG) {
        level = GIOCHI_LOG_DEBUG;
    }
    g_level = level;
}

int giochi_log_get_level(void)
{
    return g_level;
}

int giochi_log_level_from_env(void)
{
    const char *env = getenv("GIOCHI_LOG_LEVEL");

    if (env == NULL || *env == '\0') {
        return GIOCHI_LOG_INFO;
    }
    if (strcmp(env, "error") == 0) {
        return GIOCHI_LOG_ERROR;
    }
    if (strcmp(env, "warn") == 0) {
        return GIOCHI_LOG_WARN;
    }
    if (strcmp(env, "debug") == 0) {
        return GIOCHI_LOG_DEBUG;
    }
    return GIOCHI_LOG_INFO;
}

void giochi_log(int level, const char *fmt, ...)
{
    struct timespec ts;
    va_list ap;

    if (level > g_level) {
        return;
    }

    clock_gettime(CLOCK_MONOTONIC, &ts);
    (void)fprintf(stderr, "[%5ld.%03ld] %s: ",
                  (long)ts.tv_sec, (long)(ts.tv_nsec / 1000000L),
                  level_name(level));

    va_start(ap, fmt);
    (void)vfprintf(stderr, fmt, ap);
    va_end(ap);

    (void)fputc('\n', stderr);
    (void)fflush(stderr);
}
