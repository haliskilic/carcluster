#include "icons.h"
#include <string.h>
#include <stdlib.h>

/* Renkler */
#define C_RED       lv_color_hex(0xff2030)
#define C_AMBER     lv_color_hex(0xffaa00)
#define C_GREEN     lv_color_hex(0x30d030)
#define C_BLUE      lv_color_hex(0x4488ff)
#define C_OFF       lv_color_hex(0x1a2030)
#define C_DIM       lv_color_hex(0x223040)

#define ICON_SZ 40

static lv_color_t icon_color(icon_id_t id)
{
    switch (id) {
        case ICON_TURN_L:
        case ICON_TURN_R:
        case ICON_LOW_BEAM:    return C_GREEN;
        case ICON_HIGH_BEAM:   return C_BLUE;
        case ICON_ABS:
        case ICON_ENGINE:
        case ICON_FUEL_LOW:    return C_AMBER;
        default:               return C_RED;
    }
}

static lv_obj_t *fill_rect(lv_obj_t *p, int x, int y, int w, int h, lv_color_t c, int radius)
{
    lv_obj_t *r = lv_obj_create(p);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, w, h);
    lv_obj_set_pos(r, x, y);
    lv_obj_set_style_bg_color(r, c, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r, radius, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return r;
}

static lv_obj_t *txt_in(lv_obj_t *p, const char *t, lv_color_t c, const lv_font_t *f, int x, int y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

static void draw_arrow(lv_obj_t *c, lv_color_t col, bool right)
{
    fill_rect(c, right? 6:18, 16, 16, 6, col, 1);
    int ax = right ? 22 : 12;
    fill_rect(c, ax,    13, 4, 12, col, 1);
    fill_rect(c, ax + (right?-4:4), 10, 4, 18, col, 1);
}

static void draw_high_beam(lv_obj_t *c, lv_color_t col)
{
    fill_rect(c, 6, 10, 20, 20, col, 10);
    for (int i = 0; i < 4; i++) fill_rect(c, 28, 12 + i*4, 6, 2, col, 1);
}

static void draw_low_beam(lv_obj_t *c, lv_color_t col)
{
    fill_rect(c, 6, 10, 20, 20, col, 10);
    for (int i = 0; i < 4; i++) fill_rect(c, 28, 18 + i*3, 6, 2, col, 1);
}

static void draw_brake(lv_obj_t *c, lv_color_t col)
{
    fill_rect(c, 4, 4, 32, 32, col, 16);
    fill_rect(c, 8, 8, 24, 24, C_OFF, 12);
    fill_rect(c, 17, 12, 6, 12, col, 1);
    fill_rect(c, 17, 26, 6, 4, col, 1);
}

static void draw_abs(lv_obj_t *c, lv_color_t col)
{
    fill_rect(c, 4, 4, 32, 32, col, 16);
    lv_obj_t *inner = fill_rect(c, 8, 8, 24, 24, C_OFF, 12);
    txt_in(inner, "ABS", col, &lv_font_montserrat_14, 0, 4);
}

static void draw_airbag(lv_obj_t *c, lv_color_t col)
{
    fill_rect(c, 6, 8, 8, 8, col, 4);
    fill_rect(c, 4, 18, 12, 14, col, 2);
    fill_rect(c, 18, 20, 18, 10, col, 9);
}

static void draw_seatbelt(lv_obj_t *c, lv_color_t col)
{
    fill_rect(c, 14, 6, 12, 8, col, 4);
    fill_rect(c, 8, 16, 24, 18, col, 4);
    fill_rect(c, 12, 18, 4, 14, C_OFF, 1);
    fill_rect(c, 24, 18, 4, 14, C_OFF, 1);
    fill_rect(c, 18, 22, 4, 4,  col,   1);
}

static void draw_engine(lv_obj_t *c, lv_color_t col)
{
    fill_rect(c, 6, 14, 28, 16, col, 3);
    fill_rect(c, 10, 8, 8, 6, col, 1);
    fill_rect(c, 22, 8, 8, 6, col, 1);
    fill_rect(c, 4, 18, 4, 8, col, 1);
    fill_rect(c, 32, 18, 4, 8, col, 1);
    fill_rect(c, 12, 17, 16, 10, C_OFF, 2);
}

static void draw_battery(lv_obj_t *c, lv_color_t col)
{
    fill_rect(c, 8, 8, 4, 4, col, 0);
    fill_rect(c, 28, 8, 4, 4, col, 0);
    fill_rect(c, 4, 12, 32, 22, col, 3);
    fill_rect(c, 12, 22, 6, 2, C_OFF, 1);
    fill_rect(c, 14, 19, 2, 8, C_OFF, 1);
    fill_rect(c, 24, 22, 6, 2, C_OFF, 1);
}

static void draw_oil(lv_obj_t *c, lv_color_t col)
{
    fill_rect(c, 8, 18, 24, 16, col, 4);
    fill_rect(c, 12, 14, 8, 4, col, 1);
    fill_rect(c, 24, 12, 4, 6, col, 2);
    fill_rect(c, 25, 18, 2, 4, col, 1);
    fill_rect(c, 30, 22, 4, 2, col, 0);
}

static void draw_coolant(lv_obj_t *c, lv_color_t col)
{
    fill_rect(c, 16, 4, 8, 24, col, 3);
    fill_rect(c, 12, 22, 16, 12, col, 6);
    fill_rect(c, 4, 28, 32, 2, col, 1);
}

static void draw_fuel_low(lv_obj_t *c, lv_color_t col)
{
    fill_rect(c, 6, 8, 18, 26, col, 2);
    fill_rect(c, 9, 11, 12, 8, C_OFF, 1);
    fill_rect(c, 24, 14, 4, 16, col, 2);
    fill_rect(c, 28, 14, 4, 4, col, 1);
}

static void draw_for(lv_obj_t *target, icon_id_t id, lv_color_t col)
{
    switch (id) {
        case ICON_TURN_L:    draw_arrow(target, col, false); break;
        case ICON_TURN_R:    draw_arrow(target, col, true);  break;
        case ICON_HIGH_BEAM: draw_high_beam(target, col);    break;
        case ICON_LOW_BEAM:  draw_low_beam(target, col);     break;
        case ICON_BRAKE:     draw_brake(target, col);        break;
        case ICON_ABS:       draw_abs(target, col);          break;
        case ICON_AIRBAG:    draw_airbag(target, col);       break;
        case ICON_SEATBELT:  draw_seatbelt(target, col);     break;
        case ICON_ENGINE:    draw_engine(target, col);       break;
        case ICON_BATTERY:   draw_battery(target, col);      break;
        case ICON_OIL:       draw_oil(target, col);          break;
        case ICON_COOLANT:   draw_coolant(target, col);      break;
        case ICON_FUEL_LOW:  draw_fuel_low(target, col);     break;
    }
}

/* Mimari:
 *   Her icon container'ında iki çocuk var: inactive (DIM) ALWAYS görünür,
 *   active (canlı renk) opacity 0..255 arasında modülasyonlu üstüne biner.
 *   - opa=0   : sadece inactive görünür (OFF)
 *   - opa=255 : active tam kaplar (ON)
 *   - opa~ara : blend (PULSE)
 *
 *   Tek bir periyodik lv_timer (50 ms) tüm icon'ları senkron günceller.
 *   Tüm sinyaller sync'd: dörtlü çakar, çift kritik uyarı aynı fazda pulse'lar. */

#define MAX_ICONS 16
typedef struct {
    lv_obj_t   *active_layer;
    icon_mode_t mode;
    uint8_t     last_opa;     /* gereksiz invalidate'i atlamak için cache */
} icon_info_t;

static lv_obj_t  *all_icons[MAX_ICONS];
static int        n_icons = 0;
static lv_timer_t *anim_timer = NULL;
static uint32_t   anim_t0_ms = 0;
#define BOOT_CHECK_MS  2000

static icon_info_t *icon_info(lv_obj_t *icon)
{
    return (icon_info_t *)lv_obj_get_user_data(icon);
}

lv_obj_t *icon_create(lv_obj_t *parent, icon_id_t id, int x, int y)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, ICON_SZ, ICON_SZ);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_style_bg_color(c, C_OFF, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 8, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Inactive layer (DIM) — daima görünür */
    lv_obj_t *inactive = lv_obj_create(c);
    lv_obj_remove_style_all(inactive);
    lv_obj_set_size(inactive, ICON_SZ, ICON_SZ);
    lv_obj_set_pos(inactive, 0, 0);
    lv_obj_set_style_bg_opa(inactive, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(inactive, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    draw_for(inactive, id, C_DIM);

    /* Active layer (canlı renk) — opa ile modüle edilir, HIDDEN değil */
    lv_obj_t *active = lv_obj_create(c);
    lv_obj_remove_style_all(active);
    lv_obj_set_size(active, ICON_SZ, ICON_SZ);
    lv_obj_set_pos(active, 0, 0);
    lv_obj_set_style_bg_opa(active, LV_OPA_TRANSP, 0);
    lv_obj_set_style_opa(active, 0, 0);   /* başta görünmez */
    lv_obj_clear_flag(active, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    draw_for(active, id, icon_color(id));

    icon_info_t *info = malloc(sizeof(icon_info_t));
    info->active_layer = active;
    info->mode = ICON_MODE_OFF;
    info->last_opa = 0;
    lv_obj_set_user_data(c, info);

    if (n_icons < MAX_ICONS) all_icons[n_icons++] = c;
    return c;
}

void icon_set_mode(lv_obj_t *icon, icon_mode_t mode)
{
    icon_info(icon)->mode = mode;
}

/* Geriye dönük uyumluluk: bool → ON/OFF mode */
void icon_set_active(lv_obj_t *icon, bool active)
{
    icon_set_mode(icon, active ? ICON_MODE_ON : ICON_MODE_OFF);
}

static void anim_tick_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t now = lv_tick_get();
    if (anim_t0_ms == 0) anim_t0_ms = now;
    bool boot_check = (now - anim_t0_ms) < BOOT_CHECK_MS;

    /* 2 Hz blink — her 250 ms toggle */
    bool blink_on = ((now / 250) & 1) == 0;

    /* 1 Hz triangular pulse: 0..1000 ms, opa 90..255 */
    uint32_t ph = now % 1000;
    uint32_t up = ph < 500 ? ph : (1000 - ph);   /* 0..500..0 */
    uint8_t pulse_opa = 90 + (uint8_t)((255 - 90) * up / 500);

    for (int i = 0; i < n_icons; i++) {
        icon_info_t *info = icon_info(all_icons[i]);
        icon_mode_t mode = boot_check ? ICON_MODE_ON : info->mode;
        uint8_t opa;
        switch (mode) {
            case ICON_MODE_OFF:   opa = 0;                       break;
            case ICON_MODE_ON:    opa = LV_OPA_COVER;            break;
            case ICON_MODE_BLINK: opa = blink_on ? LV_OPA_COVER : 0; break;
            case ICON_MODE_PULSE: opa = pulse_opa;               break;
            default:              opa = 0;                       break;
        }
        if (opa != info->last_opa) {
            lv_obj_set_style_opa(info->active_layer, opa, 0);
            info->last_opa = opa;
        }
    }
}

void icons_anim_init(void)
{
    if (anim_timer) return;
    anim_t0_ms = lv_tick_get();
    anim_timer = lv_timer_create(anim_tick_cb, 50, NULL);
}
