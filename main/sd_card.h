// sd_card.h — microSD over SPI: mount, info, directory listing, speed test.
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char     name[24];
    uint64_t capacity_mb;
    uint32_t speed_khz;
    char     type[8];       // "SDHC", "SDSC", "MMC"
} sd_info_t;

typedef struct {
    float write_mbps;
    float read_mbps;
    size_t bytes;
    esp_err_t err;
} sd_speed_t;

esp_err_t sd_mount(void);
esp_err_t sd_unmount(void);
bool      sd_is_mounted(void);
esp_err_t sd_get_info(sd_info_t *out);

// Callback per root-directory entry. `is_dir` and size in bytes.
typedef void (*sd_list_cb_t)(const char *name, bool is_dir, uint32_t size, void *ctx);
esp_err_t sd_list_root(sd_list_cb_t cb, void *ctx);

// Write then read back a `mb`-megabyte file, measuring throughput. Blocks.
void sd_speed_test(int mb, sd_speed_t *out);

#ifdef __cplusplus
}
#endif
