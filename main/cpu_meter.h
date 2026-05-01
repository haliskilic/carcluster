#pragma once
#include <stdint.h>

/* Per-core CPU utilization meter.
 *
 * FreeRTOS idle hook'ları her çekirdekte idle döngüsünde tetiklenir.
 * Sayaçlar 500ms'de bir örneklenir; max görülen idle/sn baseline (%0 CPU)
 * olarak alınır → CPU% = 100 - (idle_per_sec / max) * 100.
 * Calibration auto-adaptive: ilk birkaç saniye max yakınsar.
 *
 * cpu_meter_init() bir kez çağrılır (idle hook'lar register, sampler task spawn).
 * cpu_meter_get_pct(core) anlık % değeri döner (0..100). */

void cpu_meter_init(void);
int  cpu_meter_get_pct(int core);   /* core: 0 veya 1 */
