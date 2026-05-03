#include "ota.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "ota";

#define NS       "carcluster"
#define KEY_URL  "ota_url"

static char        s_url[256] = {0};
static volatile ota_state_t s_state = OTA_IDLE;
static volatile int s_pct = 0;
static char        s_msg[64] = {0};

static void set_msg(const char *m) {
    strncpy(s_msg, m, sizeof(s_msg) - 1);
    s_msg[sizeof(s_msg) - 1] = 0;
}

void ota_set_url(const char *url)
{
    strncpy(s_url, url, sizeof(s_url) - 1);
    s_url[sizeof(s_url) - 1] = 0;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, KEY_URL, url);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "URL: %s", s_url);
}

static void load_url(void) {
    if (s_url[0]) return;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(s_url);
        nvs_get_str(h, KEY_URL, s_url, &sz);
        nvs_close(h);
    }
}

static void ota_task(void *arg)
{
    (void)arg;
    load_url();
    if (!s_url[0]) {
        s_state = OTA_FAILED;
        set_msg("URL not set (use OTA URL <http://...>)");
        ESP_LOGE(TAG, "%s", s_msg);
        vTaskDelete(NULL);
        return;
    }

    s_state = OTA_DOWNLOADING;
    s_pct = 0;
    set_msg("connecting...");
    ESP_LOGI(TAG, "starting OTA from %s", s_url);

    esp_http_client_config_t hcfg = {
        .url = s_url,
        .timeout_ms = 10000,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t hc = esp_http_client_init(&hcfg);
    if (!hc) { set_msg("http_client_init failed"); goto fail; }

    if (esp_http_client_open(hc, 0) != ESP_OK) {
        set_msg("http_client_open failed");
        goto fail_close;
    }
    int total = esp_http_client_fetch_headers(hc);
    if (total <= 0) {
        snprintf(s_msg, sizeof(s_msg), "fetch_headers: bad len=%d", total);
        goto fail_close;
    }
    int status = esp_http_client_get_status_code(hc);
    if (status != 200) {
        snprintf(s_msg, sizeof(s_msg), "HTTP status %d", status);
        goto fail_close;
    }
    ESP_LOGI(TAG, "content-length: %d", total);

    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (!next) { set_msg("no next OTA partition"); goto fail_close; }
    ESP_LOGI(TAG, "writing to %s @ 0x%lx", next->label, (unsigned long)next->address);

    esp_ota_handle_t oh = 0;
    esp_err_t er = esp_ota_begin(next, total, &oh);
    if (er != ESP_OK) {
        snprintf(s_msg, sizeof(s_msg), "ota_begin: %s", esp_err_to_name(er));
        goto fail_close;
    }

    char buf[2048];
    int got = 0;
    while (got < total) {
        int n = esp_http_client_read(hc, buf, sizeof(buf));
        if (n <= 0) {
            snprintf(s_msg, sizeof(s_msg), "http_read short at %d/%d", got, total);
            esp_ota_abort(oh);
            goto fail_close;
        }
        er = esp_ota_write(oh, buf, n);
        if (er != ESP_OK) {
            snprintf(s_msg, sizeof(s_msg), "ota_write: %s", esp_err_to_name(er));
            esp_ota_abort(oh);
            goto fail_close;
        }
        got += n;
        s_pct = (int)((int64_t)got * 100 / total);
        if ((got & 0xFFFF) == 0) {
            ESP_LOGI(TAG, "%d%% (%d/%d)", s_pct, got, total);
        }
    }

    s_state = OTA_VERIFYING;
    set_msg("verifying...");
    er = esp_ota_end(oh);
    if (er != ESP_OK) {
        snprintf(s_msg, sizeof(s_msg), "ota_end: %s", esp_err_to_name(er));
        goto fail_close;
    }
    er = esp_ota_set_boot_partition(next);
    if (er != ESP_OK) {
        snprintf(s_msg, sizeof(s_msg), "set_boot: %s", esp_err_to_name(er));
        goto fail_close;
    }

    esp_http_client_close(hc);
    esp_http_client_cleanup(hc);

    s_state = OTA_DONE;
    set_msg("done — rebooting in 2s");
    ESP_LOGI(TAG, "OTA done, rebooting...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return;

fail_close:
    esp_http_client_close(hc);
    esp_http_client_cleanup(hc);
fail:
    s_state = OTA_FAILED;
    ESP_LOGE(TAG, "OTA failed: %s", s_msg);
    vTaskDelete(NULL);
}

void ota_start(void)
{
    if (s_state == OTA_DOWNLOADING || s_state == OTA_VERIFYING) {
        ESP_LOGW(TAG, "already in progress");
        return;
    }
    xTaskCreatePinnedToCore(ota_task, "ota", 8192, NULL, 5, NULL, 0);
}

void ota_get_status(ota_status_t *out)
{
    out->state = s_state;
    out->pct   = s_pct;
    strncpy(out->msg, s_msg, sizeof(out->msg) - 1);
    out->msg[sizeof(out->msg) - 1] = 0;
}
