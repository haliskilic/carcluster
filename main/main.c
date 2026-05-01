#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "board.h"
#include "lvgl_port.h"
#include "state.h"
#include "ui.h"
#include "demo.h"
#include "persist.h"
#include "trip.h"
#include "cpu_meter.h"

static const char *TAG = "carcluster";

cluster_state_t g_state = { .gear = 'P', .fuel = 75, .temp = 90 };  /* total_km NVS'ten */
SemaphoreHandle_t g_state_mutex;
volatile TaskHandle_t g_demo_task = NULL;
volatile TaskHandle_t g_ui_task   = NULL;

void state_init(void)
{
    g_state_mutex = xSemaphoreCreateMutex();
    if (!g_state_mutex) {
        ESP_LOGE(TAG, "state mutex create failed");
        abort();
    }
}

static void ui_refresh_task(void *arg)
{
    /* VSYNC notify-driven — her panel scan başlangıcında uyanır,
     * 1 frame içinde state'i okuyup widget'ları günceller. Faz kilidi. */
    while (1) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        lvgl_port_lock();
        ui_refresh();
        lvgl_port_unlock();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== carcluster boot (VSYNC-driven, phase-locked) ===");
    state_init();

    /* NVS persistence — total_km'i yükle, autosave task'ını başlat */
    persist_init();
    g_state.total_km = persist_load_total_km(12345);

    board_init();

    /* Önce task'ları yarat — VSYNC ISR registered olmadan önce
     * g_ui_task ve g_demo_task non-NULL olsun (race window kapalı). */
    TaskHandle_t ui_h = NULL;
    xTaskCreatePinnedToCore(ui_refresh_task, "ui_refresh",
                            4096, NULL, 3, &ui_h, 0);
    g_ui_task = ui_h;
    demo_start();

    /* Şimdi LVGL + VSYNC ISR — handle'lar hazır */
    lvgl_port_init();

    lvgl_port_lock();
    ui_build();
    ui_refresh();
    ui_set_ip("DEMO");
    lvgl_port_unlock();

    /* Persistence autosave 30 sn'de bir total_km'i NVS'e yazar */
    persist_start_autosave();

    /* Trip computer 1 Hz integrator (mesafe + süre + tüketim + range) */
    trip_start();

    /* CPU meter — idle hook'ları register, sampler task spawn (per-core %) */
    cpu_meter_init();

    ESP_LOGI(TAG, "Ready.");
}
