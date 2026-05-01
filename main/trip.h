#pragma once

/* Trip computer — 1 Hz integrator task.
 * g_state.speed/rpm/fuel okur, trip_m, trip_seconds, inst_l100_x10,
 * avg_l100_x10, avg_speed, range_km alanlarını yazar.
 *
 * Demo amaçlı sentetik tüketim modeli kullanılıyor:
 *   inst_l100 = 4.0 + (speed/240)*4 + ((rpm-800)/8400)*4   [4..12 L/100km]
 * Gerçek araç entegrasyonunda (CAN/OBD-II) bu modelin yerine FUEL_RATE PID
 * (LiveData PID 5E) gelir. */

void trip_start(void);   /* Arka plan task'ı başlatır (1 Hz tick) */
void trip_reset(void);   /* Trip mesafesi/süresi/yakıt sıfırlanır (touch UI'dan) */
