/* Giochi Konsol OS v1.2 — parsing do estado da musica (ver music.hpp). */
#include "music.hpp"

#include <cstdio>

#include "giochi_json.h"

namespace giochi {
namespace {

MusicState state_from(const char *name)
{
    if (name == nullptr) {
        return MusicState::Unavailable;
    }
    const std::string s(name);

    if (s == "playing") {
        return MusicState::Playing;
    }
    if (s == "paused") {
        return MusicState::Paused;
    }
    if (s == "stopped") {
        return MusicState::Stopped;
    }
    if (s == "empty") {
        return MusicState::Empty;
    }
    /* "ok" (RESCAN), "unavailable" e qualquer estado novo: melhor a tela dizer
     * "indisponivel" do que inventar. */
    return MusicState::Unavailable;
}

void read_queue(const gj_value *root, MusicInfo *out)
{
    const gj_value *fila = gj_object_get(root, "queue");

    if (fila == nullptr) {
        return;
    }
    const size_t n = gj_array_len(fila);

    for (size_t i = 0; i < n; i++) {
        const gj_value *item = gj_array_at(fila, i);

        if (item == nullptr) {
            continue;
        }
        MusicTrack t;

        t.title = gj_as_string(gj_object_get(item, "title"), "");
        t.artist = gj_as_string(gj_object_get(item, "artist"), "");
        const double dur = gj_as_number(gj_object_get(item, "dur"), 0.0);

        t.duration_s = dur > 0.0 ? static_cast<unsigned>(dur) : 0u;
        out->tracks.push_back(t);
    }
}

/* Parse UNICO, usado pelos dois seletores. Um parse por resposta, nao dois —
 * o JSON do STATUS ja' traz a fila inteira. */
bool parse_into(const char *json, MusicInfo *out)
{
    if (json == nullptr || *json == '\0') {
        out->detail = "sem resposta do musicd";
        return false;
    }

    char err[128] = {0};
    gj_value *raiz = gj_parse(json, err, sizeof(err));

    if (raiz == nullptr) {
        out->detail = err[0] != '\0' ? err : "resposta invalida";
        return false;
    }

    read_queue(raiz, out);

    /* `index` so' vem no STATUS; no QUEUE fica 0, que e' o esperado. */
    const double idx = gj_as_number(gj_object_get(raiz, "index"), 0.0);

    out->current = static_cast<int>(idx);
    if (out->current < 0 ||
        (out->has_tracks() &&
         out->current >= static_cast<int>(out->tracks.size()))) {
        out->current = 0;
    }

    const double pos = gj_as_number(gj_object_get(raiz, "pos"), 0.0);

    out->pos_s = pos > 0.0 ? static_cast<unsigned>(pos) : 0u;
    out->decoder_ready =
        gj_as_bool(gj_object_get(raiz, "decoder_available"), 0) != 0;

    /* Estado: de "status" quando existe; senao derivado da fila. */
    const char *status = gj_as_string(gj_object_get(raiz, "status"), nullptr);

    if (status != nullptr) {
        out->state = state_from(status);
    } else {
        out->state = out->has_tracks() ? MusicState::Stopped : MusicState::Empty;
    }
    /* Fila sem faixas e' "vazia", independente do que o daemon diga: e' o que
     * o usuario ve na tela. */
    if (!out->has_tracks() && out->state != MusicState::Unavailable) {
        out->state = MusicState::Empty;
    }

    gj_free(raiz);
    return true;
}

}  // namespace

unsigned MusicInfo::duration() const
{
    if (!has_tracks() || current < 0 ||
        current >= static_cast<int>(tracks.size())) {
        return 0;
    }
    return tracks[static_cast<size_t>(current)].duration_s;
}

MusicInfo music_parse_status(const char *json)
{
    MusicInfo info;

    (void)parse_into(json, &info);
    return info;
}

MusicInfo music_parse_queue(const char *json)
{
    MusicInfo info;

    if (parse_into(json, &info)) {
        info.current = 0;
        info.pos_s = 0;
    }
    return info;
}

std::string music_format_time(unsigned seconds)
{
    char buf[16];

    (void)std::snprintf(buf, sizeof(buf), "%u:%02u", seconds / 60u,
                        seconds % 60u);
    return std::string(buf);
}

std::string music_format_count(const MusicInfo &info)
{
    char buf[32];

    if (info.tracks.empty()) {
        return std::string();
    }
    const int total = static_cast<int>(info.tracks.size());
    int atual = info.current + 1; /* 1-based na tela */

    if (atual < 1) {
        atual = 1;
    }
    (void)std::snprintf(buf, sizeof(buf), "%02d / %02d", atual, total);
    return std::string(buf);
}

MusicListLayout music_list_layout(int line_h, int gap, int available_h,
                                  int total, int focus)
{
    MusicListLayout out;

    out.row_h = (line_h > 0 ? line_h : 1) + (gap > 0 ? gap : 0);
    out.rows = available_h / out.row_h;

    if (out.rows < 1) {
        out.rows = 1;
    }
    if (focus < 0) {
        focus = 0;
    }
    /* Rolagem minima: so' o necessario para o foco aparecer. Rolar "por
     * pagina" esconderia linhas que caberiam na tela. */
    if (focus >= out.rows) {
        out.first = focus - out.rows + 1;
    }
    const int ultima = total - out.rows;

    if (out.first > ultima) {
        out.first = ultima > 0 ? ultima : 0;
    }
    return out;
}

}  // namespace giochi
