/* Giochi Konsol OS v1.2 — estado da musica, como o Launcher precisa dele.
 *
 * POR QUE ESTE MODULO E' PURO (sem SDL)
 * ------------------------------------
 * O `giochi-musicd` responde JSON (docs/APP-SDK.md §3b) e a aba Musica tem de
 * mostrar a FILA, nao so' a frase "vazia". Separar o parsing do desenho e' o
 * mesmo criterio do `ui_geometry`: o `tools/dump-geometry` e os testes de host
 * leem o MESMO codigo que o aparelho usa, sem SDL no caminho.
 *
 * O que o daemon manda:
 *
 *   STATUS -> {"status":"playing","track":"...","index":1,"count":3,
 *              "pos":42,"dur":180,"decoder_available":true,
 *              "queue":[{"title":"..","artist":"..","dur":180}, ...]}
 *   QUEUE  -> {"queue":[...]}                       (sem posicao/indice)
 *   RESCAN -> {"status":"ok","tracks":N}
 *
 * `index` existe justamente para nao casar por titulo: duas faixas podem ter o
 * mesmo nome (comum em "01 A Quadrilha" de discos diferentes) e a lista
 * destacaria a errada.
 */
#ifndef GIOCHI_MUSIC_HPP
#define GIOCHI_MUSIC_HPP

#include <string>
#include <vector>

namespace giochi {

struct MusicTrack {
    std::string title;
    std::string artist;
    unsigned duration_s = 0;
};

enum class MusicState {
    Empty,       /* fila vazia: nao ha' o que tocar */
    Stopped,     /* fila com faixas, nada tocando */
    Playing,
    Paused,
    Unavailable, /* daemon fora do ar ou sem decodificador */
};

struct MusicInfo {
    MusicState state = MusicState::Unavailable;
    std::vector<MusicTrack> tracks;
    int current = 0;        /* indice da faixa atual (0 se vazio) */
    unsigned pos_s = 0;     /* posicao na faixa atual */
    bool decoder_ready = false;
    std::string detail;     /* motivo, quando Unavailable */

    bool has_tracks() const { return !tracks.empty(); }
    unsigned duration() const;
};

/* Interpreta a resposta de `STATUS`. JSON invalido => estado Unavailable com o
 * motivo em `detail` (nunca uma lista vazia silenciosa: "0 faixas" e' um fato,
 * "nao consegui ler" e' outro, e quem esta' na tela precisa distinguir). */
MusicInfo music_parse_status(const char *json);

/* Interpreta a resposta de `QUEUE` (sem posicao nem indice; o atual fica 0). */
MusicInfo music_parse_queue(const char *json);

/* "1:05" a partir de segundos. Sem horas: faixa de 3 h nao existe num portatil
 * e o formato curto cabe na coluna. */
std::string music_format_time(unsigned seconds);

/* "02 / 07" para o cabecalho da lista. */
std::string music_format_count(const MusicInfo &info);

/* Geometria da LISTA de faixas — pura, para o `render.cpp` e o preview
 * (`tools/preview-ui.py`, via `dump-geometry`) usarem os MESMOS numeros.
 *
 * Se cada lado calculasse `linhas que cabem` e `primeira visivel` por conta
 * propria, o preview mostraria uma rolagem que o aparelho nao faz — a classe
 * de defeito do AGENTS.md §3.71 (duas verdades para o mesmo dado). */
struct MusicListLayout {
    int row_h = 0;  /* altura de uma linha */
    int rows = 0;   /* quantas linhas cabem no espaco */
    int first = 0;  /* indice da primeira linha visivel */
};

MusicListLayout music_list_layout(int line_h, int gap, int available_h,
                                  int total, int focus);

}  // namespace giochi

#endif
