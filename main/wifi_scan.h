// wifi_scan.h — station-mode AP scan (no association, no credentials).
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char    ssid[33];
    int8_t  rssi;
    uint8_t channel;
    char    auth[12];   // "open", "WPA2", ...
} wifi_ap_t;

// Lazy one-time WiFi init in STA mode.
esp_err_t wifi_scan_init(void);

// Blocking scan. Fills `out` (up to `max`); returns count via *found.
esp_err_t wifi_scan_run(wifi_ap_t *out, size_t max, uint16_t *found);

#ifdef __cplusplus
}
#endif
