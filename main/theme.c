#include "theme.h"

/* Global palette — ui.c bu değişkenlere bağlı (önceden #define idi).
 * theme_apply(id) seçilen palet'i bu değişkenlere yazar. */
lv_color_t C_BG, C_PANEL, C_FG, C_DIM, C_RED, C_GREEN, C_AMBER,
           C_BLUE, C_ACCENT, C_RING,
           C_LBL_DIM, C_LBL_BRIGHT, C_TICK_LBL, C_BACKPLATE;

static theme_id_t s_active = THEME_BLUE;

const char *theme_name(theme_id_t id)
{
    switch (id) {
        case THEME_BLUE:   return "Blue";
        case THEME_ORANGE: return "Orange";
        case THEME_YELLOW: return "Yellow";
        case THEME_RED:    return "Red";
        default:           return "?";
    }
}

theme_id_t theme_active(void) { return s_active; }

void theme_apply(theme_id_t id)
{
    if (id >= THEME_COUNT) id = THEME_BLUE;
    s_active = id;

    /* Ortak (tüm temalarda aynı): RED=tehlike, GREEN=info, BLUE=aksesuar.
     * Bunlar ISO 2575 standardı, marka teması değiştirmiyor. */
    C_RED   = lv_color_hex(0xff2030);
    C_GREEN = lv_color_hex(0x22c55e);
    C_BLUE  = lv_color_hex(0x4488ff);

    switch (id) {
    case THEME_BLUE:
    default:
        C_BG          = lv_color_hex(0x0a0e14);
        C_PANEL       = lv_color_hex(0x0d1424);
        C_FG          = lv_color_hex(0xeef4fb);
        C_DIM         = lv_color_hex(0x4a5568);
        C_AMBER       = lv_color_hex(0xffb400);
        C_ACCENT      = lv_color_hex(0x00d4ff);
        C_RING        = lv_color_hex(0x1c2436);
        C_LBL_DIM     = lv_color_hex(0x6b7c91);
        C_LBL_BRIGHT  = lv_color_hex(0xffffff);
        C_TICK_LBL    = lv_color_hex(0xe6edf5);
        C_BACKPLATE   = lv_color_hex(0x1f2a3a);
        break;

    case THEME_ORANGE:
        C_BG          = lv_color_hex(0x000000);
        C_PANEL       = lv_color_hex(0x141414);
        C_FG          = lv_color_hex(0xffffff);
        C_DIM         = lv_color_hex(0x666666);
        C_AMBER       = lv_color_hex(0xff8c00);   /* warm orange */
        C_ACCENT      = lv_color_hex(0xff8c00);
        C_RING        = lv_color_hex(0x222222);
        C_LBL_DIM     = lv_color_hex(0x808080);
        C_LBL_BRIGHT  = lv_color_hex(0xffffff);
        C_TICK_LBL    = lv_color_hex(0xeeeeee);
        C_BACKPLATE   = lv_color_hex(0x1c1c1c);
        break;

    case THEME_YELLOW:
        C_BG          = lv_color_hex(0x0a0a0a);
        C_PANEL       = lv_color_hex(0x141414);
        C_FG          = lv_color_hex(0xe5e5e5);
        C_DIM         = lv_color_hex(0x444444);
        C_AMBER       = lv_color_hex(0xffd700);   /* yellow */
        C_ACCENT      = lv_color_hex(0xffd700);
        C_RING        = lv_color_hex(0x1a1a1a);
        C_LBL_DIM     = lv_color_hex(0x707070);
        C_LBL_BRIGHT  = lv_color_hex(0xffffff);
        C_TICK_LBL    = lv_color_hex(0xe0e0e0);
        C_BACKPLATE   = lv_color_hex(0x1a1a1a);
        break;

    case THEME_RED:
        C_BG          = lv_color_hex(0x000000);
        C_PANEL       = lv_color_hex(0x0a0a0a);
        C_FG          = lv_color_hex(0xc0c0c0);
        C_DIM         = lv_color_hex(0x404040);
        C_AMBER       = lv_color_hex(0xe53935);   /* red as warn */
        C_ACCENT      = lv_color_hex(0xe53935);
        C_RING        = lv_color_hex(0x181818);
        C_LBL_DIM     = lv_color_hex(0x606060);
        C_LBL_BRIGHT  = lv_color_hex(0xe0e0e0);
        C_TICK_LBL    = lv_color_hex(0xb0b0b0);
        C_BACKPLATE   = lv_color_hex(0x121212);
        break;
    }
}
