#include "device_profile.h"

#include "giochi_log.h"
#include "giochi_util.h"

#include <stdio.h>
#include <string.h>

#ifndef GIOCHI_SOC
#define GIOCHI_SOC "rk3326"
#endif

#ifndef GIOCHI_DEVICE
#define GIOCHI_DEVICE "generic"
#endif

#ifndef GIOCHI_OS_VERSION
#define GIOCHI_OS_VERSION "1.2"
#endif

#ifndef GIOCHI_KERNEL_VERSION
#define GIOCHI_KERNEL_VERSION "6.12"
#endif

typedef struct {
    const char *device;      /* GIOCHI_DEVICE                              */
    const char *soc;
    const char *cpu_model;
    int cpu_max_mhz;
    const char *gpu_model;
    int ram_mb;
    int vram_mb;
    int display_width;
    int display_height;
    int display_refresh_hz;
    int integrated_radio;    /* Wi-Fi/BT no proprio aparelho (ADR-14)      */
    int usb_gadget_ok;       /* X35S/X35H: USB nao ancora (F12)            */
    const char *proton_reason;
} giochi_device_values;

/* Valores de docs/hardware-e-drivers.md §1 e §3-§5. */
static const giochi_device_values g_devices[] = {
    { "x55",   "rk3566", "Cortex-A55", 1800, "Mali-G52", 2048, 128, 720, 1280, 60, 1, 1,
      "vulkan_immature" },
    { "x35s",  "rk3566", "Cortex-A55", 1800, "Mali-G52", 2048, 128, 640, 480, 60, 0, 0,
      "vulkan_immature" },
    { "x35h",  "rk3566", "Cortex-A55", 1800, "Mali-G52", 2048, 128, 640, 480, 60, 0, 0,
      "vulkan_immature" },
    { "rk3566", "rk3566", "Cortex-A55", 1800, "Mali-G52", 2048, 128, 640, 480, 60, 0, 1,
      "vulkan_immature" },
    { "rk3326", "rk3326", "Cortex-A35", 1500, "Mali-G31", 1024, 64, 640, 480, 60, 0, 1,
      "vulkan_absent" },
};

static giochi_device_values device_values(void)
{
    size_t i;

    for (i = 0; i < sizeof(g_devices) / sizeof(g_devices[0]); i++) {
        if (strcmp(g_devices[i].device, GIOCHI_DEVICE) == 0) {
            return g_devices[i];
        }
    }
    return strcmp(GIOCHI_SOC, "rk3566") == 0 ? g_devices[3] : g_devices[4];
}

const char *giochi_device_soc(void)
{
    return device_values().soc;
}

const char *giochi_device_id(void)
{
    return GIOCHI_DEVICE;
}

int giochi_device_profile_write(const char *path)
{
    giochi_device_values d = device_values();
    char device_id[96];
    char buf[4096];
    char dir[512];
    char *slash;
    int n;

    /* TODO(v1.2): serial de /proc/device-tree/serial-number no sufixo numerico. */
    if (strcmp(d.device, "generic") == 0 || strcmp(d.device, d.soc) == 0) {
        (void)snprintf(device_id, sizeof(device_id), "giochi-%s-001", d.soc);
    } else {
        (void)snprintf(device_id, sizeof(device_id), "giochi-%s-%s-001",
                       d.soc, d.device);
    }

    n = snprintf(buf, sizeof(buf),
        "{\n"
        "  \"schema_version\": \"1.0\",\n"
        "  \"device_id\": \"%s\",\n"
        "  \"soc\": \"%s\",\n"
        "  \"cpu\": { \"arch\": \"aarch64\", \"cores\": 4, \"model\": \"%s\", \"max_mhz\": %d },\n"
        "  \"gpu\": { \"model\": \"%s\", \"driver\": \"panfrost\", \"gles_max\": \"3.2\" },\n"
        "  \"ram_mb\": %d,\n"
        "  \"vram_mb\": %d,\n"
        "  \"storage\": { \"system_mb\": 160, \"user_free_mb\": 12000 },\n"
        "  \"os\": { \"name\": \"Giochi Konsol OS\", \"version\": \"%s\", \"kernel\": \"%s\" },\n"
        "  \"graphics_api\": { \"gles3\": true, \"gles2\": true, \"vulkan\": false },\n"
        "  \"audio\": { \"alsa\": true, \"dmix\": true, \"sample_rate\": 48000 },\n"
        "  \"input\": { \"evdev\": true, \"gamepad\": true, \"touchscreen\": false },\n"
        "  \"display\": { \"width\": %d, \"height\": %d, \"refresh_hz\": %d },\n"
        "  \"engine\": { \"godot_min\": \"3.5\", \"godot_max\": \"4.3\", \"renderer\": \"compatibility\" },\n"
        "  \"network\": { \"wifi_usb\": true, \"bluetooth_usb\": true, \"integrated\": %s },\n"
        "  \"usb_gadget\": { \"mtp\": true, \"mass_storage\": %s },\n"
        "  \"proton\": { \"supported\": false, \"reason\": \"%s\", \"min_ram_mb\": 4096, \"min_vulkan\": \"1.1\", \"fex_required\": true }\n"
        "}\n",
        device_id, d.soc, d.cpu_model, d.cpu_max_mhz, d.gpu_model,
        d.ram_mb, d.vram_mb, GIOCHI_OS_VERSION, GIOCHI_KERNEL_VERSION,
        d.display_width, d.display_height, d.display_refresh_hz,
        d.integrated_radio ? "true" : "false",
        d.usb_gadget_ok ? "true" : "false",
        d.proton_reason);

    if (n < 0 || (size_t)n >= sizeof(buf)) {
        GLOG_ERR("device_profile.json excede o buffer interno");
        return -1;
    }

    giochi_strlcpy(dir, path, sizeof(dir));
    slash = strrchr(dir, '/');
    if (slash != NULL && slash != dir) {
        *slash = '\0';
        if (giochi_mkdir_p(dir, 0755) != 0) {
            GLOG_ERR("nao foi possivel criar %s", dir);
            return -1;
        }
    }

    if (giochi_write_file_atomic(path, buf, (size_t)n) != 0) {
        GLOG_ERR("nao foi possivel gravar %s", path);
        return -1;
    }

    GLOG_INFO("device_profile.json gravado em %s (device=%s soc=%s)", path,
              d.device, d.soc);
    return 0;
}
