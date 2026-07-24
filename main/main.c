// main.c — Makerfabs ESP32-S3 Parallel TFT 3.5" hardware demo.
//
// Boot order: NVS -> I2C bus -> temp sensor -> LovyanGFX display (color gate) ->
// touch detect -> LVGL UI -> USB HID.
#include "board_pins.h"
#include "lcd.h"
#include "sys_info.h"
#include "usb_hid.h"
#include "console_uart.h"
#include "ui/ui.h"

#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app";

void app_main(void)
{
    ESP_LOGI(TAG, "Makerfabs ESP32-S3 Parallel TFT 3.5\" demo starting");

    // NVS is needed by WiFi later; init early and tolerate a version bump.
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    sys_info_init();

    // Display + touch bring-up via LovyanGFX (also owns the shared I2C bus).
    ESP_ERROR_CHECK(lcd_init());
    lcd_raw_fill(0x0000);          // clear to black (backlight stays off until UI)

    // LVGL UI (takes over the display and ramps the backlight up).
    ui_start();

    // Serial console on the 2nd Mabee connector (GPIO43/44, shared with logs).
    // Started after the UI so the Console tab's RX callback is already registered.
    console_uart_init(115200);

    // Native USB HID keyboard (independent of the UART console).
    usb_hid_init();

    ESP_LOGI(TAG, "init complete");
}
