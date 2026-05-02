#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    /* Analog */
    int      speed;     /* km/h, 0..999 (4 hane) */
    int      rpm;       /* 0..9999 (4 hane) */
    int      fuel;      /* %, 0..100 */
    int      temp;      /* °C, -40..150 */
    uint32_t total_km;  /* unsigned: int max=2.1B; demo @30fps int aşar 100 günde */
    char     gear;      /* P R N D 1..6 */

    /* Status (yeşil/mavi) */
    bool left_blink;
    bool right_blink;
    bool high_beam;
    bool low_beam;
    bool position_lamp;

    /* Critical (kırmızı) — ISO 26262 ASIL B/C/D */
    bool brake_warn;        /* fren sistemi arızası */
    bool airbag_warn;       /* SRS */
    bool seatbelt_warn;     /* emniyet kemeri */
    bool battery_warn;      /* şarj sistemi */
    bool oil_warn;          /* yağ basıncı */
    bool coolant_warn;      /* soğutma suyu yüksek */

    /* Caution (amber) */
    bool engine_warn;       /* MIL / check engine */
    bool abs_warn;          /* ABS */
    bool fuel_low_warn;     /* yakıt seviyesi düşük (otomatik veya manuel) */

    /* Trip computer (trip.c tarafından 1 Hz güncellenir).
     * Fixed-point ×10: avg_l100_x10=64 → 6.4 L/100km */
    uint32_t trip_m;          /* trip mesafesi, metre — int taşardı 2150km'de */
    uint32_t trip_seconds;    /* trip süresi, saniye */
    int      avg_speed;       /* trip ortalama hızı, km/h */
    int      inst_l100_x10;   /* anlık tüketim ×10 (sentetik, demo) */
    int      avg_l100_x10;    /* ortalama tüketim ×10 */
    int      range_km;        /* tahmini menzil, km */
} cluster_state_t;

extern cluster_state_t g_state;
extern SemaphoreHandle_t g_state_mutex;

/* Demo pause flag — touch UI'dan ayarlanır, demo task her loop'ta kontrol eder.
 * true: demo state üretmeyi durdurur, mevcut değerler ekrana kalır. */
extern volatile bool g_demo_paused;

/* VSYNC tetikleyici — her panel scan başlangıcında uyandırılan task'lar.
 * lvgl_port.c (VSYNC ISR) tarafından notify edilir. */
/* volatile: VSYNC ISR tarafından okunur, task'lar tarafından set edilir */
extern volatile TaskHandle_t g_demo_task;
extern volatile TaskHandle_t g_ui_task;

void state_init(void);

static inline void state_lock(void)   { xSemaphoreTake(g_state_mutex, portMAX_DELAY); }
static inline void state_unlock(void) { xSemaphoreGive(g_state_mutex); }
