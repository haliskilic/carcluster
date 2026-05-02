#pragma once
#include <stdint.h>

/* Unit toggle — METRIC veya IMPERIAL. NVS u8'de saklanır.
 *
 * Internal state HEP metric tutulur (km/h, °C, L/100km, km, m).
 * Sadece UI digital readout'lar bu modüldeki formatter'lar üzerinden çevrilir.
 * Gauge tick'leri metric kalır (0..240 km/h, 0..130°C) — tipik premium araç
 * davranışı (ekrandaki büyük dijital değer "gerçek" sayı, gauge görsel). */

typedef enum { UNIT_METRIC = 0, UNIT_IMPERIAL = 1 } unit_t;

void   unit_init(void);            /* NVS'ten yükle, runtime cache set */
unit_t unit_get(void);
void   unit_set(unit_t u);         /* NVS'e yaz, runtime cache güncelle */

/* Kısa unit string'leri — display'de "75 mph" gibi suffix için */
const char *unit_speed_label(void);   /* "km/h" / "mph" */
const char *unit_dist_label(void);    /* "km"   / "mi"  */
const char *unit_temp_label(void);    /* "°C"  / "°F"   */
const char *unit_consum_label(void);  /* "L/100" / "MPG" */

/* Conversion — input HEP metric, output user-selected unit */
int      unit_conv_speed(int kmh);
uint32_t unit_conv_dist_km(uint32_t km);
int      unit_conv_temp_c(int c);
/* L/100km ×10 → MPG ×10 (imperial) veya identity (metric) */
int      unit_conv_l100_x10(int l100_x10);
