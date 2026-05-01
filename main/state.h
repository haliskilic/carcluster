#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    /* Analog */
    int speed;          /* km/h, 0..999 (4 hane) */
    int rpm;            /* 0..9999 (4 hane) */
    int fuel;           /* %, 0..100 */
    int temp;           /* °C, -40..150 */
    int total_km;
    char gear;          /* P R N D 1..6 */

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
} cluster_state_t;

extern cluster_state_t g_state;
extern SemaphoreHandle_t g_state_mutex;

/* VSYNC tetikleyici — her panel scan başlangıcında uyandırılan task'lar.
 * lvgl_port.c (VSYNC ISR) tarafından notify edilir. */
extern TaskHandle_t g_demo_task;
extern TaskHandle_t g_ui_task;

void state_init(void);

static inline void state_lock(void)   { xSemaphoreTake(g_state_mutex, portMAX_DELAY); }
static inline void state_unlock(void) { xSemaphoreGive(g_state_mutex); }
