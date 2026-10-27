#include "giochi_json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GJ_MAX_DEPTH 64

typedef struct {
    const char *start;
    const char *p;
    char *err;
    size_t errsz;
    int depth;
    int failed;
} gj_parser;

static void gj_set_error(gj_parser *ps, const char *msg)
{
    if (ps->failed) {
        return;
    }
    ps->failed = 1;
    if (ps->err != NULL && ps->errsz > 0) {
        (void)snprintf(ps->err, ps->errsz, "%s (offset %ld)",
                       msg, (long)(ps->p - ps->start));
    }
}

static void gj_skip_ws(gj_parser *ps)
{
    while (*ps->p != '\0' && isspace((unsigned char)*ps->p)) {
        ps->p++;
    }
}

static gj_value *gj_new(gj_type type)
{
    gj_value *v = calloc(1, sizeof(*v));

    if (v != NULL) {
        v->type = type;
    }
    return v;
}

static void gj_append(gj_value *parent, gj_value *child)
{
    if (parent->u.children.tail == NULL) {
        parent->u.children.head = child;
    } else {
        parent->u.children.tail->next = child;
    }
    parent->u.children.tail = child;
}

static int gj_utf8_encode(unsigned cp, char *out)
{
    if (cp < 0x80U) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800U) {
        out[0] = (char)(0xC0U | (cp >> 6));
        out[1] = (char)(0x80U | (cp & 0x3FU));
        return 2;
    }
    if (cp < 0x10000U) {
        out[0] = (char)(0xE0U | (cp >> 12));
        out[1] = (char)(0x80U | ((cp >> 6) & 0x3FU));
        out[2] = (char)(0x80U | (cp & 0x3FU));
        return 3;
    }
    out[0] = (char)(0xF0U | (cp >> 18));
    out[1] = (char)(0x80U | ((cp >> 12) & 0x3FU));
    out[2] = (char)(0x80U | ((cp >> 6) & 0x3FU));
    out[3] = (char)(0x80U | (cp & 0x3FU));
    return 4;
}

static int gj_hex4(const char *p, unsigned *out)
{
    unsigned value = 0;
    int i;

    for (i = 0; i < 4; i++) {
        int c = (unsigned char)p[i];
        int digit;

        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        } else {
            return -1;
        }
        value = (value << 4) | (unsigned)digit;
    }
    *out = value;
    return 0;
}

static char *gj_parse_string(gj_parser *ps)
{
    size_t cap = 32;
    size_t len = 0;
    char *buf;
    char encoded[4];
    int enc_len;

    if (*ps->p != '"') {
        gj_set_error(ps, "expected string");
        return NULL;
    }
    ps->p++;

    buf = malloc(cap);
    if (buf == NULL) {
        gj_set_error(ps, "out of memory");
        return NULL;
    }

    while (*ps->p != '\0' && *ps->p != '"') {
        unsigned char c = (unsigned char)*ps->p;

        encoded[0] = (char)c;
        enc_len = 1;

        if (c == '\\') {
            char esc;

            ps->p++;
            esc = *ps->p;
            if (esc == '\0') {
                gj_set_error(ps, "unterminated escape");
                free(buf);
                return NULL;
            }
            ps->p++;

            switch (esc) {
            case '"':  encoded[0] = '"';  break;
            case '\\': encoded[0] = '\\'; break;
            case '/':  encoded[0] = '/';  break;
            case 'b':  encoded[0] = '\b'; break;
            case 'f':  encoded[0] = '\f'; break;
            case 'n':  encoded[0] = '\n'; break;
            case 'r':  encoded[0] = '\r'; break;
            case 't':  encoded[0] = '\t'; break;
            case 'u': {
                unsigned cp = 0;

                if (gj_hex4(ps->p, &cp) != 0) {
                    gj_set_error(ps, "invalid \\u escape");
                    free(buf);
                    return NULL;
                }
                ps->p += 4;
                if (cp >= 0xD800U && cp <= 0xDBFFU &&
                    ps->p[0] == '\\' && ps->p[1] == 'u') {
                    unsigned lo = 0;

                    if (gj_hex4(ps->p + 2, &lo) == 0 &&
                        lo >= 0xDC00U && lo <= 0xDFFFU) {
                        cp = 0x10000U + ((cp - 0xD800U) << 10) + (lo - 0xDC00U);
                        ps->p += 6;
                    }
                }
                if (cp >= 0xD800U && cp <= 0xDFFFU) {
                    cp = 0xFFFDU;
                }
                enc_len = gj_utf8_encode(cp, encoded);
                break;
            }
            default:
                gj_set_error(ps, "invalid escape sequence");
                free(buf);
                return NULL;
            }
        } else {
            if (c < 0x20U) {
                gj_set_error(ps, "control character in string");
                free(buf);
                return NULL;
            }
            ps->p++;
        }

        if (len + (size_t)enc_len + 1 > cap) {
            size_t newcap = cap * 2;
            char *tmp;

            while (newcap < len + (size_t)enc_len + 1) {
                newcap *= 2;
            }
            tmp = realloc(buf, newcap);
            if (tmp == NULL) {
                gj_set_error(ps, "out of memory");
                free(buf);
                return NULL;
            }
            buf = tmp;
            cap = newcap;
        }
        memcpy(buf + len, encoded, (size_t)enc_len);
        len += (size_t)enc_len;
    }

    if (*ps->p != '"') {
        gj_set_error(ps, "unterminated string");
        free(buf);
        return NULL;
    }
    ps->p++;
    buf[len] = '\0';
    return buf;
}

static gj_value *gj_parse_value(gj_parser *ps);

static gj_value *gj_parse_object(gj_parser *ps)
{
    gj_value *obj = gj_new(GJ_OBJECT);

    if (obj == NULL) {
        gj_set_error(ps, "out of memory");
        return NULL;
    }
    ps->p++; /* '{' */
    gj_skip_ws(ps);
    if (*ps->p == '}') {
        ps->p++;
        return obj;
    }

    for (;;) {
        char *key;
        gj_value *val;

        gj_skip_ws(ps);
        key = gj_parse_string(ps);
        if (key == NULL) {
            gj_free(obj);
            return NULL;
        }
        gj_skip_ws(ps);
        if (*ps->p != ':') {
            gj_set_error(ps, "expected ':'");
            free(key);
            gj_free(obj);
            return NULL;
        }
        ps->p++;
        val = gj_parse_value(ps);
        if (val == NULL) {
            free(key);
            gj_free(obj);
            return NULL;
        }
        val->key = key;
        gj_append(obj, val);

        gj_skip_ws(ps);
        if (*ps->p == ',') {
            ps->p++;
            continue;
        }
        if (*ps->p == '}') {
            ps->p++;
            return obj;
        }
        gj_set_error(ps, "expected ',' or '}'");
        gj_free(obj);
        return NULL;
    }
}

static gj_value *gj_parse_array(gj_parser *ps)
{
    gj_value *arr = gj_new(GJ_ARRAY);

    if (arr == NULL) {
        gj_set_error(ps, "out of memory");
        return NULL;
    }
    ps->p++; /* '[' */
    gj_skip_ws(ps);
    if (*ps->p == ']') {
        ps->p++;
        return arr;
    }

    for (;;) {
        gj_value *val = gj_parse_value(ps);

        if (val == NULL) {
            gj_free(arr);
            return NULL;
        }
        gj_append(arr, val);

        gj_skip_ws(ps);
        if (*ps->p == ',') {
            ps->p++;
            continue;
        }
        if (*ps->p == ']') {
            ps->p++;
            return arr;
        }
        gj_set_error(ps, "expected ',' or ']'");
        gj_free(arr);
        return NULL;
    }
}

static gj_value *gj_parse_number(gj_parser *ps)
{
    char *end = NULL;
    double value;
    gj_value *v;

    value = strtod(ps->p, &end);
    if (end == ps->p) {
        gj_set_error(ps, "invalid number");
        return NULL;
    }
    v = gj_new(GJ_NUMBER);
    if (v == NULL) {
        gj_set_error(ps, "out of memory");
        return NULL;
    }
    v->u.number = value;
    ps->p = end;
    return v;
}

static gj_value *gj_parse_literal(gj_parser *ps, const char *literal,
                                  gj_type type, int boolean)
{
    size_t len = strlen(literal);
    gj_value *v;

    if (strncmp(ps->p, literal, len) != 0) {
        gj_set_error(ps, "invalid literal");
        return NULL;
    }
    ps->p += len;
    v = gj_new(type);
    if (v == NULL) {
        gj_set_error(ps, "out of memory");
        return NULL;
    }
    if (type == GJ_BOOL) {
        v->u.boolean = boolean;
    }
    return v;
}

static gj_value *gj_parse_value(gj_parser *ps)
{
    gj_value *v = NULL;

    gj_skip_ws(ps);
    if (ps->failed) {
        return NULL;
    }
    if (++ps->depth > GJ_MAX_DEPTH) {
        gj_set_error(ps, "nesting too deep");
        ps->depth--;
        return NULL;
    }

    switch (*ps->p) {
    case '{':
        v = gj_parse_object(ps);
        break;
    case '[':
        v = gj_parse_array(ps);
        break;
    case '"': {
        char *str = gj_parse_string(ps);

        if (str != NULL) {
            v = gj_new(GJ_STRING);
            if (v != NULL) {
                v->u.string = str;
            } else {
                free(str);
                gj_set_error(ps, "out of memory");
            }
        }
        break;
    }
    case 't':
        v = gj_parse_literal(ps, "true", GJ_BOOL, 1);
        break;
    case 'f':
        v = gj_parse_literal(ps, "false", GJ_BOOL, 0);
        break;
    case 'n':
        v = gj_parse_literal(ps, "null", GJ_NULL, 0);
        break;
    default:
        if (*ps->p == '-' || (*ps->p >= '0' && *ps->p <= '9')) {
            v = gj_parse_number(ps);
        } else {
            gj_set_error(ps, "unexpected character");
        }
        break;
    }

    ps->depth--;
    return v;
}

void gj_free(gj_value *v)
{
    while (v != NULL) {
        gj_value *next = v->next;

        free(v->key);
        if (v->type == GJ_STRING) {
            free(v->u.string);
        } else if (v->type == GJ_ARRAY || v->type == GJ_OBJECT) {
            gj_free(v->u.children.head);
        }
        free(v);
        v = next;
    }
}

gj_value *gj_parse(const char *text, char *err, size_t errsz)
{
    gj_parser ps;
    gj_value *v;

    if (err != NULL && errsz > 0) {
        err[0] = '\0';
    }
    if (text == NULL) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "null input");
        }
        return NULL;
    }

    memset(&ps, 0, sizeof(ps));
    ps.start = text;
    ps.p = text;
    ps.err = err;
    ps.errsz = errsz;

    v = gj_parse_value(&ps);
    if (v == NULL) {
        return NULL;
    }
    gj_skip_ws(&ps);
    if (*ps.p != '\0') {
        gj_set_error(&ps, "trailing data after value");
        gj_free(v);
        return NULL;
    }
    return v;
}

gj_value *gj_parse_file(const char *path, char *err, size_t errsz)
{
    FILE *fp;
    char *buf;
    long size;
    size_t got;
    gj_value *v;

    if (err != NULL && errsz > 0) {
        err[0] = '\0';
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "cannot open '%s'", path);
        }
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        (void)fclose(fp);
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "cannot seek '%s'", path);
        }
        return NULL;
    }
    size = ftell(fp);
    if (size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        (void)fclose(fp);
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "cannot read '%s'", path);
        }
        return NULL;
    }

    buf = malloc((size_t)size + 1);
    if (buf == NULL) {
        (void)fclose(fp);
        if (err != NULL && errsz > 0) {
            (void)snprintf(err, errsz, "out of memory");
        }
        return NULL;
    }
    got = fread(buf, 1, (size_t)size, fp);
    (void)fclose(fp);
    buf[got] = '\0';

    v = gj_parse(buf, err, errsz);
    free(buf);
    return v;
}

const gj_value *gj_object_get(const gj_value *obj, const char *key)
{
    const gj_value *it;

    if (obj == NULL || obj->type != GJ_OBJECT || key == NULL) {
        return NULL;
    }
    for (it = obj->u.children.head; it != NULL; it = it->next) {
        if (it->key != NULL && strcmp(it->key, key) == 0) {
            return it;
        }
    }
    return NULL;
}

size_t gj_array_len(const gj_value *v)
{
    const gj_value *it;
    size_t count = 0;

    if (v == NULL || v->type != GJ_ARRAY) {
        return 0;
    }
    for (it = v->u.children.head; it != NULL; it = it->next) {
        count++;
    }
    return count;
}

const gj_value *gj_array_at(const gj_value *v, size_t index)
{
    const gj_value *it;

    if (v == NULL || v->type != GJ_ARRAY) {
        return NULL;
    }
    for (it = v->u.children.head; it != NULL; it = it->next) {
        if (index == 0) {
            return it;
        }
        index--;
    }
    return NULL;
}

const char *gj_as_string(const gj_value *v, const char *fallback)
{
    if (v == NULL || v->type != GJ_STRING || v->u.string == NULL) {
        return fallback;
    }
    return v->u.string;
}

double gj_as_number(const gj_value *v, double fallback)
{
    if (v == NULL || v->type != GJ_NUMBER) {
        return fallback;
    }
    return v->u.number;
}

int gj_as_bool(const gj_value *v, int fallback)
{
    if (v == NULL || v->type != GJ_BOOL) {
        return fallback;
    }
    return v->u.boolean;
}

int gj_as_int(const gj_value *v, int fallback)
{
    double d;

    if (v == NULL || v->type != GJ_NUMBER) {
        return fallback;
    }
    d = v->u.number;
    if (d > 2147483647.0 || d < -2147483648.0) {
        return fallback;
    }
    return (int)d;
}

const char *gj_type_name(gj_type type)
{
    switch (type) {
    case GJ_NULL:   return "null";
    case GJ_BOOL:   return "bool";
    case GJ_NUMBER: return "number";
    case GJ_STRING: return "string";
    case GJ_ARRAY:  return "array";
    case GJ_OBJECT: return "object";
    default:        return "?";
    }
}
