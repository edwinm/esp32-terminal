// sd_card.c — microSD over the dedicated SPI2 bus (CS=1, MOSI=2, MISO=41, SCK=42).
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include "sd_card.h"
#include "board_pins.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "sd";
static sdmmc_card_t *s_card = NULL;
static bool s_spi_inited = false;

bool sd_is_mounted(void) { return s_card != NULL; }

esp_err_t sd_mount(void)
{
    if (s_card) return ESP_OK;

    if (!s_spi_inited) {
        spi_bus_config_t bus = {
            .mosi_io_num = SD_PIN_MOSI,
            .miso_io_num = SD_PIN_MISO,
            .sclk_io_num = SD_PIN_SCK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4096,
        };
        esp_err_t err = spi_bus_initialize(SD_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
        if (err != ESP_OK) { ESP_LOGE(TAG, "spi init: %s", esp_err_to_name(err)); return err; }
        s_spi_inited = true;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;
    host.max_freq_khz = SD_MAX_FREQ_KHZ;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = SD_PIN_CS;
    slot.host_id = SD_SPI_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(err));
        s_card = NULL;
    }
    return err;
}

esp_err_t sd_unmount(void)
{
    if (!s_card) return ESP_OK;
    esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    s_card = NULL;
    if (s_spi_inited) { spi_bus_free(SD_SPI_HOST); s_spi_inited = false; }
    return ESP_OK;
}

esp_err_t sd_get_info(sd_info_t *out)
{
    if (!s_card) return ESP_ERR_INVALID_STATE;
    memset(out, 0, sizeof(*out));
    strlcpy(out->name, s_card->cid.name, sizeof(out->name));
    out->capacity_mb = ((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) / (1024 * 1024);
    out->speed_khz = s_card->max_freq_khz;
    if (s_card->is_mmc) strlcpy(out->type, "MMC", sizeof(out->type));
    else strlcpy(out->type, (s_card->ocr & (1 << 30)) ? "SDHC" : "SDSC", sizeof(out->type));
    return ESP_OK;
}

esp_err_t sd_list_root(sd_list_cb_t cb, void *ctx)
{
    if (!s_card) return ESP_ERR_INVALID_STATE;
    DIR *dir = opendir(SD_MOUNT_POINT);
    if (!dir) return ESP_FAIL;
    struct dirent *de;
    char path[300];
    while ((de = readdir(dir)) != NULL) {
        struct stat st = {0};
        snprintf(path, sizeof(path), "%s/%s", SD_MOUNT_POINT, de->d_name);
        stat(path, &st);
        cb(de->d_name, de->d_type == DT_DIR, (uint32_t)st.st_size, ctx);
    }
    closedir(dir);
    return ESP_OK;
}

void sd_speed_test(int mb, sd_speed_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!s_card) { out->err = ESP_ERR_INVALID_STATE; return; }

    const size_t chunk = 32 * 1024;
    const int chunks = (mb * 1024 * 1024) / chunk;
    uint8_t *buf = heap_caps_malloc(chunk, MALLOC_CAP_DEFAULT);
    if (!buf) { out->err = ESP_ERR_NO_MEM; return; }
    memset(buf, 0xA5, chunk);

    const char *path = SD_MOUNT_POINT "/speed.tst";

    // Write
    FILE *f = fopen(path, "wb");
    if (!f) { free(buf); out->err = ESP_FAIL; return; }
    int64_t t0 = esp_timer_get_time();
    size_t written = 0;
    for (int i = 0; i < chunks; i++) {
        if (fwrite(buf, 1, chunk, f) != chunk) break;
        written += chunk;
    }
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    int64_t t1 = esp_timer_get_time();

    // Read
    f = fopen(path, "rb");
    size_t read_total = 0;
    if (f) {
        while (fread(buf, 1, chunk, f) == chunk) read_total += chunk;
        fclose(f);
    }
    int64_t t2 = esp_timer_get_time();

    remove(path);
    free(buf);

    double ws = (t1 - t0) / 1e6, rs = (t2 - t1) / 1e6;
    out->write_mbps = ws > 0 ? (written / 1e6) / ws : 0;
    out->read_mbps  = rs > 0 ? (read_total / 1e6) / rs : 0;
    out->bytes = written;
    out->err = ESP_OK;
    ESP_LOGI(TAG, "SD write %.2f MB/s, read %.2f MB/s", out->write_mbps, out->read_mbps);
}
