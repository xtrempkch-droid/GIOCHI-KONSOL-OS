/* Giochi Konsol OS v1.2 — acoes de energia e de radio (control.sock). */
#ifndef GIOCHI_POWER_H
#define GIOCHI_POWER_H

/* action: "poweroff" | "reboot" | "suspend". Retorna 0 ou -1. */
int giochi_power_execute(const char *action);

/* Retorno: 0 ok | -1 erro | -2 adaptador ausente. */
int giochi_usb_mode_set(const char *mode);
int giochi_wifi_set(int up);
int giochi_bt_set(int up);

#endif /* GIOCHI_POWER_H */
