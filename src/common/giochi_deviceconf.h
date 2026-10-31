/* giochi_deviceconf.h — leitura de /etc/giochi/device.conf (secao 2.2).
 *
 * device.conf e gerado no build por buildroot/board/giochi/post-build.sh e e a
 * fonte unica da identidade do aparelho em tempo de execucao:
 *
 *     GIOCHI_DEVICE=x55
 *     GIOCHI_SOC=rk3566
 *     GIOCHI_BUTTONS_MAP=default
 *
 * Por que ler em tempo de execucao e nao confiar no -DGIOCHI_DEVICE do
 * compilador: o valor atravessa duas camadas de shell (recipe do pacote
 * Buildroot e recipe do Makefile da raiz) e ja se perdeu em silencio uma vez —
 * a X55 escreveu "device=rk3326" no device_profile.json. O arquivo e medido na
 * imagem, entao nao tem como divergir.
 */
#ifndef GIOCHI_DEVICECONF_H
#define GIOCHI_DEVICECONF_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Caminho padrao dentro da imagem. */
#define GIOCHI_DEVICECONF_PATH "/etc/giochi/device.conf"

/* Le `chave` de `path` e copia o valor (sem espacos ou CR nas pontas) em
 * `out`. Devolve 0 se achou, -1 se o arquivo nao abriu, a chave nao existe ou
 * o valor e vazio. `out` fica sempre terminado em '\0'.
 *
 * A chave casa com o inicio da linha: "GIOCHI_SOC=" nao casa com
 * "X_GIOCHI_SOC=", e o tamanho do prefixo vem de strlen — nunca de um numero
 * escrito a mao. */
int giochi_deviceconf_get(const char *path, const char *key, char *out,
                          size_t out_size);

/* Igual, mas se a leitura falhar devolve `fallback` (que pode ser NULL).
 * Nunca devolve ponteiro para dentro de `out` vazio.
 *
 * `out` precisa ter espaco para o valor; devolve `fallback` quando o valor nao
 * cabe. */
const char *giochi_deviceconf_get_or(const char *path, const char *key,
                                     char *out, size_t out_size,
                                     const char *fallback);

#ifdef __cplusplus
}
#endif

#endif /* GIOCHI_DEVICECONF_H */
