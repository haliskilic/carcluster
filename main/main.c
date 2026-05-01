#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "board.h"
#include "lvgl_port.h"
#include "state.h"
#include "ui.h"
#include "demo.h"

static const char *TAG = "carcluster";

cluster_state_t g_state = { .gear = 'P', .total_km = 12345, .fuel = 85, .temp = 25 };
SemaphoreHandle_t g_state_mutex;
TaskHandle_t g_demo_task = NULL;
TaskHandle_t g_ui_task   = NULL;

void state_init(void)
{
    g_state_mutex = xSemaphoreCreateMutex();
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

    board_init();
    lvgl_port_init();

    lvgl_port_lock();
    ui_build();
    ui_refresh();
    ui_set_ip("DEMO");
    lvgl_port_unlock();

    xTaskCreatePinnedToCore(ui_refresh_task, "ui_refresh",
                            4096, NULL, 3, &g_ui_task, 0);
    demo_start();   /* g_demo_task'ı set eder */

    ESP_LOGI(TAG, "Ready. VSYNC drives demo + ui_refresh.");
}
