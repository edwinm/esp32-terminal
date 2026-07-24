// console_uart.c — shared UART0 (GPIO43/44) for the Console tab + debug log.
#include "console_uart.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "con";

#define CON_UART   UART_NUM_0
#define CON_TX     43
#define CON_RX     44
#define RX_BUF_SZ  4096

static console_rx_cb_t s_cb = NULL;
static void           *s_ctx = NULL;
static int             s_baud = 115200;

static void rx_task(void *arg)
{
    (void)arg;
    uint8_t buf[257];
    for (;;) {
        int n = uart_read_bytes(CON_UART, buf, sizeof(buf) - 1, pdMS_TO_TICKS(50));
        if (n > 0 && s_cb) {
            buf[n] = 0;
            s_cb((const char *)buf, n, s_ctx);
        }
    }
}

void console_uart_init(int baud)
{
    s_baud = baud;
    uart_config_t cfg = {
        .baud_rate = baud,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    // UART0 is the console; install the driver and route console I/O through it
    // so log output and app writes share the TX path without low-level clashes.
    uart_driver_install(CON_UART, RX_BUF_SZ, 0, 0, NULL, 0);
    uart_param_config(CON_UART, &cfg);
    uart_set_pin(CON_UART, CON_TX, CON_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_vfs_dev_use_driver(CON_UART);

    xTaskCreate(rx_task, "con_rx", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "console UART on GPIO%d/%d @ %d baud (shared with debug log)",
             CON_TX, CON_RX, baud);
}

void console_uart_set_baud(int baud)
{
    s_baud = baud;
    uart_set_baudrate(CON_UART, baud);
}

int console_uart_baud(void) { return s_baud; }

void console_uart_send(const char *data, int len)
{
    uart_write_bytes(CON_UART, data, len);
}

void console_uart_set_rx_cb(console_rx_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_ctx = ctx;
}
