// usb_cdc.c — TinyUSB CDC-ACM on the native USB-OTG port.
//
// Flow control is the USB stack's: the RX callback only wakes the reader, and
// if the reader falls behind, esp_tinyusb's ring buffer fills, the bulk OUT
// endpoint NAKs, and the host's write() blocks. No byte gets dropped because we
// were busy repainting.
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"

#include "term.h"
#include "usb_cdc.h"

static const char *TAG = "usbcdc";

// USB string descriptors. esp_tinyusb's defaults would hand every board the
// same serial ("123456"), which makes the udev rule in the README useless as
// soon as there is a second CDC device on the host — so the serial is built
// from this chip's eFuse MAC instead. Index 0 is the language ID, and only its
// first two bytes are read.
static const char k_langid[3] = { 0x09, 0x04, 0x00 };   // English (US)
static char        s_serial[13];
static const char *s_strings[5];

static TaskHandle_t      s_reader;
static SemaphoreHandle_t s_tx_lock;
static StaticSemaphore_t s_tx_lock_buf;
static volatile bool     s_dtr;
static volatile uint32_t s_rx_count;

// --- TinyUSB callbacks ------------------------------------------------------

// Runs on the tusb_device task. Do nothing here but wake the reader: blocking
// this callback stalls control transfers and the host decides the device died.
static void on_rx(int itf, cdcacm_event_t *event)
{
    (void)itf; (void)event;
    if (s_reader) xTaskNotifyGive(s_reader);
}

static void on_line_state(int itf, cdcacm_event_t *event)
{
    (void)itf;
    s_dtr = event->line_state_changed_data.dtr;
    ESP_LOGI(TAG, "line state: dtr=%d rts=%d",
             (int)event->line_state_changed_data.dtr,
             (int)event->line_state_changed_data.rts);
}

// --- Public -----------------------------------------------------------------

esp_err_t usb_cdc_init(void)
{
    s_tx_lock = xSemaphoreCreateMutexStatic(&s_tx_lock_buf);
    s_reader = xTaskGetCurrentTaskHandle();

    uint8_t mac[6] = { 0 };
    esp_efuse_mac_get_default(mac);
    snprintf(s_serial, sizeof(s_serial), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    s_strings[0] = k_langid;
    s_strings[1] = "Makerfabs";
    s_strings[2] = "ESP32-S3 Serial Terminal";
    s_strings[3] = s_serial;
    s_strings[4] = "ESP32-S3 Terminal CDC";

    // The device and configuration descriptors stay at esp_tinyusb's defaults:
    // with CONFIG_TINYUSB_CDC_ENABLED they already declare the IAD device class
    // (0xEF/0x02/0x01) that CDC needs. Only the strings are ours.
    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG();
    cfg.descriptor.string = s_strings;
    cfg.descriptor.string_count = sizeof(s_strings) / sizeof(s_strings[0]);

    esp_err_t err = tinyusb_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install: %s", esp_err_to_name(err));
        return err;
    }

    tinyusb_config_cdcacm_t acm = {
        .cdc_port                    = TINYUSB_CDC_ACM_0,
        .callback_rx                 = on_rx,
        .callback_rx_wanted_char     = NULL,
        .callback_line_state_changed = on_line_state,
        .callback_line_coding_changed = NULL,
    };
    err = tinyusb_cdcacm_init(&acm);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_cdcacm_init: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "CDC-ACM up on the native USB port, serial %s", s_serial);
    return ESP_OK;
}

size_t usb_cdc_read(uint8_t *buf, size_t max, uint32_t timeout_ms)
{
    // The reader task registers itself here rather than at init time, because
    // init runs on app_main and the draining happens on the `term` task.
    s_reader = xTaskGetCurrentTaskHandle();

    size_t total = 0;
    for (;;) {
        size_t got = 0;
        esp_err_t err = tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, buf + total,
                                            max - total, &got);
        if (err != ESP_OK || got == 0) break;
        total += got;
        if (total >= max) break;
    }
    if (total > 0) {
        s_rx_count += total;
        return total;
    }

    // Nothing buffered: wait to be woken by the RX callback. The timeout keeps
    // the caller's loop alive so it can notice a detach.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms));

    total = 0;
    for (;;) {
        size_t got = 0;
        esp_err_t err = tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, buf + total,
                                            max - total, &got);
        if (err != ESP_OK || got == 0) break;
        total += got;
        if (total >= max) break;
    }
    s_rx_count += total;
    return total;
}

void usb_cdc_write(const uint8_t *data, size_t len)
{
    if (!tud_mounted() || len == 0) return;

    xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    size_t sent = 0;
    int stalls = 0;
    while (sent < len) {
        size_t n = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, data + sent, len - sent);
        if (n == 0) {
            // TX FIFO full: flush and give the host a moment. Bail out rather
            // than spin — we must never block the render or parser task.
            if (tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(20)) != ESP_OK
                || ++stalls > 4) {
                break;
            }
            continue;
        }
        sent += n;
    }
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(20));
    xSemaphoreGive(s_tx_lock);
}

usb_link_state_t usb_cdc_link_state(void)
{
    if (!tud_mounted()) return USB_LINK_DETACHED;
    return s_dtr ? USB_LINK_OPEN : USB_LINK_MOUNTED;
}

uint32_t usb_cdc_rx_count(void) { return s_rx_count; }

// The parser's reply channel (DA, CPR, ...) — declared in term.h so the term
// module does not have to know what the transport is.
void term_reply(const char *s, int len)
{
    usb_cdc_write((const uint8_t *)s, (size_t)len);
}
