#include "splash.h"
#include "board.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "splash";

/* splash_logo.c — png_to_lvgl.py ile docs/img/bootsplash.png'den üretildi.
 * 800×480 RGB565, ~768 KB binary footprint. */
extern const lv_img_dsc_t splash_logo;

static lv_obj_t *s_root = NULL;

void splash_show(void)
{
    if (s_root) { ESP_LOGW(TAG, "show: already shown"); return; }

    /* Tam ekran siyah container — logo ortalanmış lv_img child.
     * lvgl_port_lock altında çağrılır → ui_build sonrası tek frame'de görünür,
     * cluster widget'ları altında kalır, splash_hide ile atomik reveal olur. */
    s_root = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LCD_H_RES, LCD_V_RES);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);

    /* Logo image — auto-generated header w=800 h=480, ekrana tam oturur */
    lv_obj_t *img = lv_img_create(s_root);
    lv_img_set_src(img, &splash_logo);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);

    /* Render order'da en üstte olsun (sonradan eklenen widget altında kalmasın) */
    lv_obj_move_foreground(s_root);

    ESP_LOGI(TAG, "show: logo image %dx%d", splash_logo.header.w, splash_logo.header.h);
}

void splash_hide(void)
{
    if (!s_root) return;
    ESP_LOGI(TAG, "hide");
    lv_obj_del(s_root);
    s_root = NULL;
}
