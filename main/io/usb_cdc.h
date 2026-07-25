// usb_cdc.h — the terminal link: a USB CDC-ACM device on the native USB-OTG
// port. On a Linux host this appears as /dev/ttyACM0; run a getty on it.
//
// The CP2104 port is untouched and keeps doing flashing + IDF logs, so debug
// output never pollutes the terminal stream.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    USB_LINK_DETACHED = 0,   // no host
    USB_LINK_MOUNTED,        // enumerated, but the host has not asserted DTR
    USB_LINK_OPEN,           // enumerated and DTR asserted (a getty has it open)
} usb_link_state_t;

esp_err_t usb_cdc_init(void);

// Block until bytes arrive (or the timeout expires), then drain everything
// available into `buf`. Returns the byte count, 0 on timeout.
size_t usb_cdc_read(uint8_t *buf, size_t max, uint32_t timeout_ms);

// Non-blocking, best effort. Dropped silently when no host is attached — a
// terminal that stalls on an unasserted control line is a miserable thing to
// debug.
void usb_cdc_write(const uint8_t *data, size_t len);

usb_link_state_t usb_cdc_link_state(void);

// Total bytes received, for the status strip's activity indicator.
uint32_t usb_cdc_rx_count(void);
