# Giochi Konsol OS v1.2 — BR2_EXTERNAL root.

# -march por SoC (secao 1.1). O -mtune vem do BR2_cortex_aXX do Buildroot.
ifeq ($(BR2_cortex_a35),y)
GIOCHI_ARCH = armv8-a+crc
else ifeq ($(BR2_cortex_a55),y)
GIOCHI_ARCH = armv8-a+crc+crypto
else
GIOCHI_ARCH = armv8-a
endif

include $(sort $(wildcard $(BR2_EXTERNAL_GIOCHI_PATH)/package/*/*.mk))
