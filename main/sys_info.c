// sys_info.c — runtime system stats + internal die-temperature sensor.
#include <string.h>
#include "sys_info.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_idf_version.h"
#include "driver/temperature_sensor.h"
#include "esp_log.h"

static const char *TAG = "sysinfo";
static temperature_sensor_handle_t s_tsens = NULL;

void sys_info_init(void)
{
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&cfg, &s_tsens) == ESP_OK) {
        temperature_sensor_enable(s_tsens);
    } else {
        ESP_LOGW(TAG, "temperature sensor unavailable");
    }
}

float sys_temp_read(void)
{
    float t = 0.0f;
    if (s_tsens && temperature_sensor_get_celsius(s_tsens, &t) == ESP_OK) return t;
    return -1000.0f;  // sentinel: unavailable
}

void sys_info_get(sys_info_t *out)
{
    memset(out, 0, sizeof(*out));

    esp_chip_info_t ci;
    esp_chip_info(&ci);
    strlcpy(out->chip_model, "ESP32-S3", sizeof(out->chip_model));
    out->chip_revision = ci.revision;
    out->cpu_cores = ci.cores;
    out->cpu_mhz = 240;
    snprintf(out->idf_version, sizeof(out->idf_version), "%s", esp_get_idf_version());

    uint32_t flash_bytes = 0;
    esp_flash_get_size(NULL, &flash_bytes);
    out->flash_size_mb = flash_bytes / (1024 * 1024);

    out->psram_total_kb = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024;
    out->psram_free_kb  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;

    out->heap_total_kb    = heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024;
    out->heap_free_kb     = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    out->heap_min_free_kb = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024;

    out->uptime_s = (uint64_t)(esp_timer_get_time() / 1000000ULL);
}
