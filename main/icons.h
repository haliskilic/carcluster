#pragma once
#include "lvgl.h"

/* ISO 2575 standardına yakın profesyonel telltale ikonları
 * Hepsi 36x36 boy, lv_obj primitiflerle çizilir, flat tasarım. */

typedef enum {
    ICON_TURN_L,        /* yeşil sol ok */
    ICON_TURN_R,        /* yeşil sağ ok */
    ICON_HIGH_BEAM,     /* mavi - far + ışın çizgileri */
    ICON_LOW_BEAM,      /* yeşil - far + aşağı ışın */
    ICON_BRAKE,         /* kırmızı - "(!)" daire */
    ICON_ABS,           /* amber - "ABS" daire */
    ICON_AIRBAG,        /* kırmızı - kişi + airbag */
    ICON_SEATBELT,      /* kırmızı - kişi + emniyet kemeri */
    ICON_ENGINE,        /* amber - motor silüeti (MIL) */
    ICON_BATTERY,       /* kırmızı - batarya */
    ICON_OIL,           /* kırmızı - yağ kabı */
    ICON_COOLANT,       /* kırmızı - termometre */
    ICON_FUEL_LOW,      /* amber - yakıt pompası */
} icon_id_t;

/* Container + ikon oluşturur, başta gizli (LVGL HIDDEN flag set) */
lv_obj_t *icon_create(lv_obj_t *parent, icon_id_t id, int x, int y);
void icon_set_active(lv_obj_t *icon, bool active);
