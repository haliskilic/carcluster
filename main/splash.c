#include "splash.h"
#include "board.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stddef.h>

static const char *TAG = "splash";
static lv_obj_t *s_root = NULL;

/* HK monogram tasarımı:
 *   H: 2 dikey bar + 1 yatay orta bar
 *   K: 1 dikey bar + 2 diagonal (lv_line)
 * Her harf 80×120 px, stroke 18 px. Aralarında 30 px boşluk.
 * Toplam genişlik 190 px. Ekran 800×480, merkez (400, 240). */

#define LH    120        /* letter height */
#define LW    80         /* letter width  */
#define ST    18         /* stroke thickness */
#define GAP   30         /* harfler arası boşluk */
#define LX    (400 - (LW + GAP/2 + LW + GAP/2))   /* hesap aşağıda yeniden */
#define LY    (240 - LH/2)

/* Düz bar — lv_obj as opaque rectangle */
static void make_rect(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t c)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, w, h);
    lv_obj_set_pos(r, x, y);
    lv_obj_set_style_bg_color(r, c, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r, 2, 0);
}

/* Diagonal — lv_line ile (LVGL stroke widget) */
static void make_line(lv_obj_t *parent, int x1, int y1, int x2, int y2,
                      int thick, lv_color_t c)
{
    static lv_point_t pts_pool[8][2];   /* up to 8 lines */
    static int idx = 0;
    if (idx >= 8) return;
    pts_pool[idx][0].x = x1; pts_pool[idx][0].y = y1;
    pts_pool[idx][1].x = x2; pts_pool[idx][1].y = y2;

    lv_obj_t *l = lv_line_create(parent);
    lv_line_set_points(l, pts_pool[idx], 2);
    lv_obj_set_style_line_color(l, c, 0);
    lv_obj_set_style_line_width(l, thick, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_pos(l, 0, 0);
    idx++;
}

void splash_show(void)
{
    if (s_root) { ESP_LOGW(TAG, "show: already shown"); return; }

    /* Tam ekran siyah overlay — lv_scr_act'a child, move_foreground ile en üstte.
     * lvgl_port_lock altında çağrılır → ui_build sonrası tek frame'de görünür,
     * cluster widget'ları altında kalır, splash_hide ile atomik reveal olur. */
    s_root = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LCD_H_RES, LCD_V_RES);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x000000), 0);  /* PURE BLACK */
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);

    lv_color_t fg = lv_color_hex(0xffffff);   /* beyaz monogram */

    /* Layout — H sol, K sağ, ortala */
    int total_w = LW + GAP + LW;
    int hx = (LCD_H_RES - total_w) / 2;       /* H'nin sol kenarı */
    int kx = hx + LW + GAP;                    /* K'nın sol kenarı */
    int ly = (LCD_V_RES - LH) / 2;             /* tepe Y */

    /* H — 3 rect */
    make_rect(s_root, hx,           ly,           ST, LH, fg);   /* sol bar */
    make_rect(s_root, hx + LW - ST, ly,           ST, LH, fg);   /* sağ bar */
    make_rect(s_root, hx + ST,      ly + (LH - ST)/2, LW - 2*ST, ST, fg); /* orta */

    /* K — 1 rect + 2 diagonal */
    make_rect(s_root, kx, ly, ST, LH, fg);                       /* sol bar */
    int mid_y = ly + LH/2;
    make_line(s_root, kx + ST, mid_y,        kx + LW - 2, ly + 2,        ST, fg);
    make_line(s_root, kx + ST, mid_y,        kx + LW - 2, ly + LH - 2,   ST, fg);

    /* En üste taşı — sonradan ui_refresh widget add'lerse altında kalır */
    lv_obj_move_foreground(s_root);
}

void splash_hide(void)
{
    if (!s_root) return;
    ESP_LOGI(TAG, "hide");
    lv_obj_del(s_root);
    s_root = NULL;
}
