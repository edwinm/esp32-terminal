// usb_hid.h — native USB-C HID keyboard (TinyUSB). Types into the USB host.
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t usb_hid_init(void);
bool      usb_hid_mounted(void);            // host enumerated us?

// Type an ASCII string / send a single HID keycode. Non-blocking (spawns a
// short worker task); no-op if no host is attached.
void usb_hid_type_string(const char *s);
void usb_hid_send_key(uint8_t keycode);     // HID_KEY_* (e.g. arrows)

#ifdef __cplusplus
}
#endif
