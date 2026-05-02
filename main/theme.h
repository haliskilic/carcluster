#pragma once
#include "lvgl.h"
#include <stdint.h>

/* Tema sistemi — 4 preset color palette. Reboot-required apply.
 *
 * Renkler ui.c'de "extern lv_color_t C_*" olarak tanımlı global değişkenler.
 * theme_apply(id) seçilen palet'i bu globallere yazar; ui_build'den ÖNCE
 * çağrılmalı (board_init paralelinde). Sonradan değiştirmek için kullanıcı
 * settings'ten seçer → NVS'e yazılır → reboot'ta yeni palet aktif. */

#define THEME_COUNT 4

typedef enum {
    THEME_BLUE   = 0,   /* default — cool navy + cyan + amber */
    THEME_ORANGE = 1,   /* black + warm orange dominant */
    THEME_YELLOW = 2,   /* near-black + yellow accent */
    THEME_RED    = 3,   /* mono dark + red */
} theme_id_t;

const char *theme_name(theme_id_t id);
void theme_apply(theme_id_t id);
theme_id_t theme_active(void);
