#include "cpu_meter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_freertos_hooks.h"
#include "esp_timer.h"
#include "esp_attr.h"

static volatile uint64_t s_idle_count[2];
static uint64_t          s_idle_max[2];
static int               s_cpu_pct[2];

/* Idle hook — tight loop, IRAM_ATTR ile flash latency yok */
static bool IRAM_ATTR idle_hook_0(void) { s_idle_count[0]++; return true; }
static bool IRAM_ATTR idle_hook_1(void) { s_idle_count[1]++; return true; }

static void sampler_task(void *arg)
{
    (void)arg;
    uint64_t prev_count[2] = {0, 0};
    uint64_t prev_us = esp_timer_get_time();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));
        uint64_t now_us = esp_timer_get_time();
        uint64_t dt_us  = now_us - prev_us;
        if (dt_us == 0) continue;

        for (int i = 0; i < 2; i++) {
            uint64_t cur   = s_idle_count[i];
            uint64_t delta = cur - prev_count[i];
            prev_count[i]  = cur;

            /* Idle iteration / sn — saatten bağımsız oran */
            uint64_t per_sec = delta * 1000000ULL / dt_us;
            if (per_sec > s_idle_max[i]) s_idle_max[i] = per_sec;

            int pct = 0;
            if (s_idle_max[i] > 0) {
                pct = 100 - (int)(per_sec * 100ULL / s_idle_max[i]);
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
    esp_register_freertos_idle_hook_for_cpu(idle_hook_0, 0);
    esp_register_freertos_idle_hook_for_cpu(idle_hook_1, 1);
    xTaskCreatePinnedToCore(sampler_task, "cpumeter", 2048, NULL, 1, NULL, 0);
}

int cpu_meter_get_pct(int core)
{
    if (core < 0 || core > 1) return 0;
    return s_cpu_pct[core];
}
