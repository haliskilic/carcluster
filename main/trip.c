#include "trip.h"
#include "state.h"
#include "persist.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include <stdint.h>

static const char *TAG = "trip";

#define TANK_LITERS  50.0f   /* sentetik depo kapasitesi */

/* Float internal accumulators — int state'e yazılırken ×10 fixed-point yapılır */
static float trip_km     = 0.0f;     /* Trip A — main panel */
static float trip_hours  = 0.0f;
static float trip_fuel_l = 0.0f;

/* Trip B (B3) — bağımsız ikinci sayaç. A ile birlikte tick eder, ayrı reset'lenir.
 * Diag sekmesinde gösterilir. */
static float trip_b_km     = 0.0f;
static float trip_b_hours  = 0.0f;
static float trip_b_fuel_l = 0.0f;

/* Lifetime stats (B8) — max speed, longest trip, total fuel/time. NVS persist. */
static int      stats_max_speed   = 0;
static uint32_t stats_longest_m   = 0;   /* Trip A'nın mesafesinden track edilir */
static uint32_t stats_total_fuel  = 0;   /* mL */
static uint32_t stats_total_secs  = 0;
static uint32_t stats_dirty_count = 0;   /* save tetiklemek için yumuşak sayaç */

#undef trip_reset
void trip_reset_a(void)
{
    trip_km = trip_hours = trip_fuel_l = 0.0f;
    state_lock();
    g_state.trip_m        = 0;
    g_state.trip_seconds  = 0;
    g_state.avg_speed     = 0;
    g_state.avg_l100_x10  = 0;
    state_unlock();
    persist_clear_trip();
}

void trip_reset_b(void)
{
    trip_b_km = trip_b_hours = trip_b_fuel_l = 0.0f;
    persist_clear_trip_b();
}

void trip_get_b(trip_b_view_t *out)
{
    out->trip_b_m       = (uint32_t)(trip_b_km * 1000.0f);
    out->trip_b_seconds = (uint32_t)(trip_b_hours * 3600.0f);
    out->trip_b_avg_speed = (trip_b_hours > 0.0001f)
        ? (int)(trip_b_km / trip_b_hours) : 0;
    float avg_l100 = (trip_b_km > 0.01f)
        ? (trip_b_fuel_l * 100.0f / trip_b_km) : 0.0f;
    out->trip_b_avg_l100_x10 = (int)(avg_l100 * 10.0f);
}

void trip_get_stats(trip_stats_t *out)
{
    out->max_speed_kmh  = stats_max_speed;
    out->longest_trip_m = stats_longest_m;
    out->total_fuel_ml  = stats_total_fuel;
    out->total_seconds  = stats_total_secs;
}

/* Sentetik tüketim modeli: idle=1, cruise=4, hard accel ~12 L/100km */
static float compute_inst_l100(int speed, int rpm)
{
    if (speed == 0) return 1.0f;                  /* idle eşdeğeri */
    float base       = 4.0f;
    float speed_term = (speed / 240.0f) * 4.0f;   /* 0..4 */
    float rpm_term   = (rpm - 800.0f) / 8400.0f;  /* 0..1 (kabaca) */
    if (rpm_term < 0.0f) rpm_term = 0.0f;
    float r = base + speed_term + rpm_term * 4.0f;
    return r < 1.0f ? 1.0f : r;
}

static void trip_task(void *arg)
{
    (void)arg;
    /* TWDT subscribe — 1 Hz pet, 10s timeout, bol margin */
    esp_task_wdt_add(NULL);

    /* Boot: NVS'ten yükle (E1) — trip reboot'a dayanıklı */
    trip_persist_t loaded = {0};
    if (persist_load_trip(&loaded)) {
        trip_km     = loaded.trip_m / 1000.0f;
        trip_hours  = loaded.trip_seconds / 3600.0f;
        trip_fuel_l = loaded.trip_fuel_ml / 1000.0f;
        ESP_LOGI(TAG, "trip A restored: %.2f km, %lu sec, %.2f L",
                 trip_km, (unsigned long)loaded.trip_seconds, trip_fuel_l);
    }
    trip_b_persist_t loaded_b = {0};
    if (persist_load_trip_b(&loaded_b)) {
        trip_b_km     = loaded_b.trip_b_m / 1000.0f;
        trip_b_hours  = loaded_b.trip_b_seconds / 3600.0f;
        trip_b_fuel_l = loaded_b.trip_b_fuel_ml / 1000.0f;
    }
    stats_persist_t loaded_s = {0};
    if (persist_load_stats(&loaded_s)) {
        stats_max_speed  = loaded_s.max_speed_kmh;
        stats_longest_m  = loaded_s.longest_trip_m;
        stats_total_fuel = loaded_s.total_fuel_ml;
        stats_total_secs = loaded_s.total_seconds;
        ESP_LOGI(TAG, "stats restored: max=%d longest=%lu total_fuel=%lu mL",
                 stats_max_speed,
                 (unsigned long)stats_longest_m,
                 (unsigned long)stats_total_fuel);
    }

    /* Save sayacı: her 30 trip-saniyesinde NVS'e yaz (pause'da save yok) */
    int last_saved_sec = (int)(trip_hours * 3600.0f);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));   /* 1 Hz */
        esp_task_wdt_reset();

        /* Pause durumunda integrator durdurulur — TIME/RANGE/avg artmasın.
         * Bu demo pause, gerçek CAN entegrasyonunda key-off ile değişebilir. */
        if (g_demo_paused) continue;

        state_lock();
        int spd  = g_state.speed;
        int rpm  = g_state.rpm;
        int fuel = g_state.fuel;
        state_unlock();

        const float dt_h    = 1.0f / 3600.0f;
        float d_km          = spd * dt_h;
        float inst_l100     = compute_inst_l100(spd, rpm);
        float fuel_l        = inst_l100 * d_km / 100.0f;

        /* Trip A + Trip B paralel artar (B3) */
        trip_km    += d_km;
        trip_hours += dt_h;
        trip_fuel_l += fuel_l;
        trip_b_km    += d_km;
        trip_b_hours += dt_h;
        trip_b_fuel_l += fuel_l;

        /* Lifetime stats (B8) — max speed her sample, total fuel/time her tick,
         * longest trip Trip A'nın mesafesinden track edilir. */
        if (spd > stats_max_speed) stats_max_speed = spd;
        stats_total_fuel += (uint32_t)(fuel_l * 1000.0f);
        stats_total_secs += 1;
        uint32_t cur_trip_m = (uint32_t)(trip_km * 1000.0f);
        if (cur_trip_m > stats_longest_m) stats_longest_m = cur_trip_m;
        stats_dirty_count++;

        float avg_l100  = (trip_km > 0.01f) ? (trip_fuel_l * 100.0f / trip_km) : 0.0f;
        float avg_speed = (trip_hours > 0.0001f) ? (trip_km / trip_hours) : 0.0f;
        float remain_l  = (fuel / 100.0f) * TANK_LITERS;
        /* avg yoksa inst'i proxy olarak kullan, sıfıra bölme yok */
        float ref_l100  = avg_l100 > 0.5f ? avg_l100 : inst_l100;
        float range     = (ref_l100 > 0.5f) ? (remain_l * 100.0f / ref_l100) : 0.0f;

        /* uint32 max: trip_m=4.29B → 4.29M km; trip_seconds=4.29B sn → 136 yıl */
        uint32_t trip_m_u  = (trip_km > 4290000.0f) ? UINT32_MAX
                                                    : (uint32_t)(trip_km * 1000.0f);
        uint32_t trip_s_u  = (trip_hours > 1190000.0f) ? UINT32_MAX
                                                       : (uint32_t)(trip_hours * 3600.0f);
        state_lock();
        g_state.trip_m         = trip_m_u;
        g_state.trip_seconds   = trip_s_u;
        g_state.avg_speed      = (int)avg_speed;
        g_state.avg_l100_x10   = (int)(avg_l100 * 10.0f);
        g_state.inst_l100_x10  = (int)(inst_l100 * 10.0f);
        g_state.range_km       = (int)range;
        state_unlock();

        /* Periyodik NVS save (30 trip-saniye) — A + B + stats hep birlikte */
        int cur_sec = (int)trip_s_u;
        if (cur_sec - last_saved_sec >= 30) {
            trip_persist_t snap = {
                .trip_m       = trip_m_u,
                .trip_seconds = trip_s_u,
                .trip_fuel_ml = (uint32_t)(trip_fuel_l * 1000.0f),
            };
            persist_save_trip(&snap);

            trip_b_persist_t snap_b = {
                .trip_b_m       = (uint32_t)(trip_b_km * 1000.0f),
                .trip_b_seconds = (uint32_t)(trip_b_hours * 3600.0f),
                .trip_b_fuel_ml = (uint32_t)(trip_b_fuel_l * 1000.0f),
            };
            persist_save_trip_b(&snap_b);

            stats_persist_t snap_s = {
                .max_speed_kmh   = stats_max_speed,
                .longest_trip_m  = stats_longest_m,
                .total_fuel_ml   = stats_total_fuel,
                .total_seconds   = stats_total_secs,
            };
            persist_save_stats(&snap_s);

            last_saved_sec = cur_sec;
            stats_dirty_count = 0;
        }
    }
}

void trip_start(void)
{
    xTaskCreatePinnedToCore(trip_task, "trip", 3072, NULL, 1, NULL, 0);
}
