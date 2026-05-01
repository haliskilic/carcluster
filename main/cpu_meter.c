#include "cpu_meter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_freertos_hooks.h"
#include "esp_timer.h"
#include "esp_attr.h"

/* uint32_t (64 değil): ESP32-S3 32-bit, uint64 read iki ayrı load
 * ISR araya girerse yarı eski/yarı yeni okur → garbage delta. uint32 read
 * atomic. Counter 500ms örnekleme aralığında ~max 1M iter → wrap riski yok
 * (uint32 max = 4.29B). Wrap olsa bile unsigned subtract delta'yı doğru verir. */
static volatile uint32_t s_idle_count[2];
static uint32_t          s_idle_max_per_sec[2];
static int               s_cpu_pct[2];
static int64_t           s_init_us = 0;
#define CALIBRATION_MS   2000   /* İlk 2sn: max yakınsasın, % 0'da kalır */

/* Idle hook — tight loop, IRAM_ATTR ile flash latency yok */
static bool IRAM_ATTR idle_hook_0(void) { s_idle_count[0]++; return true; }
static bool IRAM_ATTR idle_hook_1(void) { s_idle_count[1]++; return true; }

static void sampler_task(void *arg)
{
    (void)arg;
    uint32_t prev_count[2] = {0, 0};
    int64_t  prev_us = esp_timer_get_time();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));
        int64_t  now_us = esp_timer_get_time();
        int64_t  dt_us  = now_us - prev_us;
        if (dt_us <= 0) continue;

        bool calibrating = (now_us - s_init_us) < (CALIBRATION_MS * 1000LL);

        for (int i = 0; i < 2; i++) {
            uint32_t cur   = s_idle_count[i];           /* atomic 32-bit read */
            uint32_t delta = cur - prev_count[i];        /* unsigned wrap-safe */
            prev_count[i]  = cur;

            /* Idle iteration / sn — saatten bağımsız oran (uint64 hesap, fit garanti) */
            uint32_t per_sec = (uint32_t)((uint64_t)delta * 1000000ULL / (uint64_t)dt_us);
            if (per_sec > s_idle_max_per_sec[i]) s_idle_max_per_sec[i] = per_sec;

            /* Calibration window'da % gösterme — max stabilize olsun */
            if (calibrating) {
                s_cpu_pct[i] = 0;
                continue;
            }

            int pct = 0;
            if (s_idle_max_per_sec[i] > 0) {
                pct = 100 - (int)((uint64_t)per_sec * 100ULL / s_idle_max_per_sec[i]);
            }
            if (pct < 0)   pct = 0;
            if (pct > 100) pct = 100;
            s_cpu_pct[i] = pct;
        }
        prev_us = now_us;
    }
}

void cpu_meter_init(void)
{
    s_init_us = esp_timer_get_time();
    esp_register_freertos_idle_hook_for_cpu(idle_hook_0, 0);
    esp_register_freertos_idle_hook_for_cpu(idle_hook_1, 1);
    xTaskCreatePinnedToCore(sampler_task, "cpumeter", 2048, NULL, 1, NULL, 0);
}

int cpu_meter_get_pct(int core)
{
    if (core < 0 || core > 1) return 0;
    return s_cpu_pct[core];
}
