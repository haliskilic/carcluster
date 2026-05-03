#include "screenshot.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "board.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

/* Inline base64 encoder — RFC 4648, padding ile. mbedtls/check_config.h
 * platform mantığı bizim build'de patlattı, küçük inline implementation
 * dependency-free + yeterince hızlı (snapshot dump tek seferlik). */
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t b64_encode(char *out, const uint8_t *in, size_t in_len)
{
    size_t i = 0, o = 0;
    while (i + 3 <= in_len) {
        uint32_t n = ((uint32_t)in[i] << 16) | ((uint32_t)in[i+1] << 8) | in[i+2];
        out[o++] = B64[(n >> 18) & 0x3F];
        out[o++] = B64[(n >> 12) & 0x3F];
        out[o++] = B64[(n >>  6) & 0x3F];
        out[o++] = B64[ n        & 0x3F];
        i += 3;
    }
    if (i < in_len) {
        uint32_t n = (uint32_t)in[i] << 16;
        if (i + 1 < in_len) n |= (uint32_t)in[i+1] << 8;
        out[o++] = B64[(n >> 18) & 0x3F];
        out[o++] = B64[(n >> 12) & 0x3F];
        out[o++] = (i + 1 < in_len) ? B64[(n >> 6) & 0x3F] : '=';
        out[o++] = '=';
    }
    out[o] = 0;
    return o;
}

static const char *TAG = "shot";

void screenshot_dump_uart(void)
{
    ESP_LOGI(TAG, "screenshot_dump_uart: start");
    /* USB-Serial-JTAG console buffer'ı tutuyor — line-buffered devre dışı,
     * her printf sonrası elle flush ediyoruz aksi halde host hiçbir şey görmez */
    setvbuf(stdout, NULL, _IONBF, 0);

    int w = LCD_H_RES, h = LCD_V_RES;
    /* Snapshot needed buffer = w * h * sizeof(lv_color_t). LV_COLOR_DEPTH=16
     * konfigürasyonumuzda RGB565 → 2 byte per pixel → 800×480×2 = 768000. */
    size_t buf_size = (size_t)w * h * sizeof(lv_color_t);

    uint8_t *buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "PSRAM alloc failed (%u bytes)", (unsigned)buf_size);
        printf("[SHOT-ERROR no memory]\n");
        return;
    }

    lv_img_dsc_t dsc;
    memset(&dsc, 0, sizeof(dsc));

    lvgl_port_lock();
    lv_res_t r = lv_snapshot_take_to_buf(lv_scr_act(), LV_IMG_CF_TRUE_COLOR,
                                          &dsc, buf, buf_size);
    lvgl_port_unlock();

    if (r != LV_RES_OK) {
        ESP_LOGE(TAG, "lv_snapshot_take_to_buf failed (%d)", r);
        printf("[SHOT-ERROR snapshot failed]\n");
        free(buf);
        return;
    }

    ESP_LOGI(TAG, "snapshot %dx%d, %u bytes — dumping to UART", w, h, (unsigned)buf_size);
    printf("[SHOT-BEGIN W=%d H=%d FMT=RGB565 BYTES=%u]\n",
           w, h, (unsigned)buf_size);
    fflush(stdout);

    /* base64 chunked — 57 byte input → 76 byte output (RFC 2045 satır limiti).
     * fflush her satırda — USB-Serial-JTAG buffer'ı zorla boşalt. */
    char outbuf[80];
    size_t i = 0;
    while (i < buf_size) {
        size_t chunk_in = (buf_size - i > 57) ? 57 : (buf_size - i);
        b64_encode(outbuf, buf + i, chunk_in);
        printf("%s\n", outbuf);
        i += chunk_in;
        if ((i & 0xFFF) == 0) {
            fflush(stdout);
            vTaskDelay(1);   /* TWDT ve task starvation breath */
        }
    }
    fflush(stdout);
    printf("[SHOT-END]\n");
    fflush(stdout);

    free(buf);
    ESP_LOGI(TAG, "dump done");
}
