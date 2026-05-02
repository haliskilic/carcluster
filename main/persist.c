#include "persist.h"
#include "state.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "persist";

#define NS            "carcluster"
#define KEY_TOTAL_KM  "total_km"
#define AUTOSAVE_MS   30000   /* 30 sn — NVS wear ile UX güncelliği dengesi */

/* Reset reason counter key — esp_reset_reason_t enum value (0-9) */
static void reset_key(int reason, char *buf, size_t buflen) {
    snprintf(buf, buflen, "rst_%d", reason);
}

void persist_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS reset (no free pages or new version)");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }
}

uint32_t persist_load_total_km(uint32_t fallback)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return fallback;
    uint32_t v = fallback;
    nvs_get_u32(h, KEY_TOTAL_KM, &v);
    nvs_close(h);
    ESP_LOGI(TAG, "loaded total_km=%lu", (unsigned long)v);
    return v;
}

void persist_save_total_km(uint32_t km)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_set_u32(h, KEY_TOTAL_KM, km);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_set_u32: %s", esp_err_to_name(err));
        nvs_close(h);
        return;
    }
    err = nvs_commit(h);
    if (err != ESP_OK) ESP_LOGW(TAG, "nvs_commit: %s", esp_err_to_name(err));
    nvs_close(h);
}

uint32_t persist_inc_reset_counter(int reason)
{
    char key[16];
    reset_key(reason, key, sizeof(key));
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return 0;
    uint32_t cnt = 0;
    nvs_get_u32(h, key, &cnt);
    cnt++;
    nvs_set_u32(h, key, cnt);
    nvs_commit(h);
    nvs_close(h);
    return cnt;
}

uint32_t persist_get_reset_counter(int reason)
{
    char key[16];
    reset_key(reason, key, sizeof(key));
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return 0;
    uint32_t cnt = 0;
    nvs_get_u32(h, key, &cnt);
    nvs_close(h);
    return cnt;
}

static void autosave_task(void *arg)
{
    (void)arg;
    uint32_t last_saved = (uint32_t)-1;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(AUTOSAVE_MS));
        state_lock();
        uint32_t cur = g_state.total_km;
        state_unlock();
        if (cur != last_saved) {
            persist_save_total_km(cur);
            last_saved = cur;
            ESP_LOGI(TAG, "autosave total_km=%lu", (unsigned long)cur);
        }
    }
}

void persist_start_autosave(void)
{
    xTaskCreatePinnedToCore(autosave_task, "persist", 3072, NULL, 1, NULL, 0);
}
