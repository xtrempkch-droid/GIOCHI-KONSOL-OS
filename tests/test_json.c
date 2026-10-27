#include "giochi_json.h"
#include "test_util.h"

#include <stdlib.h>

static void test_scalars(void)
{
    char err[128];
    gj_value *v = gj_parse("{\"a\":1,\"b\":true,\"c\":null,\"d\":\"x\"}",
                           err, sizeof(err));

    CHECK(v != NULL);
    CHECK(gj_as_int(gj_object_get(v, "a"), -1) == 1);
    CHECK(gj_as_bool(gj_object_get(v, "b"), 0) == 1);
    CHECK(gj_object_get(v, "c") != NULL);
    CHECK(gj_object_get(v, "c")->type == GJ_NULL);
    CHECK_STR_EQ(gj_as_string(gj_object_get(v, "d"), ""), "x");
    gj_free(v);
}

static void test_escapes(void)
{
    char err[128];
    gj_value *v = gj_parse("{\"s\":\"linha\\n\\t\\\"aspas\\\"\\\\\"}",
                           err, sizeof(err));

    CHECK(v != NULL);
    CHECK_STR_EQ(gj_as_string(gj_object_get(v, "s"), ""),
                 "linha\n\t\"aspas\"\\");
    gj_free(v);
}

static void test_unicode(void)
{
    char err[128];
    gj_value *v = gj_parse("{\"s\":\"M\\u00fasica \\ud83c\\udfb5\"}",
                           err, sizeof(err));

    CHECK(v != NULL);
    CHECK_STR_EQ(gj_as_string(gj_object_get(v, "s"), ""),
                 "M\xC3\xBAsica \xF0\x9F\x8E\xB5");
    gj_free(v);
}

static void test_arrays(void)
{
    char err[128];
    gj_value *v = gj_parse("[1,2,[3,4]]", err, sizeof(err));

    CHECK(v != NULL);
    CHECK(gj_array_len(v) == 3);
    CHECK(gj_as_int(gj_array_at(v, 1), 0) == 2);
    CHECK(gj_array_len(gj_array_at(v, 2)) == 2);
    gj_free(v);
}

static void test_errors(void)
{
    char err[128];

    CHECK(gj_parse("{\"a\":}", err, sizeof(err)) == NULL);
    CHECK(err[0] != '\0');
    CHECK(gj_parse("{\"a\":1} lixo", err, sizeof(err)) == NULL);
    CHECK(gj_parse("", err, sizeof(err)) == NULL);
    CHECK(gj_parse("\"sem fechar", err, sizeof(err)) == NULL);
    CHECK(gj_parse("[1,2,]", err, sizeof(err)) == NULL);
}

static void test_deep_nesting_fails(void)
{
    char err[128];
    char buf[512];
    size_t i;
    gj_value *v;

    for (i = 0; i < 100; i++) {
        buf[i] = '[';
    }
    for (i = 0; i < 100; i++) {
        buf[100 + i] = ']';
    }
    buf[200] = '\0';

    v = gj_parse(buf, err, sizeof(err));
    CHECK(v == NULL);
    gj_free(v);
}

int main(void)
{
    test_scalars();
    test_escapes();
    test_unicode();
    test_arrays();
    test_errors();
    test_deep_nesting_fails();
    TEST_SUMMARY("test_json");
}
