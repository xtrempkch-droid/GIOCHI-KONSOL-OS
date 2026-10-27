/* Backend usado quando libasound/mpg123/FLAC nao estao disponiveis.
 * Mantem o daemon vivo (o protocolo continua respondendo), mas sem audio.
 */
#include "audio.h"

#include "giochi_log.h"

int giochi_audio_init(void)
{
    GLOG_WARN("audio: backend ALSA nao compilado (stub headless)");
    return 0;
}

void giochi_audio_shutdown(void)
{
}

int giochi_audio_play(const char *path, unsigned start_s)
{
    (void)path;
    (void)start_s;
    return -1;
}

int giochi_audio_pause(void)
{
    return -1;
}
