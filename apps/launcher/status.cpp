#include "status.hpp"

#include "giochi_util.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <dirent.h>

namespace giochi {

namespace {

constexpr const char *kPowerSupply = "/sys/class/power_supply";
constexpr const char *kNetClass = "/sys/class/net";

/* Lê um arquivo pequeno e devolve o texto sem espaços/CR nas pontas.
 * Reaproveita giochi_read_file para não ter dois leitores de arquivo. */
bool read_trim(const char *path, char *out, std::size_t out_size)
{
    if (out == nullptr || out_size == 0) {
        return false;
    }
    out[0] = '\0';

    std::size_t len = 0;
    char *data = giochi_read_file(path, &len);

    if (data == nullptr) {
        return false;
    }

    std::size_t n = 0;

    while (n < len && n + 1 < out_size && data[n] != '\n' && data[n] != '\r') {
        out[n] = data[n];
        n++;
    }
    out[n] = '\0';
    std::free(data);
    return true;
}

}  // namespace

int status_parse_capacity(const char *text)
{
    if (text == nullptr) {
        return -1;
    }

    char *end = nullptr;
    const long value = std::strtol(text, &end, 10);

    /* strtol com texto vazio devolve 0 e end == text: sem este teste,
     * "capacidade 0%" seria inventada a partir de arquivo vazio. */
    if (end == text || value < 0 || value > 100) {
        return -1;
    }
    return static_cast<int>(value);
}

int status_parse_wireless(const char *text)
{
    /* Formato do /proc/net/wireless:
     *   Inter-| sta-|   Quality        |   Discarded packets
     *    face | tus | link level noise |  nwid  crypt   frag  retry
     *     wlan0: 0000   52.  -58.  -256        0      0      0      0
     *
     * O campo `link` (o 3o numero) e' a qualidade em 0..70 na maioria dos
     * drivers. Usamos ele, nao o dBm: a conversao de dBm varia por driver e
     * inventar uma escala seria pior que nao mostrar. */
    if (text == nullptr) {
        return -1;
    }

    /* O kernel alinha as linhas com espaco a' esquerda (" wlan0: ..."):
     * comparar direto no inicio rejeitava a linha real. */
    while (*text == ' ' || *text == '\t') {
        text++;
    }
    if (std::strncmp(text, "wlan", 4) != 0) {
        return -1;
    }

    const char *colon = std::strchr(text, ':');

    if (colon == nullptr) {
        return -1;
    }

    /* Depois do ':' vem "status link level noise ..." */
    int status = 0;
    double link = 0;

    if (std::sscanf(colon + 1, "%x %lf", &status, &link) != 2) {
        return -1;
    }
    if (link < 0) {
        return -1;
    }

    /* 70 e' o maximo pratico do campo `link`. */
    int pct = static_cast<int>((link / 70.0) * 100.0);

    if (pct < 0) {
        pct = 0;
    }
    if (pct > 100) {
        pct = 100;
    }
    return pct;
}

int status_parse_net(const char *text)
{
    if (text == nullptr) {
        return 0;
    }
    if (std::strcmp(text, "up") == 0) {
        return 1;
    }
    if (std::strcmp(text, "1") == 0) {
        return 1;
    }
    return 0;
}

void status_update(UiStatus *out)
{
    if (out == nullptr) {
        return;
    }

    /* --- bateria: primeiro power_supply com capacity legível --- */
    out->battery_pct = -1;
    out->charging = 0;

    if (DIR *dir = opendir(kPowerSupply); dir != nullptr) {
        while (struct dirent *entry = readdir(dir)) {
            if (entry->d_name[0] == '.') {
                continue;
            }

            char path[512];
            char text[32];

            std::snprintf(path, sizeof(path), "%s/%s/capacity", kPowerSupply,
                          entry->d_name);
            if (!read_trim(path, text, sizeof(text))) {
                continue;
            }
            const int pct = status_parse_capacity(text);

            if (pct < 0) {
                continue;
            }
            out->battery_pct = pct;

            /* `status` é "Charging"/"Discharging"/"Full"; o ABI exige a
             * primeira letra maiúscula. */
            std::snprintf(path, sizeof(path), "%s/%s/status", kPowerSupply,
                          entry->d_name);
            if (read_trim(path, text, sizeof(text))) {
                out->charging = std::strcmp(text, "Charging") == 0 ||
                                std::strcmp(text, "Full") == 0;
            }
            break;
        }
        closedir(dir);
    }

    /* --- Wi-Fi e cabo, separados ---
     * Um portatil nao tem modem celular: o indicador e' de Wi-Fi (arcos), e o
     * cabo (USB gadget ECM) ganha icone proprio. Juntar os dois num "tem rede"
     * fazia o desenho mentir sobre QUAL rede esta' ativa. */
    out->wifi_present = 0;
    out->wifi_up = 0;
    out->wifi_signal = -1;
    out->ethernet_up = 0;

    if (DIR *dir = opendir(kNetClass); dir != nullptr) {
        while (struct dirent *entry = readdir(dir)) {
            if (entry->d_name[0] == '.' ||
                std::strcmp(entry->d_name, "lo") == 0) {
                continue;
            }

            char path[512];
            char text[32];
            const bool is_wifi = std::strncmp(entry->d_name, "wlan", 4) == 0;

            if (is_wifi) {
                out->wifi_present = 1;
            }

            std::snprintf(path, sizeof(path), "%s/%s/operstate", kNetClass,
                          entry->d_name);
            if (read_trim(path, text, sizeof(text)) && status_parse_net(text)) {
                if (is_wifi) {
                    out->wifi_up = 1;
                } else {
                    out->ethernet_up = 1;
                }
            }
        }
        closedir(dir);
    }

    /* Sinal so' faz sentido com a interface associada. */
    if (out->wifi_up) {
        std::size_t len = 0;
        char *data = giochi_read_file("/proc/net/wireless", &len);

        if (data != nullptr) {
            char *line = data;

            while (line != nullptr && *line != '\0') {
                char *next = std::strchr(line, '\n');

                if (next != nullptr) {
                    *next = '\0';
                }
                if (std::strncmp(line, "wlan", 4) == 0) {
                    out->wifi_signal = status_parse_wireless(line);
                    break;
                }
                line = next != nullptr ? next + 1 : nullptr;
            }
            std::free(data);
        }
    }

    /* --- relógio --- */
    const std::time_t now = std::time(nullptr);

    /* Antes do NTP/RTC, epoch é ~1970: mostrar "00:00" como se fosse hora é
     * mentira. Abaixo de 2020-01-01 o relógio não foi ajustado. */
    out->valid_time = now > 1577836800 ? 1 : 0;

    if (out->valid_time) {
        struct tm local {};
        (void)localtime_r(&now, &local);
        out->hour = local.tm_hour;
        out->minute = local.tm_min;
    }
}

}  // namespace giochi
