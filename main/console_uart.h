// console_uart.h — UART on the second Mabee connector (GPIO43 TX / GPIO44 RX).
//
// These are the UART0 pins, shared with the ESP-IDF debug console: we install
// the UART driver on UART0 and route console output through it, so debug logs
// and app data coexist on the same wire (they interleave — expected).
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*console_rx_cb_t)(const char *data, int len, void *ctx);

// Install the shared UART0 driver on GPIO43/44 at `baud` and start the RX task.
void console_uart_init(int baud);

void console_uart_set_baud(int baud);
int  console_uart_baud(void);

void console_uart_send(const char *data, int len);

// Register a callback invoked (from the RX task) when bytes arrive.
void console_uart_set_rx_cb(console_rx_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
