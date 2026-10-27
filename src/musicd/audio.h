/* Giochi Konsol OS v1.2 — backend de audio do giochi-musicd (ADR-08). */
#ifndef GIOCHI_AUDIO_H
#define GIOCHI_AUDIO_H

int giochi_audio_init(void);
void giochi_audio_shutdown(void);

/* Inicia a reproducao de path a partir de start_s (0 = inicio). */
int giochi_audio_play(const char *path, unsigned start_s);
int giochi_audio_pause(void);

#endif /* GIOCHI_AUDIO_H */
