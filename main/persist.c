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
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, KEY_TOTAL_KM, km);
    nvs_commit(h);
    nvs_close(h);
}

static void autosave_task(void *arg)
{
    (void)arg;
    uint32_t last_saved = (uint32_t)-1;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(AUTOSAVE_MS));
        state_lock();
        uint32_t cur = (uint32_t)g_state.total_km;
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
