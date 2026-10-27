#include "musicd.h"
#include "test_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_empty_player(void)
{
    giochi_player p;
    char out[1024];

    giochi_music_player_init(&p);
    CHECK(giochi_music_command(&p, "PLAY", out, sizeof(out)) ==
          GIOCHI_MUSIC_ERR_EMPTY);
    CHECK(strstr(out, "\"empty\"") != NULL);
    CHECK(giochi_music_command(&p, "STATUS", out, sizeof(out)) ==
          GIOCHI_MUSIC_OK);
    CHECK(strstr(out, "\"queue\":[]") != NULL);
    giochi_music_player_free(&p);
}

static void touch(const char *dir, const char *name)
{
    char path[512];
    FILE *fp;

    (void)snprintf(path, sizeof(path), "%s/%s", dir, name);
    fp = fopen(path, "wb");
    if (fp != NULL) {
        (void)fputs("x", fp);
        (void)fclose(fp);
    }
}

static void test_scan_and_queue(void)
{
    char dir[] = "/tmp/giochi-music-XXXXXX";
    giochi_player p;
    char out[4096];

    CHECK(mkdtemp(dir) != NULL);
    touch(dir, "b_track.mp3");
    touch(dir, "a_track.flac");
    touch(dir, "ignore.txt");

    giochi_music_player_init(&p);
    CHECK(giochi_music_scan(&p, dir) == 0);
    CHECK(p.count == 2);
    CHECK_STR_EQ(p.tracks[0].title, "a track");
    CHECK_STR_EQ(p.tracks[0].artist, "Desconhecido");

    CHECK(giochi_music_command(&p, "QUEUE", out, sizeof(out)) ==
          GIOCHI_MUSIC_OK);
    CHECK(strstr(out, "\"a track\"") != NULL);
    CHECK(strstr(out, "\"b track\"") != NULL);
    CHECK(strstr(out, "ignore") == NULL);

    CHECK(giochi_music_command(&p, "STATUS", out, sizeof(out)) ==
          GIOCHI_MUSIC_OK);
    CHECK(strstr(out, "\"queue\":[") != NULL);

    CHECK(giochi_music_command(&p, "BOGUS", out, sizeof(out)) ==
          GIOCHI_MUSIC_ERR_COMMAND);
    CHECK(strstr(out, "unknown_command") != NULL);

    CHECK(giochi_music_command(&p, "SEEK abc", out, sizeof(out)) ==
          GIOCHI_MUSIC_ERR_COMMAND);
    CHECK(strstr(out, "invalid_position") != NULL);

    giochi_music_player_free(&p);

    {
        char path[512];

        (void)snprintf(path, sizeof(path), "%s/a_track.flac", dir);
        (void)unlink(path);
        (void)snprintf(path, sizeof(path), "%s/b_track.mp3", dir);
        (void)unlink(path);
        (void)snprintf(path, sizeof(path), "%s/ignore.txt", dir);
        (void)unlink(path);
    }
    (void)rmdir(dir);
}

static void test_missing_dir(void)
{
    giochi_player p;

    giochi_music_player_init(&p);
    CHECK(giochi_music_scan(&p, "/tmp/giochi-music-nao-existe") == -1);
    CHECK(p.count == 0);
    giochi_music_player_free(&p);
}

int main(void)
{
    test_empty_player();
    test_scan_and_queue();
    test_missing_dir();
    TEST_SUMMARY("test_musicd");
}
