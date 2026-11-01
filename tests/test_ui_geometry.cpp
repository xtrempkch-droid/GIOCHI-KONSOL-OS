/* Geometria da UI: os numeros de docs/LAYOUT-PROPOSTA.md §3, cobrados.
 *
 * O defeito que este teste existe para impedir: o desenho da UI virar um monte
 * de constante espalhada pelo render.cpp, onde uma mudanca no painel pequeno
 * (que ninguem ve) quebra o grande (que e o unico que roda aqui). A tabela da
 * proposta foi MEDIDA pelo gerador; aqui ela e verificada pelos dois lados.
 */
#include "ui_geometry.hpp"
#include "test_util.h"

#include <cstdio>

namespace {

using giochi::UiGeometry;
using giochi::UiRect;

/* Painel base 640x480: todos os valores da coluna esquerda da tabela §3. */
void test_tokens_640(void)
{
    UiGeometry g;

    giochi::ui_geometry_base(&g, 640, 480);

    CHECK(g.topbar.h == 40);          /* metrics.topbar_height */
    CHECK(g.hint_bar.h == 30);        /* metrics.hint_bar_height */
    CHECK(g.hint_bar.y == 450);       /* 480 - 30 */
    CHECK(g.margin == 12);
    CHECK(g.inset == 4);
    CHECK(g.gap == 8);
    CHECK(g.tile_w == 152);           /* library.json grid.tile_size */
    CHECK(g.tile_h == 85);
    CHECK(g.title_h == 20);           /* grid.title_height */
    CHECK(g.corner == 4);             /* metrics.tile_corner_radius */
    CHECK(g.accent_h == 3);
    CHECK(g.focus_ring == 2);
    CHECK(g.font_tab == 14);          /* layout.json tabs.bar.font_size */
    CHECK(g.font_title == 12);        /* typography.tile_title_size */
    CHECK(g.columns == 4);            /* §3: "colunas 4 no painel pequeno" */
    CHECK(g.rows == 3);
    CHECK(g.capacity == 12);
    CHECK(g.content.y == 40);
    CHECK(g.content.h == 410);        /* 480 - 40 - 30 */
    /* Largura da grade declarada na tabela: 632 = 640 - 2*4. */
    CHECK(640 - 2 * g.inset == 632);
}

/* X55 (1280x720): a tabela da direita. O arredondamento importa: 85*1,5 =
 * 127,5 tem de virar 128, que e o valor declarado em library.json. */
void test_tokens_1280(void)
{
    UiGeometry g;

    giochi::ui_geometry_base(&g, 1280, 720);

    CHECK(g.scale_pct == 150);
    CHECK(g.topbar.h == 60);
    CHECK(g.hint_bar.h == 45);
    CHECK(g.margin == 18);
    CHECK(g.inset == 6);
    CHECK(g.gap == 12);
    CHECK(g.tile_w == 228);
    CHECK(g.tile_h == 128);           /* 127,5 arredondado */
    CHECK(g.title_h == 30);
    CHECK(g.corner == 6);
    CHECK(g.accent_h == 4);           /* §4.1: 4 px no painel grande */
    CHECK(g.focus_ring == 3);         /* §5.2: 2/3 px */
    CHECK(g.font_tab == 21);
    CHECK(g.font_title == 18);
    CHECK(g.columns == 5);            /* §3: 5 colunas, 15 tiles */
    CHECK(g.rows == 3);
    CHECK(g.capacity == 15);
    CHECK(1280 - 2 * g.inset == 1268);
}

/* A grade tem de terminar antes do hint bar nos dois paineis. A proposta mede
 * "fim da grade" 383 (640) e 576 (1280) — §4.3 diz que o gerador falha se uma
 * caixa invadir as barras; aqui e a mesma regra, do lado do render. */
void test_grade_nao_invade_barras(void)
{
    const int sizes[][2] = {{640, 480}, {1280, 720}, {320, 240}, {800, 480}};

    for (const auto &s : sizes) {
        UiGeometry g;

        giochi::ui_geometry_base(&g, s[0], s[1]);
        giochi::ui_geometry_grid(&g, 20, 0);

        for (int i = 0; i < g.tile_count; i++) {
            const UiRect &t = g.tile[i];

            CHECK(t.y > g.topbar.y + g.topbar.h - 1);
            /* Título fica abaixo do tile. */
            CHECK(t.y + t.h + g.title_h <= g.hint_bar.y);
            CHECK(t.x >= 0);
            CHECK(t.x + t.w <= g.screen_w);
        }
        CHECK(g.capacity > 0);
    }

    /* Medicao exata da ultima linha da tabela §3. */
    UiGeometry a;

    giochi::ui_geometry_base(&a, 640, 480);
    giochi::ui_geometry_grid(&a, 12, 0);
    CHECK(a.tile[8].y + a.tile[8].h + a.title_h == 383);

    UiGeometry b;

    giochi::ui_geometry_base(&b, 1280, 720);
    giochi::ui_geometry_grid(&b, 15, 0);
    CHECK(b.tile[10].y + b.tile[10].h + b.title_h == 576);
}

/* Paginacao: o foco manda na pagina, e o foco nunca sai do que esta desenhado. */
void test_paginacao(void)
{
    UiGeometry g;

    giochi::ui_geometry_base(&g, 640, 480);
    giochi::ui_geometry_grid(&g, 30, 0);
    CHECK(g.first_index == 0);
    CHECK(g.tile_count == 12);
    CHECK(g.focus_slot == 0);

    giochi::ui_geometry_grid(&g, 30, 11);
    CHECK(g.first_index == 0);
    CHECK(g.focus_slot == 11);

    giochi::ui_geometry_grid(&g, 30, 12);
    CHECK(g.first_index == 12);
    CHECK(g.focus_slot == 0);

    giochi::ui_geometry_grid(&g, 14, 13);
    CHECK(g.first_index == 12);
    CHECK(g.tile_count == 2);
    CHECK(g.focus_slot == 1);

    /* Biblioteca encolheu e o foco ficou fora: nao pode virar slot -1 nem
     * desenhar anel em tile inexistente. */
    giochi::ui_geometry_grid(&g, 0, 7);
    CHECK(g.tile_count == 0);
    CHECK(g.focus_slot == -1);
    CHECK(giochi::ui_geometry_focus_ring(g, 0).w == 0);

    giochi::ui_geometry_grid(&g, 3, 99);
    CHECK(g.first_index == 0);
    CHECK(g.focus_slot == 2);
}

/* Anel de foco por FORA do tile (§5.2): nao come o tile nem muda o layout. */
void test_anel_de_foco(void)
{
    UiGeometry g;

    giochi::ui_geometry_base(&g, 1280, 720);
    giochi::ui_geometry_grid(&g, 15, 3);

    const UiRect ring = giochi::ui_geometry_focus_ring(g, g.focus_slot);
    const UiRect &t = g.tile[g.focus_slot];

    CHECK(ring.x == t.x - g.focus_ring);
    CHECK(ring.y == t.y - g.focus_ring);
    CHECK(ring.w == t.w + 2 * g.focus_ring);
    CHECK(ring.h == t.h + 2 * g.focus_ring);
    CHECK(giochi::ui_rect_contains(ring, t.x + 1, t.y + 1) == 1);
    CHECK(giochi::ui_rect_contains(t, t.x - 1, t.y) == 0);
}

/* Abas medidas: "x da proxima = x + largura + 20" (§4.1), e aba que nao cabe
 * some em vez de encostar nos indicadores. */
void test_abas(void)
{
    UiGeometry g;
    const int widths[5] = {48, 80, 40, 30, 40};

    giochi::ui_geometry_base(&g, 640, 480);
    giochi::ui_geometry_tabs(&g, widths, 5, 2);

    CHECK(g.tab_count == 5);
    CHECK(g.tab_active == 2);
    CHECK(g.tab[0].x == g.margin);
    CHECK(g.tab[0].w == 48);
    CHECK(g.tab[1].x == g.margin + 48 + 20); /* §4.1: +20 no base */
    CHECK(g.tab[2].x == g.tab[1].x + 80 + 20);
    CHECK(g.tab[1].y == 0);
    CHECK(g.tab[1].h == g.topbar.h);

    /* No painel grande o gap e 30 (20 * 1,5). */
    UiGeometry big;

    giochi::ui_geometry_base(&big, 1280, 720);
    giochi::ui_geometry_tabs(&big, widths, 5, 0);
    CHECK(big.tab[1].x == big.margin + 48 + 30);

    /* Rotulo que nao cabe some em vez de encostar nos indicadores. */
    const int huge[2] = {1300, 1300}; /* maior que o painel */

    giochi::ui_geometry_tabs(&big, huge, 2, 0);
    CHECK(big.tab[0].w == 0);
    CHECK(big.tab[1].w == 0);
}

/* A X55 de verdade: painel 720x1280 em retrato, rotation=270 no DTB, layout
 * 1280x720. Medido no aparelho: sem isto a UI saia em pe' e com escala 2,66x
 * (a geometria era calculada sobre a SAIDA fisica, nao sobre o layout). */
void test_apresentacao_x55(void)
{
    const giochi::UiPresentation p =
        giochi::ui_present(270, 1280, 720, 720, 1280);

    CHECK(p.ok == 1);
    CHECK(p.angle == 270);
    CHECK(p.need_target == 1); /* precisa desenhar fora da tela e girar */
    /* Quad do tamanho LOGICO, centrado na saida: -280 = (720-1280)/2,
     * 280 = (1280-720)/2. Girado em torno do centro (360,640), a caixa
     * resultante e' exatamente 720x1280 — o painel inteiro. */
    CHECK(p.dst_w == 1280);
    CHECK(p.dst_h == 720);
    CHECK(p.dst_x == -280);
    CHECK(p.dst_y == 280);
}

/* Painel que ja' esta' paisagem (X35S/X35H): nada a fazer. */
void test_apresentacao_sem_rotacao(void)
{
    const giochi::UiPresentation p = giochi::ui_present(0, 640, 480, 640, 480);

    CHECK(p.ok == 1);
    CHECK(p.angle == 0);
    CHECK(p.need_target == 0);
    CHECK(p.dst_x == 0);
    CHECK(p.dst_y == 0);
    CHECK(p.dst_w == 640);
    CHECK(p.dst_h == 480);

    /* 180 tambem precisa de intermediaria: nao da' para girar "no lugar"
     * desenhando direto no framebuffer. */
    const giochi::UiPresentation h = giochi::ui_present(180, 640, 480, 640, 480);

    CHECK(h.ok == 1);
    CHECK(h.angle == 180);
    CHECK(h.need_target == 1);
}

/* Rotacao pedida que nao corresponde ao painel: NAO girar.
 *
 * Girar uma imagem sem a transposicao das dimensoes corta metade da tela. E'
 * melhor aparecer reto (e o log dizer por que) do que aparecer cortado. */
void test_apresentacao_recusa_rotacao_incoerente(void)
{
    const giochi::UiPresentation a = giochi::ui_present(270, 1280, 720, 1280, 720);

    CHECK(a.ok == 0);
    CHECK(a.need_target == 0);
    CHECK(a.motivo[0] != '\0');

    const giochi::UiPresentation b = giochi::ui_present(0, 1280, 720, 720, 1280);

    CHECK(b.ok == 0);

    /* Normalizacao: -270 equivale a 90; 450 a 90. */
    const giochi::UiPresentation c = giochi::ui_present(-270, 1280, 720, 720, 1280);

    CHECK(c.ok == 1);
    CHECK(c.angle == 90);

    const giochi::UiPresentation d = giochi::ui_present(450, 1280, 720, 720, 1280);

    CHECK(d.ok == 1);
    CHECK(d.angle == 90);

    /* Dimensoes absurdas nao podem gerar retangulo negativo silencioso. */
    const giochi::UiPresentation e = giochi::ui_present(90, 0, 0, 720, 1280);

    CHECK(e.ok == 0);
}

/* O contador de pacotes NAO pode cair em cima de um tile.
 *
 * Medido no preview: desenhado logo abaixo da topbar, ele invadia o primeiro
 * tile da grade (o conteudo comeca em topbar + margem). A faixa livre e' entre
 * o fim da grade e a hint bar. */
void test_contador_nao_invade_grade(void)
{
    const int sizes[][2] = {{640, 480}, {1280, 720}, {800, 480}};

    for (const auto &s : sizes) {
        UiGeometry g;

        giochi::ui_geometry_base(&g, s[0], s[1]);
        giochi::ui_geometry_grid(&g, 15, 0);

        const UiRect count = giochi::ui_geometry_count_rect(g);

        CHECK(count.h > 0);
        CHECK(count.y >= g.topbar.y + g.topbar.h);
        CHECK(count.y + count.h <= g.hint_bar.y);

        for (int i = 0; i < g.tile_count; i++) {
            CHECK(giochi::ui_rect_intersects(count, g.tile[i]) == 0);

            /* O titulo do tile fica logo abaixo dele: tambem nao pode tocar. */
            const UiRect titulo{g.tile[i].x, g.tile[i].y + g.tile[i].h, g.tile[i].w,
                                g.title_h};

            CHECK(giochi::ui_rect_intersects(count, titulo) == 0);
        }
    }
}

void test_intersecao(void)
{
    const UiRect a{0, 0, 10, 10};

    CHECK(giochi::ui_rect_intersects(a, UiRect{5, 5, 10, 10}) == 1);
    CHECK(giochi::ui_rect_intersects(a, UiRect{10, 0, 5, 5}) == 0); /* encosta */
    CHECK(giochi::ui_rect_intersects(a, UiRect{20, 20, 5, 5}) == 0);
    CHECK(giochi::ui_rect_intersects(a, UiRect{0, 0, 0, 10}) == 0); /* vazio */
}

/* Entradas absurdas nao podem gerar retangulo negativo. */
void test_entradas_invalidas(void)
{
    UiGeometry g;

    giochi::ui_geometry_base(&g, 0, 0);
    CHECK(g.screen_w > 0);
    CHECK(g.screen_h == 480);
    CHECK(g.tile_w == 152);

    giochi::ui_geometry_base(&g, 160, 120); /* menor que os proprios tokens */
    CHECK(g.columns >= 1);
    CHECK(g.rows >= 1);
    CHECK(g.tile_count >= 0);

    giochi::ui_geometry_base(nullptr, 640, 480); /* nao pode explodir */
}

}  // namespace

int main(void)
{
    test_tokens_640();
    test_tokens_1280();
    test_grade_nao_invade_barras();
    test_paginacao();
    test_anel_de_foco();
    test_abas();
    test_apresentacao_x55();
    test_apresentacao_sem_rotacao();
    test_apresentacao_recusa_rotacao_incoerente();
    test_contador_nao_invade_grade();
    test_intersecao();
    test_entradas_invalidas();
    TEST_SUMMARY("test_ui_geometry");
}
