#include "demo.h"
#include "state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* VSYNC-driven sürüş demosu — her panel scan'da bir step ilerler.
 * 0→240 km/h: 240 frame × 35 ms = ~8.4 sn. Telltale animasyonlarını görmek için
 * her fazda farklı uyarı kombinasyonu tetikleniyor. */

static void apply_full(int spd, char gear, bool low,
                       bool lblink, bool rblink,
                       bool brake_w, bool engine_w)
{
    state_lock();
    g_state.speed       = spd;
    g_state.rpm         = (spd > 0) ? (800 + spd * 35) : 800;
    g_state.gear        = gear;
    g_state.low_beam    = low;
    g_state.left_blink  = lblink;
    g_state.right_blink = rblink;
    g_state.brake_warn  = brake_w;
    g_state.engine_warn = engine_w;
    if (spd > 0) g_state.total_km++;
    state_unlock();
}

static void wait_frame(void)
{
    /* VSYNC ISR demo_task'a notify atar (faz kilit) */
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
}

static void demo_loop_task(void *arg)
{
    /* Boot fast sweep (~840 ms) — needle 0→240→0 + RPM 800→9200→800.
     * icons.c'deki bulb-check (1200 ms tüm telltale ON) ile çakışır:
     * sweep biterken telltale'ler de söner ve normal mod'a geçilir. */
    for (int v = 0; v <= 240; v += 12) {
        state_lock();
        g_state.speed = v;
        g_state.rpm   = (v > 0) ? (800 + v * 35) : 800;
        g_state.gear  = 'P';
        state_unlock();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    for (int v = 240; v >= 0; v -= 12) {
        state_lock();
        g_state.speed = v;
        g_state.rpm   = (v > 0) ? (800 + v * 35) : 800;
        state_unlock();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    state_lock();
    g_state.speed = 0;
    g_state.rpm   = 800;
    state_unlock();

    while (1) {
        /* İvme: ilk 60 frame (~2 sn) sol sinyal — şerit değişikliği simülasyonu */
        for (int v = 0; v <= 240; v++) {
            apply_full(v, 'D', true, /*L*/v < 60, /*R*/false, /*brk*/false, /*eng*/false);
            wait_frame();
        }
        /* Cruise: 50 frame (~1.75 sn) sakin */
        for (int i = 0; i < 50; i++) {
            apply_full(240, 'D', true, false, false, false, false);
            wait_frame();
        }
        /* Fren: son 60 frame (~2 sn) sağ sinyal — sağa çekiş simülasyonu */
        for (int v = 240; v >= 0; v--) {
            apply_full(v, 'D', true, /*L*/false, /*R*/v < 60, /*brk*/false, /*eng*/false);
            wait_frame();
        }
        /* Park 40 frame (~1.4 sn): park freni PULSE, son 20 frame dörtlü flaşör */
        for (int i = 0; i < 40; i++) {
            bool hazard = i >= 20;
            apply_full(0, 'P', true,
                       /*L*/hazard, /*R*/hazard,
                       /*brk*/true,        /* park freni → PULSE */
                       /*eng*/i >= 30);    /* son 10 frame motor MIL → PULSE */
            wait_frame();
        }
    }
}

void demo_start(void)
{
    /* g_demo_task volatile, cast away for xTaskCreate API */
    xTaskCreatePinnedToCore(demo_loop_task, "demo", 4096, NULL, 3,
                            (TaskHandle_t *)&g_demo_task, 0);
}
