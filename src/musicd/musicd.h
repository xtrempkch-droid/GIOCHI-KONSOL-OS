/* Giochi Konsol OS v1.2 — estado e protocolo do giochi-musicd (secao 4.8). */
#ifndef GIOCHI_MUSICD_H
#define GIOCHI_MUSICD_H

#include <stddef.h>

#define GIOCHI_TRACK_PATH_MAX 512
#define GIOCHI_TRACK_TEXT_MAX 128

typedef struct {
    char title[GIOCHI_TRACK_TEXT_MAX];
    char artist[GIOCHI_TRACK_TEXT_MAX];
    char album[GIOCHI_TRACK_TEXT_MAX];
    char path[GIOCHI_TRACK_PATH_MAX];
    unsigned duration_s;
} giochi_track;

typedef struct {
    giochi_track *tracks;
    size_t count;
    size_t capacity;
    size_t current;
    int playing;
    int decoder_ready;
    unsigned pos_s;
    /* Diretorio da ultima varredura, para o verbo RESCAN.
     *
     * Sem isto o musicd varria /user/music UMA VEZ, no boot, e nunca mais. Uma
     * musica copiada depois (MTP, SSH, cartao no PC) ficava invisivel ate'
     * reiniciar o aparelho — e nada no protocolo permitia pedir a varredura de
     * novo. Medido no aparelho em 2026-11-01:
     *
     *   [   7.325] musica: 0 faixas em /user/music    <- boot, pasta vazia
     *   [ 273.211] musica: 1 faixas em /user/music    <- so' apos reiniciar
     */
    char dir[GIOCHI_TRACK_PATH_MAX];
} giochi_player;

enum {
    GIOCHI_MUSIC_OK = 0,
    GIOCHI_MUSIC_ERR_EMPTY = 1,
    GIOCHI_MUSIC_ERR_DECODER = 2,
    GIOCHI_MUSIC_ERR_COMMAND = 3,
    GIOCHI_MUSIC_ERR_INTERNAL = -1
};

void giochi_music_player_init(giochi_player *p);
void giochi_music_player_free(giochi_player *p);

/* Varre dir em busca de .mp3/.flac/.ogg. Retorna 0 ou -1. */
int giochi_music_scan(giochi_player *p, const char *dir);

/* Executa um comando do protocolo e escreve o JSON de resposta em out. */
int giochi_music_command(giochi_player *p, const char *cmd, char *out,
                         size_t outsz);

/* Constante de build: 1 se mpg123 ou FLAC foram compilados. */
int giochi_music_decoder_available(void);

#endif /* GIOCHI_MUSICD_H */
