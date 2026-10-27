/* Micro harness de testes (sem dependencias externas). */
#ifndef GIOCHI_TEST_UTIL_H
#define GIOCHI_TEST_UTIL_H

#include <stdio.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_failures++;                                                    \
            (void)fprintf(stderr, "%s:%d: CHECK falhou: %s\n",               \
                          __FILE__, __LINE__, #cond);                        \
        }                                                                    \
    } while (0)

#define CHECK_STR_EQ(a, b)                                                   \
    do {                                                                     \
        const char *ta_ = (a);                                               \
        const char *tb_ = (b);                                               \
        g_checks++;                                                          \
        if (ta_ == NULL || tb_ == NULL || strcmp(ta_, tb_) != 0) {            \
            g_failures++;                                                    \
            (void)fprintf(stderr, "%s:%d: esperado '%s', obtido '%s'\n",      \
                          __FILE__, __LINE__, tb_ != NULL ? tb_ : "(null)",   \
                          ta_ != NULL ? ta_ : "(null)");                      \
        }                                                                    \
    } while (0)

#define TEST_SUMMARY(name)                                                   \
    (void)printf("%s: %d checagens, %d falha(s)\n", name, g_checks,          \
                 g_failures);                                                \
    return g_failures == 0 ? 0 : 1

#endif /* GIOCHI_TEST_UTIL_H */
