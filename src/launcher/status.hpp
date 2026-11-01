/* Giochi Konsol OS v1.2 — indicadores da topbar (secao 4.1).
 *
 * A §4.1 pede quatro coisas à direita: rede, bateria, porcentagem e relógio.
 * Elas vêm do kernel por arquivo, então a leitura fica isolada aqui — e não no
 * render.cpp — por dois motivos:
 *
 *   1. o parse é testável no host (tests/test_status.cpp) sem /sys;
 *   2. ler /sys a 60 fps é desperdício: os valores mudam em segundos.
 *
 * O renderizador chama `status_update()` e depois só lê o struct.
 */
#ifndef GIOCHI_STATUS_HPP
#define GIOCHI_STATUS_HPP

namespace giochi {

struct UiStatus {
    int battery_pct = -1;  /* -1 = sem bateria conhecida (não desenhar) */
    int charging = 0;

    /* Wi-Fi: o indicador da topbar é de Wi-Fi, não de "rede móvel" — um
     * portátil não tem modem celular, e barras de celular mentem sobre o que
     * o aparelho sabe fazer (§4.1). */
    int wifi_present = 0;  /* existe wlan* no sistema */
    int wifi_up = 0;       /* wlan* com operstate up (associada) */
    int wifi_signal = -1;  /* 0..100 de /proc/net/wireless; -1 = desconhecido */

    int ethernet_up = 0;   /* cabo (USB gadget ECM / eth*): ícone separado */
    int hour = 0;
    int minute = 0;
    int valid_time = 0;    /* 0 = relógio não ajustado (epoch baixo) */
};

/* Interpreta o conteúdo de /sys/class/power_supply/<x>/capacity.
 * Devolve -1 se não for um inteiro 0..100. Público para o teste. */
int status_parse_capacity(const char *text);

/* 1 se o texto for "up" (operstate) ou "1" (carrier). Público para o teste. */
int status_parse_net(const char *text);

/* Qualidade 0..100 a partir da linha de /proc/net/wireless que começa com
 * "wlan" (o campo `link` vem em dBm, de -100 a 0 na prática).
 * Devolve -1 quando não dá para interpretar — nunca inventar "100%". */
int status_parse_wireless(const char *text);

/* Preenche `out` lendo /sys e o relógio do sistema. Silencioso em falha: cada
 * campo que não puder ser lido fica com o valor neutro. */
void status_update(UiStatus *out);

}  // namespace giochi

#endif /* GIOCHI_STATUS_HPP */
