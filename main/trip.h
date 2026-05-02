#pragma once

/* Trip computer — 1 Hz integrator task.
 * g_state.speed/rpm/fuel okur, trip_m, trip_seconds, inst_l100_x10,
 * avg_l100_x10, avg_speed, range_km alanlarını yazar.
 *
 * Demo amaçlı sentetik tüketim modeli kullanılıyor:
 *   inst_l100 = 4.0 + (speed/240)*4 + ((rpm-800)/8400)*4   [4..12 L/100km]
 * Gerçek araç entegrasyonunda (CAN/OBD-II) bu modelin yerine FUEL_RATE PID
 * (LiveData PID 5E) gelir. */

#include <stdint.h>

void trip_start(void);     /* Arka plan task'ı başlatır (1 Hz tick) */
void trip_reset_a(void);   /* Trip A reset (eski trip_reset) — main panel */
void trip_reset_b(void);   /* Trip B reset (settings) — bağımsız sayaç */

/* Eski API alias'ı — backward compat (mevcut çağrılar Trip A'ya gider) */
#define trip_reset trip_reset_a

/* Trip B + lifetime stats — diag/settings içinden okunur */
typedef struct {
    uint32_t trip_b_m;
    uint32_t trip_b_seconds;
    int      trip_b_avg_speed;
    int      trip_b_avg_l100_x10;
} trip_b_view_t;

typedef struct {
    int      max_speed_kmh;     /* en yüksek anlık hız */
    uint32_t longest_trip_m;    /* tek trip'te en uzun mesafe */
    uint32_t total_fuel_ml;     /* lifetime toplam yakıt (mL) */
    uint32_t total_seconds;     /* lifetime toplam çalışma süresi (s) */
} trip_stats_t;

void trip_get_b(trip_b_view_t *out);
void trip_get_stats(trip_stats_t *out);
