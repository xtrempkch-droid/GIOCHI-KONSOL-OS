#include "giochi_sha256.h"
#include "test_util.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static void hash_hex(const char *input, char out[GIOCHI_SHA256_HEX_SIZE])
{
    giochi_sha256 ctx;
    unsigned char digest[GIOCHI_SHA256_DIGEST_SIZE];

    giochi_sha256_init(&ctx);
    giochi_sha256_update(&ctx, input, strlen(input));
    giochi_sha256_final(&ctx, digest);
    giochi_sha256_hex(digest, out);
}

int main(void)
{
    char hex[GIOCHI_SHA256_HEX_SIZE];
    char path[] = "/tmp/giochi-sha256-XXXXXX";
    unsigned char digest[GIOCHI_SHA256_DIGEST_SIZE];
    int fd;
    const char payload[] = "abc";

    hash_hex("", hex);
    CHECK_STR_EQ(hex,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    hash_hex("abc", hex);
    CHECK_STR_EQ(hex,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    hash_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", hex);
    CHECK_STR_EQ(hex,
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    fd = mkstemp(path);
    CHECK(fd >= 0);
    if (fd >= 0) {
        CHECK(write(fd, payload, sizeof(payload) - 1) == 3);
        (void)close(fd);
        CHECK(giochi_sha256_file(path, digest) == 0);
        giochi_sha256_hex(digest, hex);
        CHECK_STR_EQ(hex,
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        (void)unlink(path);
    }

    CHECK(giochi_sha256_file("/caminho/inexistente", digest) == -1);
    TEST_SUMMARY("test_sha256");
}
