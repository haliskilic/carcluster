#pragma once
#include <stdint.h>

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
