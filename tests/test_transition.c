#include "giochi_transition.h"
#include "giochi_util.h"
#include "test_util.h"

#include <stdlib.h>
#include <unistd.h>

int main(void)
{
    char dir[] = "/tmp/giochi-fifo-XXXXXX";
    char fifo[512];
    giochi_transition_msg sent;
    giochi_transition_msg got;
    int fd;

    CHECK(mkdtemp(dir) != NULL);
    (void)snprintf(fifo, sizeof(fifo), "%s/transition", dir);

    fd = giochi_transition_open(fifo);
    CHECK(fd >= 0);
    if (fd < 0) {
        TEST_SUMMARY("test_transition");
    }

    giochi_transition_fill(&sent, GIOCHI_TRANS_LAUNCH, "/user/games/a.giochi",
                           "{\"title\":\"A\"}");
    CHECK(giochi_transition_send(fd, &sent) == 0);

    CHECK(giochi_transition_recv(fd, &got) == 1);
    CHECK(got.cmd == GIOCHI_TRANS_LAUNCH);
    CHECK_STR_EQ(got.path, "/user/games/a.giochi");
    CHECK_STR_EQ(got.metadata, "{\"title\":\"A\"}");

    CHECK(giochi_transition_recv(fd, &got) == 0);

    /* Reabrir nao perde a mensagem pendente e nao recria o FIFO. */
    (void)close(fd);
    fd = giochi_transition_open(fifo);
    CHECK(fd >= 0);
    giochi_transition_fill(&sent, GIOCHI_TRANS_EXIT_GAME, "", "");
    CHECK(giochi_transition_send(fd, &sent) == 0);
    CHECK(giochi_transition_recv(fd, &got) == 1);
    CHECK(got.cmd == GIOCHI_TRANS_EXIT_GAME);

    (void)close(fd);
    (void)unlink(fifo);
    (void)rmdir(dir);
    TEST_SUMMARY("test_transition");
}
