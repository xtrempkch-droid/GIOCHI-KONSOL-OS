/* Backend ALSA direto (ADR-08): sem Pulse/PipeWire. */
#include "audio.h"

#include "giochi_log.h"

#ifdef GIOCHI_HAVE_ALSA

#include <alsa/asoundlib.h>

static snd_pcm_t *g_pcm = NULL;

int giochi_audio_init(void)
{
    /* Nos aparelhos de referencia o codec e o RK817 (card "rk817ext") e o
     * mixer "Playback Mux" PRECISA ficar em HP: o amplificador do alto-falante
     * e alimentado por HPOL/HPOR (docs/hardware-e-drivers.md §3.3). Em SPK o
     * aparelho fica mudo. TODO(v1.2): aplicar esse mixer na inicializacao. */
    int rc = snd_pcm_open(&g_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);

    if (rc < 0) {
        GLOG_ERR("alsa: snd_pcm_open(default): %s", snd_strerror(rc));
        g_pcm = NULL;
        return -1;
    }
    return 0;
}

void giochi_audio_shutdown(void)
{
    if (g_pcm != NULL) {
        (void)snd_pcm_close(g_pcm);
        g_pcm = NULL;
    }
}

int giochi_audio_play(const char *path, unsigned start_s)
{
    (void)path;
    (void)start_s;
    /* TODO(v1.2): decodificar via libmpg123/libFLAC e escrever PCM no dmix.
     * O esqueleto abre o dispositivo em giochi_audio_init() para validar a
     * pilha ALSA, mas a reproducao ainda nao esta implementada. */
    GLOG_WARN("alsa: reproducao ainda nao implementada");
    return -1;
}

int giochi_audio_pause(void)
{
    if (g_pcm == NULL) {
        return -1;
    }
    return snd_pcm_pause(g_pcm, 1) < 0 ? -1 : 0;
}

#else /* !GIOCHI_HAVE_ALSA */

int giochi_audio_init(void)
{
    GLOG_WARN("audio: compilado sem GIOCHI_HAVE_ALSA");
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

#endif /* GIOCHI_HAVE_ALSA */
