#include "idle.h"
#include "board.h"
#include "state.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include <stdbool.h>

static const char *TAG = "idle";

#define IDLE_THRESHOLD_MS  30000   /* 30 sn — demo'da hızlı doğrulama, prod 5dk olur */

static volatile int64_t s_last_activity_us = 0;
static volatile bool    s_dimmed           = false;

void idle_mark_activity(void)
{
    s_last_activity_us = esp_timer_get_time();
    if (s_dimmed) {
        /* Wake — backlight on (CH422G EXIO2 set) */
        board_ch422g_write(BIT_BL_EN, 0);
        s_dimmed = false;
        ESP_LOGI(TAG, "wake (touch activity)");
    }
}

static void idle_task(void *arg)
{
    (void)arg;
    s_last_activity_us = esp_timer_get_time();

    /* TWDT subscribe — 1 Hz pet, bol margin */
    esp_task_wdt_add(NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_task_wdt_reset();

        /* Demo çalışıyorsa activity her zaman fresh */
        if (!g_demo_paused) {
            s_last_activity_us = esp_timer_get_time();
            continue;
        }

        /* Pause durumunda elapsed kontrolü */
        int64_t elapsed_ms = (esp_timer_get_time() - s_last_activity_us) / 1000;
        if (elapsed_ms > IDLE_THRESHOLD_MS && !s_dimmed) {
            board_ch422g_write(0, BIT_BL_EN);
            s_dimmed = true;
            ESP_LOGI(TAG, "screen-off (idle %lld ms)", elapsed_ms);
        }
    }
}

void idle_init(void)
{
    xTaskCreatePinnedToCore(idle_task, "idle", 3072, NULL, 1, NULL, 0);
}
