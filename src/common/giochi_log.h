/* Giochi Konsol OS v1.2 — logging comum a todos os processos. */
#ifndef GIOCHI_LOG_H
#define GIOCHI_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

enum giochi_log_level {
    GIOCHI_LOG_ERROR = 0,
    GIOCHI_LOG_WARN  = 1,
    GIOCHI_LOG_INFO  = 2,
    GIOCHI_LOG_DEBUG = 3
};

#if defined(__GNUC__)
#  define GIOCHI_PRINTF(fmt_idx, arg_idx) \
        __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#  define GIOCHI_PRINTF(fmt_idx, arg_idx)
#endif

void giochi_log_set_level(int level);
int  giochi_log_get_level(void);

/* Le GIOCHI_LOG_LEVEL (error|warn|info|debug). Default: info. */
int  giochi_log_level_from_env(void);

void giochi_log(int level, const char *fmt, ...) GIOCHI_PRINTF(2, 3);

#define GLOG_ERR(...)   giochi_log(GIOCHI_LOG_ERROR, __VA_ARGS__)
#define GLOG_WARN(...)  giochi_log(GIOCHI_LOG_WARN, __VA_ARGS__)
#define GLOG_INFO(...)  giochi_log(GIOCHI_LOG_INFO, __VA_ARGS__)
#define GLOG_DEBUG(...) giochi_log(GIOCHI_LOG_DEBUG, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* GIOCHI_LOG_H */
