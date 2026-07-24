// usb_hid.c — USB HID keyboard on the native USB-OTG port via esp_tinyusb.
//
// The UART console/flashing path (CP2104) is untouched; this uses the *other*
// USB-C connector (native OTG on GPIO19/20).
#include <string.h>
#include "usb_hid.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "usbhid";

// --- HID report descriptor (single keyboard interface) ---------------------
static const uint8_t hid_report_desc[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(1)),
};

// --- String descriptors ----------------------------------------------------
static const char *hid_string_desc[] = {
    (char[]){0x09, 0x04},   // 0: language (English)
    "Makerfabs",            // 1: manufacturer
    "S3 TFT35 Demo KBD",    // 2: product
    "123456",               // 3: serial
    "HID Keyboard",         // 4: HID interface
};

// --- Configuration descriptor ----------------------------------------------
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static const uint8_t hid_config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    // Interface, string idx 4, boot protocol keyboard, report desc len, EP IN, size, poll ms
    TUD_HID_DESCRIPTOR(0, 4, HID_ITF_PROTOCOL_KEYBOARD, sizeof(hid_report_desc),
                       0x81, 16, 10),
};

// --- TinyUSB HID callbacks (required) --------------------------------------
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return hid_report_desc;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen)
{
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize)
{
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}

bool usb_hid_mounted(void) { return tud_mounted(); }

// --- ASCII -> HID keycode (US layout, printable range) ---------------------
// {modifier, keycode}; modifier bit0 = left shift.
static void ascii_to_keycode(char c, uint8_t *mod, uint8_t *key)
{
    *mod = 0; *key = 0;
    if (c >= 'a' && c <= 'z') { *key = HID_KEY_A + (c - 'a'); }
    else if (c >= 'A' && c <= 'Z') { *mod = KEYBOARD_MODIFIER_LEFTSHIFT; *key = HID_KEY_A + (c - 'A'); }
    else if (c >= '1' && c <= '9') { *key = HID_KEY_1 + (c - '1'); }
    else if (c == '0') { *key = HID_KEY_0; }
    else switch (c) {
        case ' ': *key = HID_KEY_SPACE; break;
        case '\n': *key = HID_KEY_ENTER; break;
        case '\t': *key = HID_KEY_TAB; break;
        case '-': *key = HID_KEY_MINUS; break;
        case '_': *mod = KEYBOARD_MODIFIER_LEFTSHIFT; *key = HID_KEY_MINUS; break;
        case '.': *key = HID_KEY_PERIOD; break;
        case ',': *key = HID_KEY_COMMA; break;
        case '!': *mod = KEYBOARD_MODIFIER_LEFTSHIFT; *key = HID_KEY_1; break;
        case '?': *mod = KEYBOARD_MODIFIER_LEFTSHIFT; *key = HID_KEY_SLASH; break;
        case '/': *key = HID_KEY_SLASH; break;
        case ':': *mod = KEYBOARD_MODIFIER_LEFTSHIFT; *key = HID_KEY_SEMICOLON; break;
        default: break;
    }
}

static void send_key(uint8_t mod, uint8_t key)
{
    if (!tud_mounted()) return;
    uint8_t keys[6] = {key, 0, 0, 0, 0, 0};
    tud_hid_keyboard_report(1, mod, key ? keys : NULL);
    vTaskDelay(pdMS_TO_TICKS(12));
    tud_hid_keyboard_report(1, 0, NULL);   // release
    vTaskDelay(pdMS_TO_TICKS(12));
}

static void type_task(void *arg)
{
    char *s = (char *)arg;
    for (const char *p = s; *p; p++) {
        uint8_t mod, key;
        ascii_to_keycode(*p, &mod, &key);
        if (key) send_key(mod, key);
    }
    free(s);
    vTaskDelete(NULL);
}

void usb_hid_type_string(const char *s)
{
    if (!tud_mounted()) { ESP_LOGW(TAG, "no USB host attached"); return; }
    char *copy = strdup(s);
    if (!copy) return;
    xTaskCreate(type_task, "hid_type", 3072, copy, 3, NULL);
}

static void key_task(void *arg)
{
    uint8_t key = (uint8_t)(uintptr_t)arg;
    send_key(0, key);
    vTaskDelete(NULL);
}

void usb_hid_send_key(uint8_t keycode)
{
    if (!tud_mounted()) return;
    xTaskCreate(key_task, "hid_key", 2048, (void *)(uintptr_t)keycode, 3, NULL);
}

esp_err_t usb_hid_init(void)
{
    // esp_tinyusb 2.x: start from the default (full-speed on S3) config, then
    // point the descriptor at our HID keyboard strings + config descriptor.
    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG();
    cfg.descriptor.device = NULL;   // fall back to the stack's default device desc
    cfg.descriptor.string = hid_string_desc;
    cfg.descriptor.string_count = sizeof(hid_string_desc) / sizeof(hid_string_desc[0]);
    cfg.descriptor.full_speed_config = hid_config_desc;

    esp_err_t err = tinyusb_driver_install(&cfg);
    if (err != ESP_OK) ESP_LOGE(TAG, "tinyusb install: %s", esp_err_to_name(err));
    else ESP_LOGI(TAG, "USB HID keyboard installed");
    return err;
}
