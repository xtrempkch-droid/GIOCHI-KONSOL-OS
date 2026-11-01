/* Giochi Konsol OS v1.2 — parsing do estado da musica (apps/launcher/music.cpp).
 *
 * POR QUE ESTE TESTE EXISTE
 * -------------------------
 * A aba Musica era o painel "vazia" e o Launcher nunca pedia a fila ao
 * giochi-musicd. Ao ligar isso, o parsing do JSON virou a fronteira entre o
 * daemon e a tela: se ele errar, a lista sai vazia e parece que "nao ha'
 * musica" — o mesmo sintoma que custou um ciclo de aparelho (§3.129).
 *
 * O modulo e' puro (sem SDL), entao o teste roda no host com o MESMO codigo que
 * o aparelho usa.
 */
#include <cstdio>
#include <cstring>
#include <string>

#include "music.hpp"
#include "test_util.h"

using giochi::MusicInfo;
using giochi::MusicState;

/* Resposta real do daemon (medida com o binario de host). */
static const char *kStatusOk =
    "{\"status\":\"playing\",\"track\":\"A Quadrilha\",\"index\":1,\"count\":3,"
    "\"pos\":42,\"dur\":180,\"decoder_available\":true,\"queue\":["
    "{\"title\":\"Primeira\",\"artist\":\"Dominguinhos\",\"dur\":210},"
    "{\"title\":\"A Quadrilha\",\"artist\":\"Dominguinhos\",\"dur\":180},"
    "{\"title\":\"Terceira\",\"artist\":\"Desconhecido\",\"dur\":0}]}";

static const char *kQueueOk =
    "{\"queue\":[{\"title\":\"Um\",\"artist\":\"X\",\"dur\":10},"
    "{\"title\":\"Dois\",\"artist\":\"Y\",\"dur\":20}]}";

static const char *kEmpty =
    "{\"status\":\"empty\",\"track\":\"\",\"index\":0,\"count\":0,\"pos\":0,"
    "\"dur\":0,\"decoder_available\":true,\"queue\":[]}";

static void test_status_completo(void)
{
    const MusicInfo m = giochi::music_parse_status(kStatusOk);

    CHECK(m.state == MusicState::Playing);
    CHECK(m.tracks.size() == 3);
    CHECK(m.current == 1);
    CHECK(m.pos_s == 42);
    CHECK(m.duration() == 180);
    CHECK(m.decoder_ready);
    CHECK(m.detail.empty());

    CHECK_STR_EQ(m.tracks[0].title.c_str(), "Primeira");
    CHECK_STR_EQ(m.tracks[1].title.c_str(), "A Quadrilha");
    CHECK_STR_EQ(m.tracks[1].artist.c_str(), "Dominguinhos");
    CHECK(m.tracks[0].duration_s == 210);
    CHECK(m.tracks[2].duration_s == 0);
}

/* O bug que `index` evita: duas faixas com o MESMO titulo. Casar por titulo
 * marcaria a primeira e o usuario ouviria a segunda. */
static void test_duas_faixas_com_o_mesmo_titulo(void)
{
    const char *json =
        "{\"status\":\"playing\",\"index\":2,\"pos\":0,\"dur\":100,"
        "\"decoder_available\":true,\"queue\":["
        "{\"title\":\"01 A Quadrilha\",\"artist\":\"A\",\"dur\":100},"
        "{\"title\":\"01 A Quadrilha\",\"artist\":\"B\",\"dur\":100},"
        "{\"title\":\"01 A Quadrilha\",\"artist\":\"C\",\"dur\":100}]}";
    const MusicInfo m = giochi::music_parse_status(json);

    CHECK(m.tracks.size() == 3);
    CHECK(m.current == 2);
    CHECK_STR_EQ(m.tracks[static_cast<size_t>(m.current)].artist.c_str(), "C");
}

static void test_fila_vazia(void)
{
    const MusicInfo m = giochi::music_parse_status(kEmpty);

    CHECK(m.state == MusicState::Empty);
    CHECK(!m.has_tracks());
    CHECK(m.duration() == 0);
    /* mensagem de contagem vazia, nao "01 / 00" */
    CHECK(m.detail.empty());
    CHECK(giochi::music_format_count(m).empty());
}

static void test_queue_sem_posicao(void)
{
    const MusicInfo m = giochi::music_parse_queue(kQueueOk);

    CHECK(m.tracks.size() == 2);
    CHECK(m.state == MusicState::Stopped);
    CHECK(m.current == 0);
    CHECK(m.pos_s == 0);
}

/* JSON quebrado NAO pode virar "lista vazia": o motivo tem de aparecer, senao
 * a tela mente dizendo que nao ha' musica. */
static void test_json_invalido_diz_o_motivo(void)
{
    const MusicInfo m = giochi::music_parse_status("{lixo");

    CHECK(m.state == MusicState::Unavailable);
    CHECK(!m.has_tracks());
    CHECK(!m.detail.empty());
}

static void test_sem_resposta(void)
{
    const MusicInfo a = giochi::music_parse_status("");
    const MusicInfo b = giochi::music_parse_status(nullptr);

    CHECK(a.state == MusicState::Unavailable);
    CHECK(b.state == MusicState::Unavailable);
    CHECK(!a.detail.empty());
    CHECK(!b.detail.empty());
}

/* Indice fora da faixa (daemon antigo, fila encolhida) nao pode estourar. */
static void test_indice_fora_da_faixa_e_contido(void)
{
    const char *json =
        "{\"status\":\"playing\",\"index\":99,\"queue\":["
        "{\"title\":\"so' uma\",\"artist\":\"\",\"dur\":5}]}";
    const MusicInfo m = giochi::music_parse_status(json);

    CHECK(m.current == 0);
    CHECK(m.duration() == 5);
}

static void test_formato_de_tempo(void)
{
    CHECK_STR_EQ(giochi::music_format_time(0).c_str(), "0:00");
    CHECK_STR_EQ(giochi::music_format_time(9).c_str(), "0:09");
    CHECK_STR_EQ(giochi::music_format_time(65).c_str(), "1:05");
    CHECK_STR_EQ(giochi::music_format_time(600).c_str(), "10:00");
}

static void test_contagem(void)
{
    const MusicInfo m = giochi::music_parse_status(kStatusOk);

    CHECK_STR_EQ(giochi::music_format_count(m).c_str(), "02 / 03");
}

/* A rolagem tem de ser a MESMA no aparelho e no preview: a funcao e' pura e
 * este teste fixa os numeros. */
static void test_geometria_da_lista(void)
{
    /* 10 linhas de 15 px + 3 de gap = 18; 90 px de area cabem 5. */
    giochi::MusicListLayout l = giochi::music_list_layout(15, 3, 90, 10, 0);

    CHECK(l.row_h == 18);
    CHECK(l.rows == 5);
    CHECK(l.first == 0);

    /* foco na 8a linha (indice 7): a janela desliza so' o necessario */
    l = giochi::music_list_layout(15, 3, 90, 10, 7);
    CHECK(l.first == 3);

    /* foco dentro da primeira janela nao rola */
    l = giochi::music_list_layout(15, 3, 90, 10, 4);
    CHECK(l.first == 0);

    /* fila menor que a tela: nunca rola */
    l = giochi::music_list_layout(15, 3, 90, 3, 2);
    CHECK(l.first == 0);
    CHECK(l.rows == 5);

    /* area apertada ainda mostra UMA linha (lista vazia na tela seria pior) */
    l = giochi::music_list_layout(15, 3, 4, 10, 0);
    CHECK(l.rows == 1);
    CHECK(l.first == 0);

    /* rolar ate' o fim nao passa do ultimo bloco possivel */
    l = giochi::music_list_layout(15, 3, 90, 8, 7);
    CHECK(l.first == 3);
}

int main(void)
{
    test_status_completo();
    test_duas_faixas_com_o_mesmo_titulo();
    test_fila_vazia();
    test_queue_sem_posicao();
    test_json_invalido_diz_o_motivo();
    test_sem_resposta();
    test_indice_fora_da_faixa_e_contido();
    test_formato_de_tempo();
    test_contagem();
    test_geometria_da_lista();
    TEST_SUMMARY("test_music_ui");
}
