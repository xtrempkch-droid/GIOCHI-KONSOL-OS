/* Giochi Konsol OS v1.2 — parser JSON minimo (DOM) usado por layouts,
 * settings.json, device_profile.json, metadata.json e pelo protocolo de socket.
 *
 * Implementacao propria, sem dependencias externas (ADR-03: footprint).
 */
#ifndef GIOCHI_JSON_H
#define GIOCHI_JSON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GJ_NULL = 0,
    GJ_BOOL,
    GJ_NUMBER,
    GJ_STRING,
    GJ_ARRAY,
    GJ_OBJECT
} gj_type;

typedef struct gj_value gj_value;

struct gj_value {
    gj_type type;
    char *key; /* preenchido quando o valor e membro de um objeto */
    union {
        int boolean;
        double number;
        char *string;
        struct {
            gj_value *head;
            gj_value *tail;
        } children;
    } u;
    gj_value *next;
};

/* Retorna NULL em erro de sintaxe/OOM e escreve a causa em err. */
gj_value *gj_parse(const char *text, char *err, size_t errsz);
gj_value *gj_parse_file(const char *path, char *err, size_t errsz);
void gj_free(gj_value *v);

const gj_value *gj_object_get(const gj_value *obj, const char *key);
size_t gj_array_len(const gj_value *v);
const gj_value *gj_array_at(const gj_value *v, size_t index);

const char *gj_as_string(const gj_value *v, const char *fallback);
double gj_as_number(const gj_value *v, double fallback);
int gj_as_bool(const gj_value *v, int fallback);
int gj_as_int(const gj_value *v, int fallback);

const char *gj_type_name(gj_type type);

#ifdef __cplusplus
}
#endif

#endif /* GIOCHI_JSON_H */
