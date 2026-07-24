// sys_info.h — chip / memory / temperature / uptime snapshot for the Info tab.
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char     chip_model[16];
    int      chip_revision;
    int      cpu_cores;
    uint32_t cpu_mhz;
    char     idf_version[32];
    uint32_t flash_size_mb;
    uint32_t psram_total_kb;
    uint32_t psram_free_kb;
    uint32_t heap_total_kb;
    uint32_t heap_free_kb;
    uint32_t heap_min_free_kb;   // low-water mark
    uint64_t uptime_s;
} sys_info_t;

void  sys_info_init(void);            // installs the internal temperature sensor
void  sys_info_get(sys_info_t *out);
float sys_temp_read(void);            // ESP32-S3 die temperature, Celsius

#ifdef __cplusplus
}
#endif
