#include "ui.h"
#include "state.h"
#include "icons.h"
#include "lvgl_port.h"
#include "cpu_meter.h"
#include "trip.h"
#include "board.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "fonts_inter.h"
#include "persist.h"
#include "theme.h"
#include "units.h"
#include "limits.h"
#include "screenshot.h"

/* Renkler artık theme.c içinde global değişken — tema seçimine göre runtime'da
 * doldurulur. theme_apply(id) ui_build'den önce main.c'de çağrılır. */
extern lv_color_t C_BG, C_PANEL, C_FG, C_DIM, C_RED, C_GREEN, C_AMBER,
                  C_BLUE, C_ACCENT, C_RING;

#define ARC_R          280
#define ARC_W          16
#define LX             170
#define RX             630
#define CY             240

static lv_obj_t *meter_rpm, *meter_speed;
static lv_meter_indicator_t *ind_rpm_needle;
static lv_meter_indicator_t *ind_speed_arc, *ind_speed_needle;
static lv_obj_t *lbl_speed_val;   /* RPM ortadaki sayı kaldırıldı */
static lv_obj_t *lbl_gear, *lbl_km, *lbl_ip;
static lv_obj_t *bar_fuel, *bar_temp, *lbl_fuel_pct, *lbl_temp_val;
static lv_obj_t *lbl_fps;

/* Trip computer panel */
static lv_obj_t *lbl_trip_km, *lbl_trip_time, *lbl_trip_l100, *lbl_trip_range;

/* Custom tick label'ları — auto-label gizlendi, bunlar ayrı widget olarak yerleştirildi.
 * Needle ilerledikçe tek tek dim → bright olur (reveal efekti). */
#define RPM_N_LABELS  10
#define SPD_N_LABELS  13
static lv_obj_t *rpm_tick_labels[RPM_N_LABELS];
static lv_obj_t *spd_tick_labels[SPD_N_LABELS];

/* Forward decl — settings modal handler dosyanın altında tanımlı */
static void ui_attach_long_press_handler(void);

/* Label durum renkleri — cool navy temasıyla uyumlu, ikisi clearly birbirinden farklı.
 *   DIM    : steel-blue gray, band üzerinde "soluk" (still readable, not screaming)
 *   BRIGHT : pure white, "lit/active" — herhangi bir band rengi (yeşil→kırmızı, cyan)
 *            üzerinde max contrast */
extern lv_color_t C_LBL_DIM, C_LBL_BRIGHT;

/* Needle damping (D2): target_value vs displayed_value ayrımı.
 * Yeni hedef set edildiğinde 150ms ease-out anim başlar; ara update'ler
 * geldikçe anim restart eder (sürekli redirect). Demo'da görsel olarak
 * "smoother", CAN gerçek verisi geldiğinde noise filter rolünü üstlenir.
 *
 * Pattern: lv_anim_set_var(NULL) → her apply_speed çağrısı aynı anim
 * identity (NULL var + exec_cb), restart edilir. Eski anim cancel olur,
 * yeni anim displayed_value'dan target'a 150ms eşelir. */
static int s_speed_displayed   = 0;
static int s_speed_arc_displayed = 0;
static int s_rpm_displayed     = 0;

#define NEEDLE_ANIM_MS 150

static void anim_speed_needle_cb(void *var, int32_t v)
{
    (void)var;
    s_speed_displayed = (int)v;
    lv_meter_set_indicator_value(meter_speed, ind_speed_needle, (int32_t)v);
}
static void anim_speed_arc_cb(void *var, int32_t v)
{
    (void)var;
    s_speed_arc_displayed = (int)v;
    lv_meter_set_indicator_end_value(meter_speed, ind_speed_arc, (int32_t)v);
}
static void anim_rpm_needle_cb(void *var, int32_t v)
{
    (void)var;
    s_rpm_displayed = (int)v;
    lv_meter_set_indicator_value(meter_rpm, ind_rpm_needle, (int32_t)v);
}

static void start_anim(int32_t from, int32_t to,
                       lv_anim_exec_xcb_t cb, void *var)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, var);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, NEEDLE_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_start(&a);
}

static void apply_speed(int v)
{
    int spd = v > 240 ? 240 : (v < 0 ? 0 : v);

    /* Damped needle + arc fill — ease-out, sürekli redirect-on-update */
    static int prev_target = -1;
    if (spd != prev_target) {
        start_anim(s_speed_displayed,     spd, anim_speed_needle_cb, (void*)1);
        start_anim(s_speed_arc_displayed, spd, anim_speed_arc_cb,    (void*)2);
        prev_target = spd;
    }

    char buf[16];
    snprintf(buf, sizeof(buf), "%d", unit_conv_speed(v));
    lv_label_set_text(lbl_speed_val, buf);

    /* Reveal: needle taradığı sayıları parlatır. lit = en yüksek aktif index. */
    static int prev_lit = -2;
    int lit = spd / 20;             /* 0..12 (240/20=12) */
    if (lit != prev_lit) {
        for (int i = 0; i < SPD_N_LABELS; i++) {
            lv_obj_set_style_text_color(spd_tick_labels[i],
                i <= lit ? C_LBL_BRIGHT : C_LBL_DIM, 0);
        }
        prev_lit = lit;
    }
}

static void apply_rpm(int v)
{
    /* Smooth scale 0-90 (RPM/100) — needle 91 pozisyonda hareket eder,
     * gerçek araba gibi her ara değerden geçer (1500 RPM=15, 2350 RPM=23 ...) */
    int rpm_x10 = v / 100;
    if (rpm_x10 > 90) rpm_x10 = 90;
    if (rpm_x10 < 0)  rpm_x10 = 0;

    /* Damped needle */
    static int prev_target = -1;
    if (rpm_x10 != prev_target) {
        start_anim(s_rpm_displayed, rpm_x10, anim_rpm_needle_cb, (void*)3);
        prev_target = rpm_x10;
    }

    /* Reveal: needle taradığı sayıları parlatır. Bandlar her zaman tam görünür. */
    static int prev_lit = -2;
    int lit = rpm_x10 / 10;         /* 0..9 */
    if (lit != prev_lit) {
        for (int i = 0; i < RPM_N_LABELS; i++) {
            lv_obj_set_style_text_color(rpm_tick_labels[i],
                i <= lit ? C_LBL_BRIGHT : C_LBL_DIM, 0);
        }
        prev_lit = lit;
    }
}

static lv_obj_t *ic_turn_l, *ic_turn_r;
static lv_obj_t *ic_high, *ic_low;
static lv_obj_t *ic_brake, *ic_abs, *ic_airbag, *ic_seat;
static lv_obj_t *ic_engine, *ic_battery, *ic_oil, *ic_coolant, *ic_fuel_low;

extern lv_color_t C_TICK_LBL, C_BACKPLATE;

/* A1 SNAPSHOT CACHE — statik gauge layer'ı bir kez render → lv_img blit.
 * Her frame'de 5 color band + tick lines + backplate rasterize maliyeti
 * ortadan kalkar (LVGL meter rasterize = en büyük CPU tüketici).
 *
 * Snapshot buffer LVGL heap'te değil DOĞRUDAN PSRAM'den (heap_caps_malloc
 * + MALLOC_CAP_SPIRAM) ayrılır → LV_MEM_SIZE limiti aşılmaz.
 * lv_snapshot_take_to_buf API caller-provided buf alır.
 *
 * Memory: 280×280×4 RGBA = 313KB × 2 gauge = 626KB PSRAM (8MB'den bol). */

/* Statik katman — scale ticks + backplate + RPM color bands. Snapshot için. */
static lv_obj_t *make_meter_static(int cx, int cy, int size,
                                    int v_min, int v_max,
                                    int n_majors, int n_minor_per_major,
                                    int smooth_factor, bool with_rpm_bands)
{
    lv_obj_t *m = lv_meter_create(lv_scr_act());
    lv_obj_set_size(m, size, size);
    lv_obj_set_pos(m, cx - size/2, cy - size/2);
    lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m, 0, 0);
    lv_obj_set_style_pad_all(m, 0, 0);
    lv_obj_set_style_outline_width(m, 0, 0);
    lv_obj_clear_flag(m, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_style_text_opa(m, LV_OPA_TRANSP, LV_PART_TICKS);
    lv_obj_set_style_text_font(m, &lv_font_inter_18, LV_PART_TICKS);

    lv_meter_scale_t *sc_visual = lv_meter_add_scale(m);
    int total_minor = (n_majors - 1) * n_minor_per_major + 1;
    lv_meter_set_scale_ticks(m, sc_visual, total_minor, 2, 6, C_DIM);
    lv_meter_set_scale_major_ticks(m, sc_visual, n_minor_per_major, 4, 6, C_TICK_LBL, 24);
    lv_meter_set_scale_range(m, sc_visual, v_min, v_max, 270, 135);

    lv_meter_scale_t *sc_smooth = sc_visual;
    int smin = v_min, smax = v_max;
    if (smooth_factor > 1) {
        sc_smooth = lv_meter_add_scale(m);
        lv_meter_set_scale_ticks(m, sc_smooth, 0, 0, 0, C_DIM);
        smin = v_min * smooth_factor;
        smax = v_max * smooth_factor;
        lv_meter_set_scale_range(m, sc_smooth, smin, smax, 270, 135);
    }

    lv_meter_indicator_t *bp = lv_meter_add_arc(m, sc_smooth, 40, C_BACKPLATE, -30);
    lv_meter_set_indicator_start_value(m, bp, smin);
    lv_meter_set_indicator_end_value(m, bp, smax);

    if (with_rpm_bands) {
        /* B7: redline user-adjustable. Bandlar redline'a göre orantılı kayar.
         * Default 7000 → eski sabit değerlerle birebir aynı renk dağılımı. */
        int redline_x10 = limits_get()->rpm_redline / 100;
        if (redline_x10 < 50) redline_x10 = 50;
        if (redline_x10 > 90) redline_x10 = 90;
        struct { int from, to; uint32_t color; } bands[] = {
            { 0,                20,                0x22c55e }, /* green */
            { 20,               redline_x10 - 30,  0x84cc16 }, /* light green */
            { redline_x10 - 30, redline_x10 - 20,  0xeab308 }, /* yellow */
            { redline_x10 - 20, redline_x10,       0xf97316 }, /* orange */
            { redline_x10,      90,                0xff2030 }, /* red */
        };
        int n = sizeof(bands) / sizeof(bands[0]);
        for (int i = 0; i < n; i++) {
            if (bands[i].to <= bands[i].from) continue;
            lv_meter_indicator_t *band = lv_meter_add_arc(m, sc_smooth, 40,
                                                         lv_color_hex(bands[i].color), -30);
            lv_meter_set_indicator_start_value(m, band, bands[i].from);
            lv_meter_set_indicator_end_value(m, band, bands[i].to);
        }
    }
    return m;
}

/* Dinamik katman — yalnızca needle (+ speed için arc fill).
 * Smooth scale tick'siz; needle açısı için range gerekli. */
static lv_obj_t *make_meter_dynamic(int cx, int cy, int size,
                                     int v_min, int v_max,
                                     lv_color_t arc_col,
                                     int smooth_factor, bool with_rpm_bands,
                                     lv_meter_indicator_t **ret_arc_ind,
                                     lv_meter_indicator_t **ret_needle)
{
    lv_obj_t *m = lv_meter_create(lv_scr_act());
    lv_obj_set_size(m, size, size);
    lv_obj_set_pos(m, cx - size/2, cy - size/2);
    lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m, 0, 0);
    lv_obj_set_style_pad_all(m, 0, 0);
    lv_obj_set_style_outline_width(m, 0, 0);
    lv_obj_clear_flag(m, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_meter_scale_t *sc = lv_meter_add_scale(m);
    lv_meter_set_scale_ticks(m, sc, 0, 0, 0, C_DIM);
    int sf = smooth_factor > 1 ? smooth_factor : 1;
    int smin = v_min * sf;
    int smax = v_max * sf;
    lv_meter_set_scale_range(m, sc, smin, smax, 270, 135);

    if (!with_rpm_bands) {
        lv_meter_indicator_t *arc = lv_meter_add_arc(m, sc, 40, arc_col, -30);
        lv_meter_set_indicator_start_value(m, arc, smin);
        lv_meter_set_indicator_end_value(m, arc, smin);
        if (ret_arc_ind) *ret_arc_ind = arc;
    } else {
        if (ret_arc_ind) *ret_arc_ind = NULL;
    }

    *ret_needle = lv_meter_add_needle_line(m, sc, 4, lv_color_hex(0xffffff), -10);
    lv_meter_set_indicator_value(m, *ret_needle, smin);
    return m;
}

/* Snapshot orchestrator: statik meter oluştur → PSRAM'de buf ayır →
 * lv_snapshot_take_to_buf ile render → orijinali sil → lv_img olarak ekle →
 * üstüne dinamik katman. Ret = dinamik meter (apply_speed/rpm bunu kullanır). */
static lv_obj_t *make_meter(int cx, int cy, int size,
                            int v_min, int v_max,
                            int n_majors, int n_minor_per_major,
                            lv_color_t arc_col,
                            int smooth_factor, bool with_rpm_bands,
                            lv_meter_indicator_t **ret_arc_ind,
                            lv_meter_indicator_t **ret_needle)
{
    lv_obj_t *st = make_meter_static(cx, cy, size, v_min, v_max,
                                      n_majors, n_minor_per_major,
                                      smooth_factor, with_rpm_bands);

    /* PSRAM'den buffer ayır — LVGL heap baypas */
    uint32_t buf_size = lv_snapshot_buf_size_needed(st, LV_IMG_CF_TRUE_COLOR_ALPHA);
    void *buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE("ui", "snapshot buf alloc failed (%lu bytes)", (unsigned long)buf_size);
        abort();
    }
    /* dsc statik storage — lv_img buna ref tutar (lifetime program boyunca) */
    /* Pool için sabit kapasite — 2 gauge (RPM + Speed). 3. çağrı = bug,
     * abort ile yakala (sessiz stack corruption yerine). */
    static lv_img_dsc_t dsc_pool[2];
    static int dsc_idx = 0;
    if (dsc_idx >= (int)(sizeof(dsc_pool) / sizeof(dsc_pool[0]))) {
        ESP_LOGE("ui", "snapshot dsc pool overflow (max %d)",
                 (int)(sizeof(dsc_pool) / sizeof(dsc_pool[0])));
        abort();
    }
    lv_img_dsc_t *dsc = &dsc_pool[dsc_idx++];

    if (lv_snapshot_take_to_buf(st, LV_IMG_CF_TRUE_COLOR_ALPHA, dsc, buf, buf_size)
        != LV_RES_OK) {
        ESP_LOGE("ui", "lv_snapshot_take_to_buf failed");
        abort();
    }
    lv_obj_del(st);

    lv_obj_t *bg = lv_img_create(lv_scr_act());
    lv_img_set_src(bg, dsc);
    lv_obj_set_pos(bg, cx - size/2, cy - size/2);

    return make_meter_dynamic(cx, cy, size, v_min, v_max, arc_col,
                               smooth_factor, with_rpm_bands,
                               ret_arc_ind, ret_needle);
}

/* Custom tick label'larını meter'ın etrafına yerleştirir.
 *   cx,cy        : meter merkezi (ekran koordinatı)
 *   radius       : label merkez radius'u (110 → bant ortası)
 *   start_deg    : ilk label'ın açısı (LVGL: 0=sağ, 90=aşağı, 270=yukarı)
 *   step_deg     : ardışık label'lar arası açı
 *   value_step   : label[i] = i * value_step (RPM:1, speed:20) */
static void place_tick_labels(lv_obj_t *parent, lv_obj_t **labels, int count,
                              int cx, int cy, int radius,
                              float start_deg, float step_deg, int value_step)
{
    for (int i = 0; i < count; i++) {
        float a_rad = (start_deg + i * step_deg) * (float)M_PI / 180.0f;
        int x = cx + (int)(radius * cosf(a_rad));
        int y = cy + (int)(radius * sinf(a_rad));

        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text_fmt(lbl, "%d", i * value_step);
        lv_obj_set_style_text_font(lbl, &lv_font_inter_18, 0);
        lv_obj_set_style_text_color(lbl, C_LBL_DIM, 0);

        lv_point_t txt_size;
        lv_txt_get_size(&txt_size, lv_label_get_text(lbl),
                        &lv_font_inter_18, 0, 0, LV_COORD_MAX, 0);
        lv_obj_set_pos(lbl, x - txt_size.x / 2, y - txt_size.y / 2);
        labels[i] = lbl;
    }
}

/* Trip panel satırı: solda küçük dim label, sağda parlak değer (right-aligned).
 * panel_w: panel genişliği (right alignment için). */
static lv_obj_t *make_trip_row(lv_obj_t *parent, int y, int panel_w,
                               const char *label_text, const char *init_value)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, label_text);
    lv_obj_set_style_text_color(l, C_DIM, 0);
    lv_obj_set_style_text_font(l, &lv_font_inter_14, 0);
    lv_obj_set_pos(l, 12, y + 3);    /* küçük font, 3px alt offset için baseline align */

    lv_obj_t *v = lv_label_create(parent);
    lv_label_set_text(v, init_value);
    lv_obj_set_style_text_color(v, C_FG, 0);
    lv_obj_set_style_text_font(v, &lv_font_inter_18, 0);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(v, panel_w - 90);   /* sağ tarafta sabit kolon */
    lv_obj_set_pos(v, 78, y);
    return v;
}

void ui_build(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Üst telltale ikonları */
    int yi = 8;
    ic_turn_l   = icon_create(scr, ICON_TURN_L,    20, yi);
    ic_low      = icon_create(scr, ICON_LOW_BEAM,  72, yi);
    ic_high     = icon_create(scr, ICON_HIGH_BEAM,118, yi);
    ic_abs      = icon_create(scr, ICON_ABS,      180, yi);
    ic_engine   = icon_create(scr, ICON_ENGINE,   226, yi);
    ic_fuel_low = icon_create(scr, ICON_FUEL_LOW, 272, yi);
    ic_oil      = icon_create(scr, ICON_OIL,      334, yi);
    ic_coolant  = icon_create(scr, ICON_COOLANT,  380, yi);
    ic_battery  = icon_create(scr, ICON_BATTERY,  426, yi);
    ic_brake    = icon_create(scr, ICON_BRAKE,    488, yi);
    ic_airbag   = icon_create(scr, ICON_AIRBAG,   534, yi);
    ic_seat     = icon_create(scr, ICON_SEATBELT, 580, yi);
    ic_turn_r   = icon_create(scr, ICON_TURN_R,   740, yi);

    /* Sol: RPM kadranı — görünür label 0-9, internal smooth scale 0-90 (smooth_factor=10).
     * Color bands gradient her zaman tam görünür; sayılar reveal ile parlatılır. */
    meter_rpm = make_meter(LX, CY, ARC_R, 0, 9, 10, 5, C_RED, 10, true,
                           NULL, &ind_rpm_needle);
    /* RPM custom label'ları: 10 adet, 135°→405°, 30° step, value 0..9 */
    place_tick_labels(scr, rpm_tick_labels, RPM_N_LABELS, LX, CY, 90,
                      135.0f, 30.0f, 1);

    lv_obj_t *u1 = lv_label_create(scr);
    lv_obj_set_style_text_color(u1, C_DIM, 0);
    lv_obj_set_style_text_font(u1, &lv_font_inter_18, 0);
    lv_label_set_text(u1, "RPM x 1000");
    /* Offset 50→90: alt tick label'ları (y≈304, val 0 ve val 9) ile çakışmasın.
     * Yeni y=330 → label spans 321-339, tick label'ları y=315'te biter (6px gap). */
    lv_obj_align_to(u1, meter_rpm, LV_ALIGN_CENTER, 0, 90);

    /* Sağ: Hız kadranı (0-240, smooth_factor=1, range zaten yeterince ince) */
    meter_speed = make_meter(RX, CY, ARC_R, 0, 240, 13, 4, C_ACCENT, 1, false,
                             &ind_speed_arc, &ind_speed_needle);
    /* Speed custom label'ları: 13 adet, 135°→405°, 22.5° step, value 0,20,...,240 */
    place_tick_labels(scr, spd_tick_labels, SPD_N_LABELS, RX, CY, 90,
                      135.0f, 22.5f, 20);

    /* km/h yazısı kadranın merkezinden biraz yukarıda */
    lv_obj_t *u2 = lv_label_create(scr);
    lv_obj_set_style_text_color(u2, C_DIM, 0);
    lv_obj_set_style_text_font(u2, &lv_font_inter_18, 0);
    lv_label_set_text(u2, unit_speed_label());
    lv_obj_align_to(u2, meter_speed, LV_ALIGN_CENTER, 0, 50);

    /* Anlık hız sayısı km/h yazısının ALTINDA — iğne alanından bağımsız.
     * Sabit 110px genişlik + center-align: 0/45/240 farklı uzunlukta olsa da
     * label "dans etmiyor", hep aynı yerde kalıyor. */
    lbl_speed_val = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_speed_val, C_FG, 0);
    lv_obj_set_style_text_font(lbl_speed_val, &lv_font_inter_36, 0);
    lv_obj_set_style_text_align(lbl_speed_val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_speed_val, 110);
    lv_label_set_text(lbl_speed_val, "0");
    lv_obj_align_to(lbl_speed_val, u2, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    /* Orta: Vites */
    lv_obj_t *gb = lv_obj_create(scr);
    lv_obj_remove_style_all(gb);
    lv_obj_set_size(gb, 80, 80);
    lv_obj_align(gb, LV_ALIGN_TOP_MID, 0, 180);
    lv_obj_set_style_bg_color(gb, C_PANEL, 0);
    lv_obj_set_style_bg_opa(gb, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(gb, 12, 0);
    lv_obj_clear_flag(gb, LV_OBJ_FLAG_SCROLLABLE);
    lbl_gear = lv_label_create(gb);
    lv_obj_set_style_text_color(lbl_gear, C_GREEN, 0);
    lv_obj_set_style_text_font(lbl_gear, &lv_font_inter_48, 0);
    lv_label_set_text(lbl_gear, "P");
    lv_obj_center(lbl_gear);

    /* Trip computer paneli — header'sız, kompakt 4 satır.
     * Genişlik 180: sol RPM val 8 tick'i (x≈299-305) ve sağ speed val 40 tick'i
     * ile çakışmıyor. Yükseklik 88: 4 satır × 18px + üst/alt marj. */
    {
        const int TPW = 180;
        const int TPH = 88;
        lv_obj_t *tp = lv_obj_create(scr);
        lv_obj_remove_style_all(tp);
        lv_obj_set_size(tp, TPW, TPH);
        lv_obj_set_pos(tp, 400 - TPW / 2, 280);
        lv_obj_set_style_bg_color(tp, C_PANEL, 0);
        lv_obj_set_style_bg_opa(tp, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(tp, 12, 0);
        lv_obj_clear_flag(tp, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        /* 4 satır: TRIP / TIME / L/100 (veya MPG) / RANGE — header yok.
         * Unit label'lar boot'ta unit setting'e göre yerleşir. */
        char trip_init[16], range_init[16];
        snprintf(trip_init,  sizeof(trip_init),  "0.0 %s", unit_dist_label());
        snprintf(range_init, sizeof(range_init), "-- %s",  unit_dist_label());
        lbl_trip_km    = make_trip_row(tp,  8, TPW, "TRIP",                trip_init);
        lbl_trip_time  = make_trip_row(tp, 26, TPW, "TIME",                "00:00");
        lbl_trip_l100  = make_trip_row(tp, 44, TPW, unit_consum_label(),   "0.0");
        lbl_trip_range = make_trip_row(tp, 62, TPW, "RANGE",               range_init);
    }

    /* Alt şerit */
    int by = 388;

    lv_obj_t *fl = lv_label_create(scr);
    lv_label_set_text(fl, "FUEL");
    lv_obj_set_style_text_color(fl, C_DIM, 0);
    lv_obj_set_style_text_font(fl, &lv_font_inter_18, 0);
    lv_obj_set_pos(fl, 30, by);

    bar_fuel = lv_bar_create(scr);
    lv_obj_set_size(bar_fuel, 220, 16);
    lv_obj_set_pos(bar_fuel, 30, by + 22);
    lv_bar_set_range(bar_fuel, 0, 100);
    lv_obj_set_style_bg_color(bar_fuel, C_RING, 0);
    lv_obj_set_style_bg_color(bar_fuel, C_GREEN, LV_PART_INDICATOR);

    /* fuel %: sabit 50px width + right-align → 0%/12%/100% farklı uzunlukta
     * olsa da sağ kenar sabit, etiket dans etmiyor */
    lbl_fuel_pct = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_fuel_pct, C_FG, 0);
    lv_obj_set_style_text_font(lbl_fuel_pct, &lv_font_inter_18, 0);
    lv_obj_set_style_text_align(lbl_fuel_pct, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(lbl_fuel_pct, 50);
    lv_label_set_text(lbl_fuel_pct, "100%");
    lv_obj_set_pos(lbl_fuel_pct, 200, by);

    lv_obj_t *tp = lv_label_create(scr);
    lv_label_set_text(tp, "COOLANT");
    lv_obj_set_style_text_color(tp, C_DIM, 0);
    lv_obj_set_style_text_font(tp, &lv_font_inter_18, 0);
    lv_obj_set_pos(tp, 550, by);

    bar_temp = lv_bar_create(scr);
    lv_obj_set_size(bar_temp, 220, 16);
    lv_obj_set_pos(bar_temp, 550, by + 22);
    lv_bar_set_range(bar_temp, -40, 150);
    lv_obj_set_style_bg_color(bar_temp, C_RING, 0);
    lv_obj_set_style_bg_color(bar_temp, C_AMBER, LV_PART_INDICATOR);

    /* temp: sabit 50px right-align → -40C/0C/150C arasında label sabit kalır */
    lbl_temp_val = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_temp_val, C_FG, 0);
    lv_obj_set_style_text_font(lbl_temp_val, &lv_font_inter_18, 0);
    lv_obj_set_style_text_align(lbl_temp_val, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(lbl_temp_val, 50);
    lv_label_set_text(lbl_temp_val, "0C");
    lv_obj_set_pos(lbl_temp_val, 720, by);

    /* ODO 3 parça — "ODO" sabit + sayı (sağ-hizalı) + "km" sabit pozisyonda.
     * Sadece sayı label dirty olur, "ODO" ve "km" sabit kalır → render iş yükü ↓ */
    lv_obj_t *odo_lbl = lv_label_create(scr);
    lv_label_set_text(odo_lbl, "ODO");
    lv_obj_set_style_text_color(odo_lbl, C_DIM, 0);
    lv_obj_set_style_text_font(odo_lbl, &lv_font_inter_14, 0);
    /* -64 → -80: ODO yazısı 1 karakter (~16px) sola, lbl_km uzun değerlerde
     * (1M+ km) sola taştığında çakışmasın */
    lv_obj_align(odo_lbl, LV_ALIGN_BOTTOM_MID, -80, -22);

    lbl_km = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_km, C_FG, 0);
    lv_obj_set_style_text_font(lbl_km, &lv_font_inter_24, 0);
    lv_obj_set_style_text_align(lbl_km, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(lbl_km, 100);
    lv_label_set_text(lbl_km, "0");
    lv_obj_align(lbl_km, LV_ALIGN_BOTTOM_MID, -10, -20);

    lv_obj_t *odo_unit = lv_label_create(scr);
    lv_label_set_text(odo_unit, unit_dist_label());
    lv_obj_set_style_text_color(odo_unit, C_DIM, 0);
    lv_obj_set_style_text_font(odo_unit, &lv_font_inter_18, 0);
    lv_obj_align(odo_unit, LV_ALIGN_BOTTOM_MID, 60, -22);

    lbl_ip = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_ip, C_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_ip, &lv_font_inter_14, 0);
    lv_label_set_text(lbl_ip, "WiFi: ...");
    lv_obj_align(lbl_ip, LV_ALIGN_BOTTOM_LEFT, 8, -4);

    lbl_fps = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_fps, C_AMBER, 0);
    lv_obj_set_style_text_font(lbl_fps, &lv_font_inter_14, 0);
    lv_obj_set_style_text_align(lbl_fps, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(lbl_fps, "R-FPS: --  DR-FPS: --  C0: --%  C1: --%");
    lv_obj_align(lbl_fps, LV_ALIGN_BOTTOM_RIGHT, -8, -4);

    /* Telltale animasyonu — periyodik 50 ms timer, tüm icon'lar sync'd.
     * İlk 2 sn boot check (tüm icon'lar ON) burada otomatik başlar. */
    icons_anim_init();

    /* Touch UI — ekrana long-press'e settings modal aç */
    ui_attach_long_press_handler();
}

void ui_refresh(void)
{
    static uint32_t last_count = 0;
    static int64_t  last_us = 0;
    char buf[32];

    /* İki ayrı FPS:
     *   R-FPS  = Render FPS — yazılımın saniyede flush_cb sayısı
     *   DR-FPS = Display Rate FPS — panel donanımının VSYNC sayısı (sabit ~28-31)  */
    static int rfps_smooth = 0, drfps_smooth = 0;
    static uint32_t last_vsync = 0;
    uint32_t now_flush = lvgl_port_get_flush_count();
    uint32_t now_vsync = lvgl_port_get_vsync_count();
    int64_t  now_us    = esp_timer_get_time();
    if (last_us == 0) {
        last_us = now_us;
        last_count = now_flush;
        last_vsync = now_vsync;
    }
    int64_t elapsed = now_us - last_us;
    if (elapsed >= 200000) {
        int rfps_inst  = (int)((now_flush - last_count) * 1000000LL / elapsed);
        int drfps_inst = (int)((now_vsync - last_vsync) * 1000000LL / elapsed);
        rfps_smooth  = (rfps_smooth  * 2 + rfps_inst)  / 3;
        drfps_smooth = (drfps_smooth * 2 + drfps_inst) / 3;
        last_count = now_flush;
        last_vsync = now_vsync;
        last_us    = now_us;
    }
    char fbuf[64];
    snprintf(fbuf, sizeof(fbuf), "R-FPS: %d  DR-FPS: %d  C0: %d%%  C1: %d%%",
             rfps_smooth, drfps_smooth,
             cpu_meter_get_pct(0), cpu_meter_get_pct(1));
    lv_label_set_text(lbl_fps, fbuf);

    state_lock();
    cluster_state_t s = g_state;
    state_unlock();

    /* Snapshot guard — state hiç değişmemişse erken çık (cruise frame'leri).
     * memcmp tüm struct'ı karşılaştırır, FPS sayaçları hariç tüm UI işine girilmez. */
    static cluster_state_t prev_snapshot = {0};
    if (memcmp(&s, &prev_snapshot, sizeof(s)) == 0) return;
    prev_snapshot = s;

    if (s.speed < 0)   s.speed = 0;
    if (s.speed > 999) s.speed = 999;
    if (s.rpm < 0)     s.rpm = 0;
    if (s.rpm > 9999)  s.rpm = 9999;

    /* Direct set — animasyon yok, sadece değer değişince güncelle */
    static int      prev_speed = -1, prev_rpm = -1, prev_fuel = -1, prev_temp = -200;
    static uint32_t prev_total_km = (uint32_t)-1;

    if (s.speed != prev_speed) { apply_speed(s.speed); prev_speed = s.speed; }
    if (s.rpm   != prev_rpm)   { apply_rpm(s.rpm);     prev_rpm   = s.rpm; }

    /* RPM redline meter içinde sabit gösterildi (7-9 bandı kırmızı arc) */

    if (s.fuel != prev_fuel) {
        lv_bar_set_value(bar_fuel, s.fuel, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%d%%", s.fuel); lv_label_set_text(lbl_fuel_pct, buf);
        lv_obj_set_style_bg_color(bar_fuel,
            s.fuel < 15 ? C_RED : (s.fuel < 30 ? C_AMBER : C_GREEN), LV_PART_INDICATOR);
        prev_fuel = s.fuel;
    }

    if (s.temp != prev_temp) {
        lv_bar_set_value(bar_temp, s.temp, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%d%s", unit_conv_temp_c(s.temp),
                 unit_get() == UNIT_IMPERIAL ? "F" : "C");
        lv_label_set_text(lbl_temp_val, buf);
        /* Coolant warn eşiği user-adjustable (B7) — limits.coolant_warn_c */
        int warn_c = limits_get()->coolant_warn_c;
        lv_obj_set_style_bg_color(bar_temp,
            s.temp > warn_c ? C_RED : (s.temp < 50 ? C_BLUE : C_GREEN), LV_PART_INDICATOR);
        prev_temp = s.temp;
    }

    snprintf(buf, sizeof(buf), "%c", s.gear); lv_label_set_text(lbl_gear, buf);
    lv_color_t gc = C_GREEN;
    if (s.gear == 'P' || s.gear == 'N') gc = C_BLUE;
    else if (s.gear == 'R')             gc = C_RED;
    lv_obj_set_style_text_color(lbl_gear, gc, 0);

    if (s.total_km != prev_total_km) {
        snprintf(buf, sizeof(buf), "%lu",
                 (unsigned long)unit_conv_dist_km(s.total_km));
        lv_label_set_text(lbl_km, buf);
        prev_total_km = s.total_km;
    }

    /* Trip computer — değişen alan başına lazy update */
    static uint32_t prev_trip_m = (uint32_t)-1, prev_trip_sec = (uint32_t)-1;
    static int      prev_inst_x10 = -1, prev_range = -1;

    if (s.trip_m != prev_trip_m) {
        /* trip_m metre cinsinden — önce km, sonra unit'e göre çevir */
        uint32_t km     = s.trip_m / 1000;
        uint32_t conv_m = unit_conv_dist_km(s.trip_m) / 1;  /* metre proxy */
        (void)conv_m;
        if (unit_get() == UNIT_IMPERIAL) {
            uint32_t mi100 = (s.trip_m * 621u) / 10000u;    /* mi×10 */
            snprintf(buf, sizeof(buf), "%lu.%01lu %s",
                     (unsigned long)(mi100 / 10),
                     (unsigned long)(mi100 % 10),
                     unit_dist_label());
        } else {
            snprintf(buf, sizeof(buf), "%lu.%01lu %s",
                     (unsigned long)km,
                     (unsigned long)((s.trip_m % 1000) / 100),
                     unit_dist_label());
        }
        lv_label_set_text(lbl_trip_km, buf);
        prev_trip_m = s.trip_m;
    }
    if (s.trip_seconds != prev_trip_sec) {
        unsigned mm = s.trip_seconds / 60;
        unsigned ss = s.trip_seconds % 60;
        snprintf(buf, sizeof(buf), "%02u:%02u", mm, ss);
        lv_label_set_text(lbl_trip_time, buf);
        prev_trip_sec = s.trip_seconds;
    }
    if (s.inst_l100_x10 != prev_inst_x10) {
        int x10 = unit_conv_l100_x10(s.inst_l100_x10);
        snprintf(buf, sizeof(buf), "%d.%d", x10 / 10, x10 % 10);
        lv_label_set_text(lbl_trip_l100, buf);
        prev_inst_x10 = s.inst_l100_x10;
    }
    if (s.range_km != prev_range) {
        if (s.range_km > 0)
            snprintf(buf, sizeof(buf), "%d %s",
                     unit_conv_speed(s.range_km), /* km→mi same factor */
                     unit_dist_label());
        else
            snprintf(buf, sizeof(buf), "-- %s", unit_dist_label());
        lv_label_set_text(lbl_trip_range, buf);
        prev_range = s.range_km;
    }

    /* Telltale modları:
     *   BLINK : turn signals (sinyal/dörtlü)
     *   PULSE : kritik kırmızılar (brake, oil, coolant, engine MIL)
     *   ON    : diğer aktif uyarılar
     *   icon_set_mode kontainer'a sadece bir alan yazar — animasyonu icons.c
     *   periyodik timer'ı yapıyor (sync'd, tek faz). Boot check (ilk 2 sn ALL ON)
     *   icons.c içinde otomatik. */
    bool fl = s.fuel_low_warn || (s.fuel < 15);
    icon_set_mode(ic_turn_l,   s.left_blink    ? ICON_MODE_BLINK : ICON_MODE_OFF);
    icon_set_mode(ic_turn_r,   s.right_blink   ? ICON_MODE_BLINK : ICON_MODE_OFF);
    icon_set_mode(ic_high,     s.high_beam     ? ICON_MODE_ON    : ICON_MODE_OFF);
    icon_set_mode(ic_low,      s.low_beam      ? ICON_MODE_ON    : ICON_MODE_OFF);
    icon_set_mode(ic_brake,    s.brake_warn    ? ICON_MODE_PULSE : ICON_MODE_OFF);
    icon_set_mode(ic_abs,      s.abs_warn      ? ICON_MODE_ON    : ICON_MODE_OFF);
    icon_set_mode(ic_airbag,   s.airbag_warn   ? ICON_MODE_ON    : ICON_MODE_OFF);
    icon_set_mode(ic_seat,     s.seatbelt_warn ? ICON_MODE_ON    : ICON_MODE_OFF);
    icon_set_mode(ic_engine,   s.engine_warn   ? ICON_MODE_PULSE : ICON_MODE_OFF);
    icon_set_mode(ic_battery,  s.battery_warn  ? ICON_MODE_ON    : ICON_MODE_OFF);
    icon_set_mode(ic_oil,      s.oil_warn      ? ICON_MODE_PULSE : ICON_MODE_OFF);
    icon_set_mode(ic_coolant,  s.coolant_warn  ? ICON_MODE_PULSE : ICON_MODE_OFF);
    icon_set_mode(ic_fuel_low, fl              ? ICON_MODE_ON    : ICON_MODE_OFF);
}

void ui_set_ip(const char *ip)
{
    char buf[40];
    snprintf(buf, sizeof(buf), "WiFi: %s :23", ip);
    lv_label_set_text(lbl_ip, buf);
}

/* ============================================================
 * Settings modal — long-press anywhere on screen → settings
 * ============================================================ */

static lv_obj_t *s_settings_modal   = NULL;
static lv_obj_t *s_settings_tabview = NULL;   /* cmd_listener tab switching için */
static lv_obj_t *s_btn_pause_lbl  = NULL;
static lv_obj_t *s_theme_btns[THEME_COUNT] = {0};
static lv_obj_t *s_theme_hint_lbl = NULL;
static lv_obj_t *s_unit_btns[2]   = {0};   /* 0=Metric 1=Imperial */
static lv_obj_t *s_unit_hint_lbl  = NULL;
/* Limits sekmesi state */
static lv_obj_t *s_lim_rpm_slider     = NULL;
static lv_obj_t *s_lim_rpm_lbl        = NULL;
static lv_obj_t *s_lim_coolant_slider = NULL;
static lv_obj_t *s_lim_coolant_lbl    = NULL;
static lv_obj_t *s_lim_hint_lbl       = NULL;

static void modal_close(lv_event_t *e)
{
    (void)e;
    if (s_settings_modal) {
        lv_obj_del(s_settings_modal);
        s_settings_modal = NULL;
        /* Tüm modal-içi pointer'ları temizle (LVGL widget'ları parent'la ölür) */
        s_settings_tabview = NULL;
        for (int i = 0; i < THEME_COUNT; i++) s_theme_btns[i] = NULL;
        for (int i = 0; i < 2; i++) s_unit_btns[i] = NULL;
        s_theme_hint_lbl     = NULL;
        s_btn_pause_lbl      = NULL;
        s_unit_hint_lbl      = NULL;
        s_lim_rpm_slider     = NULL;
        s_lim_rpm_lbl        = NULL;
        s_lim_coolant_slider = NULL;
        s_lim_coolant_lbl    = NULL;
        s_lim_hint_lbl       = NULL;
    }
}

static void on_trip_reset_a(lv_event_t *e)
{
    trip_reset_a();
    modal_close(e);
}

static void on_trip_reset_b(lv_event_t *e)
{
    trip_reset_b();
    modal_close(e);
}

static void on_demo_toggle(lv_event_t *e)
{
    (void)e;
    g_demo_paused = !g_demo_paused;
    if (s_btn_pause_lbl) {
        lv_label_set_text(s_btn_pause_lbl, g_demo_paused ? "Resume Demo" : "Pause Demo");
    }
}

/* Theme picker — tıklanan butonun id'si user_data'da. Seçim NVS'e yazılır;
 * uygulama reboot'ta yeni paleti yükler. "Restart to apply" label'ı görünür. */
static void update_theme_btn_styles(theme_id_t selected)
{
    for (int i = 0; i < THEME_COUNT; i++) {
        if (!s_theme_btns[i]) continue;
        bool active = (i == (int)selected);
        lv_obj_set_style_bg_color(s_theme_btns[i],
                                  active ? C_ACCENT : C_RING, 0);
        lv_obj_set_style_border_width(s_theme_btns[i], active ? 2 : 0, 0);
        lv_obj_set_style_border_color(s_theme_btns[i], C_FG, 0);
    }
}

static void on_theme_pick(lv_event_t *e)
{
    theme_id_t id = (theme_id_t)(intptr_t)lv_event_get_user_data(e);
    persist_save_theme((uint8_t)id);
    update_theme_btn_styles(id);
    if (s_theme_hint_lbl) {
        lv_label_set_text(s_theme_hint_lbl, "Restart to apply theme");
        lv_obj_set_style_text_color(s_theme_hint_lbl, C_AMBER, 0);
    }
}

/* Unit picker (B2) — Metric/Imperial. Display label'ları reboot'ta yeniden
 * yerleşir (gauge "km/h" → "mph", trip "km" → "mi" vs.). */
static void update_unit_btn_styles(unit_t selected)
{
    for (int i = 0; i < 2; i++) {
        if (!s_unit_btns[i]) continue;
        bool active = (i == (int)selected);
        lv_obj_set_style_bg_color(s_unit_btns[i],
                                  active ? C_ACCENT : C_RING, 0);
        lv_obj_set_style_border_width(s_unit_btns[i], active ? 2 : 0, 0);
        lv_obj_set_style_border_color(s_unit_btns[i], C_FG, 0);
    }
}

static void on_unit_pick(lv_event_t *e)
{
    unit_t u = (unit_t)(intptr_t)lv_event_get_user_data(e);
    unit_set(u);
    update_unit_btn_styles(u);
    if (s_unit_hint_lbl) {
        lv_label_set_text(s_unit_hint_lbl, "Restart to apply units");
        lv_obj_set_style_text_color(s_unit_hint_lbl, C_AMBER, 0);
    }
}

/* Limits slider'ları (B7) — RPM redline (5000-9000) + coolant warn (90-120).
 * RPM redline gauge band'ını etkiler → reboot gerekir.
 * Coolant warn live trigger threshold → reboot gerekmez. */
static void on_lim_rpm_changed(lv_event_t *e)
{
    if (!s_lim_rpm_slider || !s_lim_rpm_lbl) return;
    int v = lv_slider_get_value(s_lim_rpm_slider);
    /* 100 RPM step için yuvarla — slider 50..90 (×100) range'inde */
    int rpm = v * 100;
    char buf[32];
    snprintf(buf, sizeof(buf), "RPM redline: %d", rpm);
    lv_label_set_text(s_lim_rpm_lbl, buf);
}

static void on_lim_coolant_changed(lv_event_t *e)
{
    if (!s_lim_coolant_slider || !s_lim_coolant_lbl) return;
    int v = lv_slider_get_value(s_lim_coolant_slider);
    char buf[32];
    snprintf(buf, sizeof(buf), "Coolant warn: %d°C", v);
    lv_label_set_text(s_lim_coolant_lbl, buf);
}

static void on_lim_save(lv_event_t *e)
{
    if (!s_lim_rpm_slider || !s_lim_coolant_slider) return;
    limits_t l = {
        .rpm_redline    = lv_slider_get_value(s_lim_rpm_slider) * 100,
        .coolant_warn_c = lv_slider_get_value(s_lim_coolant_slider),
    };
    limits_set(&l);
    if (s_lim_hint_lbl) {
        lv_label_set_text(s_lim_hint_lbl, "Saved — restart for redline");
        lv_obj_set_style_text_color(s_lim_hint_lbl, C_AMBER, 0);
    }
}

/* Tab content build helpers — her sekme kendi içinde widget'larını yerleştirir.
 * Tabview tab content alan'ı flex-friendly bir lv_obj — manuel align kullanıyoruz. */

static lv_obj_t *make_btn(lv_obj_t *parent, const char *text, lv_color_t bg,
                          lv_color_t fg, int w, int h,
                          lv_align_t a, int x, int y,
                          lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_align(b, a, x, y);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, fg, 0);
    lv_obj_set_style_text_font(l, &lv_font_inter_18, 0);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user_data);
    return b;
}

static void build_tab_trip(lv_obj_t *t)
{
    lv_obj_set_style_pad_all(t, 8, 0);

    make_btn(t, "Reset Trip A", C_ACCENT, lv_color_hex(0x000000),
             280, 56, LV_ALIGN_TOP_MID, 0, 12, on_trip_reset_a, NULL);
    make_btn(t, "Reset Trip B", C_BLUE, lv_color_hex(0x000000),
             280, 56, LV_ALIGN_TOP_MID, 0, 80, on_trip_reset_b, NULL);

    lv_obj_t *btn_pause = lv_btn_create(t);
    lv_obj_set_size(btn_pause, 280, 56);
    lv_obj_align(btn_pause, LV_ALIGN_TOP_MID, 0, 148);
    lv_obj_set_style_bg_color(btn_pause, C_AMBER, 0);
    lv_obj_set_style_radius(btn_pause, 8, 0);
    s_btn_pause_lbl = lv_label_create(btn_pause);
    lv_label_set_text(s_btn_pause_lbl,
                      g_demo_paused ? "Resume Demo" : "Pause Demo");
    lv_obj_set_style_text_color(s_btn_pause_lbl, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(s_btn_pause_lbl, &lv_font_inter_18, 0);
    lv_obj_center(s_btn_pause_lbl);
    lv_obj_add_event_cb(btn_pause, on_demo_toggle, LV_EVENT_CLICKED, NULL);
}

static void build_tab_display(lv_obj_t *t)
{
    lv_obj_set_style_pad_all(t, 8, 0);

    /* Theme satırı */
    lv_obj_t *theme_lbl = lv_label_create(t);
    lv_label_set_text(theme_lbl, "THEME");
    lv_obj_set_style_text_color(theme_lbl, C_DIM, 0);
    lv_obj_set_style_text_font(theme_lbl, &lv_font_inter_14, 0);
    lv_obj_align(theme_lbl, LV_ALIGN_TOP_MID, 0, 8);

    const char *names[THEME_COUNT] = {"Blue", "Orange", "Yellow", "Red"};
    int btn_w = 110, btn_h = 40, gap = 10;
    int row_w = THEME_COUNT * btn_w + (THEME_COUNT - 1) * gap;
    int x0 = -row_w / 2 + btn_w / 2;
    for (int i = 0; i < THEME_COUNT; i++) {
        lv_obj_t *b = lv_btn_create(t);
        lv_obj_set_size(b, btn_w, btn_h);
        lv_obj_align(b, LV_ALIGN_TOP_MID, x0 + i * (btn_w + gap), 32);
        lv_obj_set_style_radius(b, 6, 0);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, names[i]);
        lv_obj_set_style_text_color(l, C_FG, 0);
        lv_obj_set_style_text_font(l, &lv_font_inter_14, 0);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, on_theme_pick, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        s_theme_btns[i] = b;
    }
    update_theme_btn_styles(theme_active());

    s_theme_hint_lbl = lv_label_create(t);
    lv_label_set_text(s_theme_hint_lbl, "");
    lv_obj_set_style_text_font(s_theme_hint_lbl, &lv_font_inter_14, 0);
    lv_obj_set_style_text_color(s_theme_hint_lbl, C_DIM, 0);
    lv_obj_align(s_theme_hint_lbl, LV_ALIGN_TOP_MID, 0, 78);

    /* Units satırı */
    lv_obj_t *unit_lbl = lv_label_create(t);
    lv_label_set_text(unit_lbl, "UNITS");
    lv_obj_set_style_text_color(unit_lbl, C_DIM, 0);
    lv_obj_set_style_text_font(unit_lbl, &lv_font_inter_14, 0);
    lv_obj_align(unit_lbl, LV_ALIGN_TOP_MID, 0, 110);

    const char *unames[2] = {"Metric", "Imperial"};
    int u_btn_w = 140, u_gap = 14;
    int u_row_w = 2 * u_btn_w + u_gap;
    int u_x0 = -u_row_w / 2 + u_btn_w / 2;
    for (int i = 0; i < 2; i++) {
        lv_obj_t *b = lv_btn_create(t);
        lv_obj_set_size(b, u_btn_w, btn_h);
        lv_obj_align(b, LV_ALIGN_TOP_MID, u_x0 + i * (u_btn_w + u_gap), 134);
        lv_obj_set_style_radius(b, 6, 0);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, unames[i]);
        lv_obj_set_style_text_color(l, C_FG, 0);
        lv_obj_set_style_text_font(l, &lv_font_inter_14, 0);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, on_unit_pick, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        s_unit_btns[i] = b;
    }
    update_unit_btn_styles(unit_get());

    s_unit_hint_lbl = lv_label_create(t);
    lv_label_set_text(s_unit_hint_lbl, "");
    lv_obj_set_style_text_font(s_unit_hint_lbl, &lv_font_inter_14, 0);
    lv_obj_set_style_text_color(s_unit_hint_lbl, C_DIM, 0);
    lv_obj_align(s_unit_hint_lbl, LV_ALIGN_TOP_MID, 0, 184);
}

static void build_tab_limits(lv_obj_t *t)
{
    lv_obj_set_style_pad_all(t, 8, 0);
    const limits_t *lim = limits_get();

    /* RPM redline slider */
    s_lim_rpm_lbl = lv_label_create(t);
    char buf[40];
    snprintf(buf, sizeof(buf), "RPM redline: %d", lim->rpm_redline);
    lv_label_set_text(s_lim_rpm_lbl, buf);
    lv_obj_set_style_text_color(s_lim_rpm_lbl, C_FG, 0);
    lv_obj_set_style_text_font(s_lim_rpm_lbl, &lv_font_inter_18, 0);
    lv_obj_align(s_lim_rpm_lbl, LV_ALIGN_TOP_MID, 0, 12);

    s_lim_rpm_slider = lv_slider_create(t);
    lv_obj_set_size(s_lim_rpm_slider, 480, 18);
    lv_obj_align(s_lim_rpm_slider, LV_ALIGN_TOP_MID, 0, 46);
    lv_slider_set_range(s_lim_rpm_slider,
                        LIMITS_RPM_MIN / 100, LIMITS_RPM_MAX / 100);
    lv_slider_set_value(s_lim_rpm_slider, lim->rpm_redline / 100, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_lim_rpm_slider, on_lim_rpm_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* Coolant warn slider */
    s_lim_coolant_lbl = lv_label_create(t);
    snprintf(buf, sizeof(buf), "Coolant warn: %d°C", lim->coolant_warn_c);
    lv_label_set_text(s_lim_coolant_lbl, buf);
    lv_obj_set_style_text_color(s_lim_coolant_lbl, C_FG, 0);
    lv_obj_set_style_text_font(s_lim_coolant_lbl, &lv_font_inter_18, 0);
    lv_obj_align(s_lim_coolant_lbl, LV_ALIGN_TOP_MID, 0, 84);

    s_lim_coolant_slider = lv_slider_create(t);
    lv_obj_set_size(s_lim_coolant_slider, 480, 18);
    lv_obj_align(s_lim_coolant_slider, LV_ALIGN_TOP_MID, 0, 118);
    lv_slider_set_range(s_lim_coolant_slider,
                        LIMITS_TEMP_MIN, LIMITS_TEMP_MAX);
    lv_slider_set_value(s_lim_coolant_slider, lim->coolant_warn_c, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_lim_coolant_slider, on_lim_coolant_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* Save button */
    make_btn(t, "Save Limits", C_ACCENT, lv_color_hex(0x000000),
             200, 50, LV_ALIGN_TOP_MID, 0, 158, on_lim_save, NULL);

    s_lim_hint_lbl = lv_label_create(t);
    lv_label_set_text(s_lim_hint_lbl, "");
    lv_obj_set_style_text_font(s_lim_hint_lbl, &lv_font_inter_14, 0);
    lv_obj_set_style_text_color(s_lim_hint_lbl, C_DIM, 0);
    lv_obj_align(s_lim_hint_lbl, LV_ALIGN_TOP_MID, 0, 215);
}

static void build_tab_diag(lv_obj_t *t)
{
    lv_obj_set_style_pad_all(t, 8, 0);

    /* Sistem bilgileri (heap, PSRAM, lifetime stats) — read-only multi-line */
    const esp_app_desc_t *app = esp_app_get_description();
    uint32_t pwr = persist_get_reset_counter(ESP_RST_POWERON);
    uint32_t pnc = persist_get_reset_counter(ESP_RST_PANIC);
    uint32_t wdt = persist_get_reset_counter(ESP_RST_TASK_WDT);
    uint32_t bro = persist_get_reset_counter(ESP_RST_BROWNOUT);

    size_t free_int  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t free_psr  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t min_int   = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);

    trip_b_view_t b;        trip_get_b(&b);
    trip_stats_t  s;        trip_get_stats(&s);

    /* Imperial mod'da display unit'leri kullan */
    int max_spd_disp = unit_conv_speed(s.max_speed_kmh);

    char text[640];
    int p = 0;
    p += snprintf(text + p, sizeof(text) - p,
        "carcluster %s  (%s)\n", app->version, app->date);
    p += snprintf(text + p, sizeof(text) - p,
        "boots %lu  panic %lu  wdt %lu  brownout %lu\n",
        (unsigned long)pwr, (unsigned long)pnc,
        (unsigned long)wdt, (unsigned long)bro);
    p += snprintf(text + p, sizeof(text) - p,
        "heap free: %lu KB (min %lu KB)   PSRAM: %lu KB\n",
        (unsigned long)(free_int / 1024),
        (unsigned long)(min_int / 1024),
        (unsigned long)(free_psr / 1024));
    p += snprintf(text + p, sizeof(text) - p,
        "uptime: %llu s\n", esp_timer_get_time() / 1000000ULL);
    p += snprintf(text + p, sizeof(text) - p, "\n--- TRIP B ---\n");
    p += snprintf(text + p, sizeof(text) - p,
        "%lu.%01lu %s  •  %lum %lus\n",
        (unsigned long)(b.trip_b_m / 1000),
        (unsigned long)((b.trip_b_m % 1000) / 100),
        unit_dist_label(),
        (unsigned long)(b.trip_b_seconds / 60),
        (unsigned long)(b.trip_b_seconds % 60));
    p += snprintf(text + p, sizeof(text) - p, "\n--- LIFETIME ---\n");
    p += snprintf(text + p, sizeof(text) - p,
        "max speed: %d %s\n", max_spd_disp, unit_speed_label());
    p += snprintf(text + p, sizeof(text) - p,
        "longest trip: %lu.%01lu %s\n",
        (unsigned long)(s.longest_trip_m / 1000),
        (unsigned long)((s.longest_trip_m % 1000) / 100),
        unit_dist_label());
    p += snprintf(text + p, sizeof(text) - p,
        "total fuel: %lu.%01lu L\n",
        (unsigned long)(s.total_fuel_ml / 1000),
        (unsigned long)((s.total_fuel_ml % 1000) / 100));
    p += snprintf(text + p, sizeof(text) - p,
        "total runtime: %lu min",
        (unsigned long)(s.total_seconds / 60));

    lv_obj_t *lbl = lv_label_create(t);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, C_FG, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_inter_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 6, 6);

    /* PANIC veya WDT > 0 ise üst başlığı amber renkle vurgula */
    if (pnc > 0 || wdt > 0) {
        lv_obj_t *warn = lv_label_create(t);
        lv_label_set_text(warn, LV_SYMBOL_WARNING " faults present");
        lv_obj_set_style_text_color(warn, C_AMBER, 0);
        lv_obj_set_style_text_font(warn, &lv_font_inter_14, 0);
        lv_obj_align(warn, LV_ALIGN_TOP_RIGHT, -6, 6);
    }

    /* Screenshot butonu kaldırıldı — host'tan cmd_listener üzerinden
     * "SHOT MAIN / SHOT MODAL <tab>" komutları ile otomatik tetiklenir.
     * Bkz. tools/auto_screenshots.py */
}

static void show_settings_modal(void)
{
    if (s_settings_modal) return;

    /* HIDDEN flag pattern — child build sırasında render yok, tek shot reveal */
    s_settings_modal = lv_obj_create(lv_scr_act());
    lv_obj_add_flag(s_settings_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_style_all(s_settings_modal);
    lv_obj_set_size(s_settings_modal, LCD_H_RES, LCD_V_RES);
    lv_obj_set_pos(s_settings_modal, 0, 0);
    lv_obj_set_style_bg_color(s_settings_modal, lv_color_hex(0x05080f), 0);
    lv_obj_set_style_bg_opa(s_settings_modal, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_settings_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_settings_modal, LV_OBJ_FLAG_CLICKABLE);

    /* Centered panel hosts tabview + close button */
    lv_obj_t *panel = lv_obj_create(s_settings_modal);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 740, 440);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, C_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, C_RING, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    /* Tabview: 4 sekme — Trip / Display / Limits / Diag */
    lv_obj_t *tv = lv_tabview_create(panel, LV_DIR_TOP, 44);
    s_settings_tabview = tv;     /* cmd_listener'ın tab switching için */
    lv_obj_set_size(tv, 740, 360);
    lv_obj_set_pos(tv, 0, 0);
    lv_obj_set_style_bg_color(tv, C_PANEL, 0);
    lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tv);
    lv_obj_set_style_text_color(tab_btns, C_FG, 0);
    lv_obj_set_style_text_font(tab_btns, &lv_font_inter_18, 0);
    lv_obj_set_style_bg_color(tab_btns, C_RING, 0);

    build_tab_trip   (lv_tabview_add_tab(tv, "Trip"));
    build_tab_display(lv_tabview_add_tab(tv, "Display"));
    build_tab_limits (lv_tabview_add_tab(tv, "Limits"));
    build_tab_diag   (lv_tabview_add_tab(tv, "Diag"));

    /* Close button — tabview'in altında */
    make_btn(panel, "Close", C_DIM, C_FG, 200, 50,
             LV_ALIGN_BOTTOM_MID, 0, -12, modal_close, NULL);

    /* Single invalidation reveal */
    lv_obj_clear_flag(s_settings_modal, LV_OBJ_FLAG_HIDDEN);
}

static void on_screen_long_press(lv_event_t *e)
{
    (void)e;
    show_settings_modal();
}

static void ui_attach_long_press_handler(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, on_screen_long_press, LV_EVENT_LONG_PRESSED, NULL);
}

/* External API — cmd_listener (auto-screenshot) için. Background task'lardan
 * çağrılır, lvgl_port_lock dahili olarak alınır → re-entrancy yok. */
void ui_cmd_show_modal(int tab_id)
{
    lvgl_port_lock();
    if (!s_settings_modal) show_settings_modal();
    if (s_settings_tabview && tab_id >= 0 && tab_id < 4) {
        lv_tabview_set_act(s_settings_tabview, (uint16_t)tab_id, LV_ANIM_OFF);
    }
    lvgl_port_unlock();
}

void ui_cmd_close_modal(void)
{
    lvgl_port_lock();
    if (s_settings_modal) modal_close(NULL);
    lvgl_port_unlock();
}

