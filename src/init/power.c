#include "power.h"

#include "giochi_log.h"
#include "giochi_util.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/reboot.h>
#endif

#define GIOCHI_ADAPTER_WLAN "/sys/class/net/wlan0"
#define GIOCHI_ADAPTER_HCI  "/sys/class/bluetooth/hci0"

static int path_exists(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0;
}

int giochi_power_execute(const char *action)
{
    if (action == NULL) {
        return -1;
    }

    if (giochi_is_dry_run()) {
        GLOG_INFO("dry-run: acao de energia '%s' ignorada", action);
        return 0;
    }

    sync();

    if (strcmp(action, "poweroff") == 0) {
#ifdef __linux__
        return reboot(RB_POWER_OFF) == 0 ? 0 : -1;
#else
        return -1;
#endif
    }
    if (strcmp(action, "reboot") == 0) {
#ifdef __linux__
        return reboot(RB_AUTOBOOT) == 0 ? 0 : -1;
#else
        return -1;
#endif
    }
    if (strcmp(action, "suspend") == 0) {
        FILE *fp = fopen("/sys/power/state", "w");

        if (fp == NULL) {
            GLOG_ERR("suspend: /sys/power/state indisponivel");
            return -1;
        }
        (void)fputs("mem", fp);
        (void)fclose(fp);
        return 0;
    }

    GLOG_ERR("acao de energia desconhecida: %s", action);
    return -1;
}

int giochi_usb_mode_set(const char *mode)
{
    char helper[512];
    char marker[256];
    const char *arg;
    const char *content;
    const char *argv[3];
    int rc;

    if (mode == NULL) {
        return -1;
    }
    if (strcmp(mode, "MTP") == 0) {
        arg = "mtp";
        content = "mtp\n";
    } else if (strcmp(mode, "MASS") == 0) {
        arg = "mass";
        content = "mass\n";
    } else if (strcmp(mode, "OFF") == 0) {
        arg = "off";
        content = "off\n";
    } else {
        return -1;
    }

    /* Estado persistido para diagnostico e para o proximo boot. */
    if (giochi_path(marker, sizeof(marker), "/run/giochi/usb_mode") != 0 ||
        giochi_write_file_atomic(marker, content, strlen(content)) != 0) {
        GLOG_WARN("nao foi possivel registrar usb_mode em /run/giochi");
    }

    if (giochi_is_dry_run()) {
        GLOG_INFO("dry-run: usb_mode -> %s", arg);
        return 0;
    }

    /* Secao 4.6/4.9.4: o mesmo helper do boot reconfigura o configfs. */
    if (giochi_path(helper, sizeof(helper), "/usr/sbin/giochi-usb-mode") != 0) {
        return -1;
    }
    argv[0] = helper;
    argv[1] = arg;
    argv[2] = NULL;

    rc = giochi_run(argv);
    if (rc != 0) {
        GLOG_ERR("usb_mode '%s': helper retornou %d", arg, rc);
        return -1;
    }
    GLOG_INFO("usb_mode alterado para %s", arg);
    return 0;
}

int giochi_wifi_set(int up)
{
    const char *argv_up[]   = { "ip", "link", "set", "wlan0", "up", NULL };
    const char *argv_down[] = { "ip", "link", "set", "wlan0", "down", NULL };

    if (!path_exists(GIOCHI_ADAPTER_WLAN)) {
        return -2;
    }
    if (giochi_is_dry_run()) {
        GLOG_INFO("dry-run: wifi %s", up ? "up" : "down");
        return 0;
    }

    /* TODO(v1.2): subir wpa_supplicant com as credenciais de settings.json
     * e disparar udhcpc em wlan0. */
    return giochi_run(up ? argv_up : argv_down) == 0 ? 0 : -1;
}

int giochi_bt_set(int up)
{
    const char *argv_unblock[] = { "rfkill", "unblock", "bluetooth", NULL };
    const char *argv_block[]   = { "rfkill", "block", "bluetooth", NULL };

    if (!path_exists(GIOCHI_ADAPTER_HCI)) {
        return -2;
    }
    if (giochi_is_dry_run()) {
        GLOG_INFO("dry-run: bluetooth %s", up ? "up" : "down");
        return 0;
    }

    /* TODO(v1.2): iniciar/parar bluetoothd (somente quando hci0 existe). */
    return giochi_run(up ? argv_unblock : argv_block) == 0 ? 0 : -1;
}
