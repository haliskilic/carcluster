#pragma once
#include <stdint.h>
#include <stdbool.h>

/* NVS-backed persistence — total_km gibi reboot'ta korunması gereken alanlar.
 * Autosave task'ı 30 sn'de bir g_state'i okur; değiştiyse NVS'e yazar.
 * NVS wear-leveling driver tarafından otomatik. */

void     persist_init(void);                   /* nvs_flash_init + erase if needed */
uint32_t persist_load_total_km(uint32_t fallback);
void     persist_save_total_km(uint32_t km);
void     persist_start_autosave(void);         /* arka plan task'ı başlat */

/* Reset reason counters — boot'ta increment (panic/wdt/brownout fault sayımı) */
uint32_t persist_inc_reset_counter(int reason);  /* esp_reset_reason_t değeri */
uint32_t persist_get_reset_counter(int reason);

/* Trip data persistence (E1) — reboot'ta korunur. NVS blob.
 * trip_m: metre, trip_seconds: saniye, trip_fuel_ml: tüketilen yakıt mL. */
typedef struct {
    uint32_t trip_m;
    uint32_t trip_seconds;
    uint32_t trip_fuel_ml;
} trip_persist_t;

bool persist_load_trip(trip_persist_t *out);   /* true = NVS'te varsa yüklendi */
void persist_save_trip(const trip_persist_t *t);
void persist_clear_trip(void);                  /* trip_reset NVS'i de sıfırlasın */

/* Theme — preset palette index (0=Audi default). NVS u8. */
uint8_t persist_load_theme(void);
void    persist_save_theme(uint8_t id);

/* Trip B (B3) — bağımsız ikinci trip counter, blob */
typedef struct {
    uint32_t trip_b_m;
    uint32_t trip_b_seconds;
    uint32_t trip_b_fuel_ml;
} trip_b_persist_t;

bool persist_load_trip_b(trip_b_persist_t *out);
void persist_save_trip_b(const trip_b_persist_t *t);
void persist_clear_trip_b(void);

/* Lifetime stats (B8) — max speed, longest trip, total fuel/time, blob */
typedef struct {
    int      max_speed_kmh;
    uint32_t longest_trip_m;
    uint32_t total_fuel_ml;
    uint32_t total_seconds;
} stats_persist_t;

bool persist_load_stats(stats_persist_t *out);
void persist_save_stats(const stats_persist_t *s);
