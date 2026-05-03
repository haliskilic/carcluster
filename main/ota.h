#pragma once
#include <stdbool.h>
#include <stdint.h>

/* OTA — HTTP/HTTPS endpoint'ten firmware indir, ota_0/ota_1'e yaz, switch + reboot.
 * 2 OTA partition + factory fallback (anti-brick): boot count'lu doğrulama yok,
 * basit replace mode. Hatalı flash sonrası bootloader esp_ota_get_app_partition
 * eski boot partition'ı korur (esp_ota_set_boot_partition başarısızsa).
 *
 * Akış:
 *   1) esp_ota_begin(next_partition, OTA_SIZE_UNKNOWN, &handle)
 *   2) HTTP loop: chunk read → esp_ota_write
 *   3) esp_ota_end + esp_ota_set_boot_partition
 *   4) esp_restart()  (yeni partition'dan boot eder) */

typedef enum {
    OTA_IDLE        = 0,
    OTA_DOWNLOADING = 1,
    OTA_VERIFYING   = 2,
    OTA_DONE        = 3,   /* reboot şart */
    OTA_FAILED      = 4,
} ota_state_t;

typedef struct {
    ota_state_t state;
    int         pct;        /* 0..100, downloading sırasında */
    char        msg[64];    /* hata veya progress detay */
} ota_status_t;

void ota_set_url(const char *url);     /* NVS persist */
void ota_start(void);                  /* arka plan task spawn */
void ota_get_status(ota_status_t *out);
