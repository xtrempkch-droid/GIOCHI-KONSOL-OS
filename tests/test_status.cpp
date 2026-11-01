/* Indicadores da topbar: parse puro, sem /sys.
 *
 * O defeito que isso impede: "capacidade 0%" inventada a partir de arquivo
 * vazio. `strtol("")` devolve 0 e `end == text` — sem checar, a UI mostraria
 * bateria zerada num aparelho com bateria cheia, ou o contrario, dependendo do
 * driver. Também: `status` do sysfs é "Charging" com maiúscula (o ABI exige);
 * comparar case-insensitive esconderia um arquivo inesperado.
 */
#include "status.hpp"
#include "test_util.h"

namespace {

void test_capacidade(void)
{
    CHECK(giochi::status_parse_capacity("82") == 82);
    CHECK(giochi::status_parse_capacity("100") == 100);
    CHECK(giochi::status_parse_capacity("0") == 0);

    /* Vazio, lixo e fora de faixa NAO podem virar 0%. */
    CHECK(giochi::status_parse_capacity("") == -1);
    CHECK(giochi::status_parse_capacity("abc") == -1);
    CHECK(giochi::status_parse_capacity("-5") == -1);
    CHECK(giochi::status_parse_capacity("101") == -1);
    CHECK(giochi::status_parse_capacity(nullptr) == -1);
}

void test_rede(void)
{
    CHECK(giochi::status_parse_net("up") == 1);
    CHECK(giochi::status_parse_net("1") == 1);

    CHECK(giochi::status_parse_net("down") == 0);
    CHECK(giochi::status_parse_net("unknown") == 0);
    CHECK(giochi::status_parse_net("0") == 0);
    CHECK(giochi::status_parse_net("") == 0);
    CHECK(giochi::status_parse_net(nullptr) == 0);
}

/* O sinal e' lido do /proc/net/wireless, cujo campo `link` vai de 0 a ~70.
 * Devolver "100%" para texto ilegivel seria pior que nao mostrar nada. */
void test_sinal_wifi(void)
{
    /* Linha real do formato do kernel (wlan0 associada, link 52/70). */
    const char *linha = " wlan0: 0000   52.  -58.  -256        0      0      0      0";

    const int pct = giochi::status_parse_wireless(linha);

    CHECK(pct >= 70 && pct <= 76); /* 52/70 = 74% */

    /* Sem associacao (link 0) = 0%, nao -1: "sem sinal" e' informacao. */
    CHECK(giochi::status_parse_wireless(
              " wlan0: 0000    0.  -256.  -256        0      0      0      0") == 0);

    /* Interface a cabo nao tem Quality: nao pode ser lida como Wi-Fi. */
    CHECK(giochi::status_parse_wireless(
              "  eth0: 0000   70.  -58.  -256        0      0      0      0") == -1);

    CHECK(giochi::status_parse_wireless("") == -1);
    CHECK(giochi::status_parse_wireless(nullptr) == -1);
    CHECK(giochi::status_parse_wireless("wlan0") == -1);
    CHECK(giochi::status_parse_wireless("wlan0: lixo") == -1);

    /* Valor absurdo nao pode estourar o desenho (o icone tem 4 arcos). */
    const int maximo = giochi::status_parse_wireless(
        " wlan0: 0000  200.  -30.  -256        0      0      0      0");

    CHECK(maximo == 100);
}

/* status_update num host sem /sys de aparelho: nao pode explodir nem mentir.
 * Bateria desconhecida e' -1 (o render nao desenha), nao 0. */
void test_update_sem_hardware(void)
{
    giochi::UiStatus st;

    st.battery_pct = 55; /* lixo de chamada anterior */
    giochi::status_update(&st);

    CHECK(st.battery_pct >= -1 && st.battery_pct <= 100);

    CHECK(st.wifi_signal >= -1 && st.wifi_signal <= 100);
    CHECK(st.wifi_present == 0 || st.wifi_present == 1);
    CHECK(st.wifi_up == 0 || st.wifi_up == 1);
    CHECK(st.ethernet_up == 0 || st.ethernet_up == 1);
    /* Sinal so' existe com interface associada. */
    if (!st.wifi_up) {
        CHECK(st.wifi_signal == -1);
    }
    if (st.valid_time) {
        CHECK(st.hour >= 0 && st.hour <= 23);
        CHECK(st.minute >= 0 && st.minute <= 59);
    }

    giochi::status_update(nullptr); /* nao pode explodir */
}

}  // namespace

int main(void)
{
    test_capacidade();
    test_rede();
    test_sinal_wifi();
    test_update_sem_hardware();
    TEST_SUMMARY("test_status");
}
