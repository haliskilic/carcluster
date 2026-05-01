#pragma once
#include "lvgl.h"
#include <stdbool.h>

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

/* Animasyon modları — tek bir periyodik timer tüm icon'ları sync'd günceller. */
typedef enum {
    ICON_MODE_OFF,      /* sadece dim layer görünür */
    ICON_MODE_ON,       /* sürekli yanar */
    ICON_MODE_BLINK,    /* 2 Hz on/off — sinyal/dörtlü */
    ICON_MODE_PULSE,    /* 1 Hz fade %35-100 — kritik uyarı */
} icon_mode_t;

lv_obj_t *icon_create(lv_obj_t *parent, icon_id_t id, int x, int y);

/* Eski API — geriye dönük uyumluluk için ON/OFF setter */
void icon_set_active(lv_obj_t *icon, bool active);

/* Yeni API — mode-based (OFF/ON/BLINK/PULSE) */
void icon_set_mode(lv_obj_t *icon, icon_mode_t mode);

/* Periyodik animasyon timer'ını kurar. ui_build sonunda BİR KEZ çağır.
 * İlk 2000 ms boyunca tüm icon'ları zorla ON yapar (bulb-check / boot sweep);
 * sonra her icon'un kendi mode'unu uygular. */
void icons_anim_init(void);
