// wifi_scan.c — passive AP discovery. Blocking scan keeps the UI code simple;
// callers run it from a short-lived worker task.
#include <string.h>
#include "wifi_scan.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "wifi";
static bool s_inited = false;

static const char *auth_str(wifi_auth_mode_t m)
{
    switch (m) {
        case WIFI_AUTH_OPEN:            return "open";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/2";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/3";
        case WIFI_AUTH_ENTERPRISE:      return "EAP";
        default:                        return "?";
    }
}

esp_err_t wifi_scan_init(void)
{
    if (s_inited) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;

    ESP_ERROR_CHECK(esp_netif_init());
    if (esp_event_loop_create_default() != ESP_OK) { /* may already exist */ }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_inited = true;
    ESP_LOGI(TAG, "WiFi STA ready for scanning");
    return ESP_OK;
}

esp_err_t wifi_scan_run(wifi_ap_t *out, size_t max, uint16_t *found)
{
    *found = 0;
    esp_err_t err = wifi_scan_init();
    if (err != ESP_OK) return err;

    wifi_scan_config_t scan_cfg = { .show_hidden = false };
    err = esp_wifi_scan_start(&scan_cfg, true /* block */);
    if (err != ESP_OK) return err;

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) return ESP_OK;

    wifi_ap_record_t *recs = calloc(ap_count, sizeof(wifi_ap_record_t));
    if (!recs) return ESP_ERR_NO_MEM;
    esp_wifi_scan_get_ap_records(&ap_count, recs);

    size_t n = 0;
    for (uint16_t i = 0; i < ap_count && n < max; i++) {
        strlcpy(out[n].ssid, (const char *)recs[i].ssid, sizeof(out[n].ssid));
        if (out[n].ssid[0] == '\0') strlcpy(out[n].ssid, "(hidden)", sizeof(out[n].ssid));
        out[n].rssi = recs[i].rssi;
        out[n].channel = recs[i].primary;
        strlcpy(out[n].auth, auth_str(recs[i].authmode), sizeof(out[n].auth));
        n++;
    }
    free(recs);
    *found = n;
    return ESP_OK;
}
