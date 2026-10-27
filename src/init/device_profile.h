/* Giochi Konsol OS v1.2 — geracao de /user/.giochi/device_profile.json. */
#ifndef GIOCHI_DEVICE_PROFILE_H
#define GIOCHI_DEVICE_PROFILE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Definidos pela build (Buildroot / -DGIOCHI_SOC=... -DGIOCHI_DEVICE=...). */
const char *giochi_device_soc(void);
const char *giochi_device_id(void);

int giochi_device_profile_write(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* GIOCHI_DEVICE_PROFILE_H */
