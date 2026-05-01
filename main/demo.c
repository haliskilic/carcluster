#include "demo.h"
#include "state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* VSYNC-driven sürüş demosu — her panel scan'da bir step ilerler.
 * 0→240 km/h: 240 frame × 35 ms = ~8.4 sn. */

static void apply(int spd, char gear, bool low_beam)
{
    state_lock();
    g_state.speed   = spd;
    g_state.rpm     = (spd > 0) ? (800 + spd * 35) : 800;
    g_state.gear    = gear;
    g_state.low_beam = low_beam;
    g_state.fuel    = 75;
    g_state.temp    = 90;
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
    while (1) {
        for (int v = 0; v <= 240; v++) { apply(v, 'D', true); wait_frame(); }   /* 8.4 sn ivme */
        for (int i = 0; i < 50; i++)   { apply(240, 'D', true); wait_frame(); } /* 1.75 sn cruise */
        for (int v = 240; v >= 0; v--) { apply(v, 'D', true); wait_frame(); }   /* 8.4 sn fren */
        for (int i = 0; i < 40; i++)   { apply(0, 'P', true); wait_frame(); }   /* 1.4 sn park */
    }
}

void demo_start(void)
{
    xTaskCreatePinnedToCore(demo_loop_task, "demo", 4096, NULL, 3, &g_demo_task, 0);
}
