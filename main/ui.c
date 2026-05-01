#include "ui.h"
#include "state.h"
#include "icons.h"
#include "lvgl_port.h"
#include "board.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define C_BG       lv_color_hex(0x070b14)
#define C_PANEL    lv_color_hex(0x0d1424)
#define C_FG       lv_color_hex(0xeef4fb)
#define C_DIM      lv_color_hex(0x4a5568)
#define C_RED      lv_color_hex(0xff2030)
#define C_GREEN    lv_color_hex(0x22c55e)
#define C_AMBER    lv_color_hex(0xffaa00)
#define C_BLUE     lv_color_hex(0x4488ff)
#define C_ACCENT   lv_color_hex(0x00d4ff)
#define C_RING     lv_color_hex(0x1c2436)

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

/* Label durum renkleri — cool navy temasıyla uyumlu, ikisi clearly birbirinden farklı.
 *   DIM    : steel-blue gray, band üzerinde "soluk" (still readable, not screaming)
 *   BRIGHT : pure white, "lit/active" — herhangi bir band rengi (yeşil→kırmızı, cyan)
 *            üzerinde max contrast */
#define C_LBL_DIM     lv_color_hex(0x6b7c91)
#define C_LBL_BRIGHT  lv_color_hex(0xffffff)

/* Anim kaldırıldı — VSYNC periyot (33ms) ile anim duration (35ms) çakışması
 * nedeniyle anim hiç tamamlanmıyordu, sürekli restart oluyordu.
 * Direkt set ile aynı görsel sonuç, daha az CPU. */

static void apply_speed(int v)
{
    int spd = v > 240 ? 240 : (v < 0 ? 0 : v);
    lv_meter_set_indicator_end_value(meter_speed, ind_speed_arc, spd);
    lv_meter_set_indicator_value(meter_speed, ind_speed_needle, spd);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", v);
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
    lv_meter_set_indicator_value(meter_rpm, ind_rpm_needle, rpm_x10);

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

/* Tick label rengi — parlak, koyu backplate üzerinde kontrastlı */
#define C_TICK_LBL  lv_color_hex(0xe6edf5)
/* Backplate — panel arkaplanından (C_BG=0x070b14) belirgin ayrılır */
#define C_BACKPLATE lv_color_hex(0x1f2a3a)

/* Geometri (lv_draw_arc'ta `radius` arc'ın OUTER yarıçapı, inner = radius - width):
 *   Outer rim 134-140  : minor + major tick çizgileri
 *   r_mod=-30, w=40    : outer=110, inner=70, center=90 — backplate / color bands / arc fill
 *
 * Custom label'lar radius=90'da (bandın gerçek merkezi). Auto-label'lar gizli (text_opa=0).
 *
 * RPM: 5 color band her zaman tam görünür. Speed: arc fill 0→value büyür.
 *
 * Reveal: needle bir sayıyı geçince C_LBL_DIM → C_LBL_BRIGHT. */
static lv_obj_t *make_meter(int cx, int cy, int size,
                            int v_min, int v_max,
                            int n_majors, int n_minor_per_major,
                            lv_color_t arc_col,
                            int smooth_factor,
                            bool with_rpm_bands,
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

    /* Auto label'ları gizle — bunun yerine custom lv_label widget'ları kullanılacak.
     * text_opa LV_PART_TICKS sadece label rendering'i etkiler, tick LİNE'larına dokunmaz. */
    lv_obj_set_style_text_opa(m, LV_OPA_TRANSP, LV_PART_TICKS);
    lv_obj_set_style_text_font(m, &lv_font_montserrat_18, LV_PART_TICKS);

    /* Görünür ölçek: tick + label (her zaman v_min..v_max aralığı, label için) */
    lv_meter_scale_t *sc_visual = lv_meter_add_scale(m);
    int total_minor = (n_majors - 1) * n_minor_per_major + 1;
    lv_meter_set_scale_ticks(m, sc_visual, total_minor, 2, 6, C_DIM);
    lv_meter_set_scale_major_ticks(m, sc_visual, n_minor_per_major, 4, 6, C_TICK_LBL, 24);
    lv_meter_set_scale_range(m, sc_visual, v_min, v_max, 270, 135);

    /* Smooth ölçek (smooth_factor>1) — needle/bands burada hareket eder */
    lv_meter_scale_t *sc_smooth = sc_visual;
    int smin = v_min, smax = v_max;
    if (smooth_factor > 1) {
        sc_smooth = lv_meter_add_scale(m);
        lv_meter_set_scale_ticks(m, sc_smooth, 0, 0, 0, C_DIM);  /* görünmez */
        smin = v_min * smooth_factor;
        smax = v_max * smooth_factor;
        lv_meter_set_scale_range(m, sc_smooth, smin, smax, 270, 135);
    }

    /* 1) Label backplate — center=110, w=40 → spans 90-130. */
    lv_meter_indicator_t *bp = lv_meter_add_arc(m, sc_smooth, 40, C_BACKPLATE, -30);
    lv_meter_set_indicator_start_value(m, bp, smin);
    lv_meter_set_indicator_end_value(m, bp, smax);

    if (with_rpm_bands) {
        /* 2a) RPM: 5 sabit color band — full range, her zaman tam görünür */
        struct { int from, to; uint32_t color; } bands[] = {
            { 0,  20, 0x22c55e },  /* yeşil — idle/eco */
            { 20, 40, 0x84cc16 },  /* lime — normal */
            { 40, 50, 0xeab308 },  /* sarı — orta yük */
            { 50, 70, 0xf97316 },  /* turuncu — yüksek */
            { 70, 90, 0xff2030 },  /* kırmızı — redline */
        };
        int n = sizeof(bands) / sizeof(bands[0]);
        for (int i = 0; i < n; i++) {
            lv_meter_indicator_t *band = lv_meter_add_arc(m, sc_smooth, 40,
                                                         lv_color_hex(bands[i].color), -30);
            lv_meter_set_indicator_start_value(m, band, bands[i].from);
            lv_meter_set_indicator_end_value(m, band, bands[i].to);
        }
        if (ret_arc_ind) *ret_arc_ind = NULL;  /* RPM'de dinamik arc yok */
    } else {
        /* 2b) Speed arc fill — center=110, w=40; start=0, end=value */
        lv_meter_indicator_t *arc = lv_meter_add_arc(m, sc_smooth, 40, arc_col, -30);
        lv_meter_set_indicator_start_value(m, arc, smin);
        lv_meter_set_indicator_end_value(m, arc, smin);
        if (ret_arc_ind) *ret_arc_ind = arc;
    }

    /* 3) İğne — en son eklenir, en üstte çizilir */
    *ret_needle = lv_meter_add_needle_line(m, sc_smooth, 4, lv_color_hex(0xffffff), -10);
    lv_meter_set_indicator_value(m, *ret_needle, smin);

    return m;
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
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(lbl, C_LBL_DIM, 0);

        lv_point_t txt_size;
        lv_txt_get_size(&txt_size, lv_label_get_text(lbl),
                        &lv_font_montserrat_18, 0, 0, LV_COORD_MAX, 0);
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
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(l, 12, y + 3);    /* küçük font, 3px alt offset için baseline align */

    lv_obj_t *v = lv_label_create(parent);
    lv_label_set_text(v, init_value);
    lv_obj_set_style_text_color(v, C_FG, 0);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_18, 0);
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
    lv_obj_set_style_text_font(u1, &lv_font_montserrat_18, 0);
    lv_label_set_text(u1, "RPM x 1000");
    lv_obj_align_to(u1, meter_rpm, LV_ALIGN_CENTER, 0, 50);

    /* Sağ: Hız kadranı (0-240, smooth_factor=1, range zaten yeterince ince) */
    meter_speed = make_meter(RX, CY, ARC_R, 0, 240, 13, 4, C_ACCENT, 1, false,
                             &ind_speed_arc, &ind_speed_needle);
    /* Speed custom label'ları: 13 adet, 135°→405°, 22.5° step, value 0,20,...,240 */
    place_tick_labels(scr, spd_tick_labels, SPD_N_LABELS, RX, CY, 90,
                      135.0f, 22.5f, 20);

    /* km/h yazısı kadranın merkezinden biraz yukarıda */
    lv_obj_t *u2 = lv_label_create(scr);
    lv_obj_set_style_text_color(u2, C_DIM, 0);
    lv_obj_set_style_text_font(u2, &lv_font_montserrat_18, 0);
    lv_label_set_text(u2, "km/h");
    lv_obj_align_to(u2, meter_speed, LV_ALIGN_CENTER, 0, 50);

    /* Anlık hız sayısı km/h yazısının ALTINDA — iğne alanından bağımsız */
    lbl_speed_val = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_speed_val, C_FG, 0);
    lv_obj_set_style_text_font(lbl_speed_val, &lv_font_montserrat_36, 0);
    lv_label_set_text(lbl_speed_val, "0");
    lv_obj_align_to(lbl_speed_val, u2, LV_ALIGN_OUT_BOTTOM_MID, -10, 6);

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
    lv_obj_set_style_text_font(lbl_gear, &lv_font_montserrat_48, 0);
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

        /* 4 satır: TRIP / TIME / L/100 / RANGE — header yok */
        lbl_trip_km    = make_trip_row(tp,  8, TPW, "TRIP",  "0.0 km");
        lbl_trip_time  = make_trip_row(tp, 26, TPW, "TIME",  "00:00");
        lbl_trip_l100  = make_trip_row(tp, 44, TPW, "L/100", "0.0");
        lbl_trip_range = make_trip_row(tp, 62, TPW, "RANGE", "-- km");
    }

    /* Alt şerit */
    int by = 388;

    lv_obj_t *fl = lv_label_create(scr);
    lv_label_set_text(fl, "FUEL");
    lv_obj_set_style_text_color(fl, C_DIM, 0);
    lv_obj_set_style_text_font(fl, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(fl, 30, by);

    bar_fuel = lv_bar_create(scr);
    lv_obj_set_size(bar_fuel, 220, 16);
    lv_obj_set_pos(bar_fuel, 30, by + 22);
    lv_bar_set_range(bar_fuel, 0, 100);
    lv_obj_set_style_bg_color(bar_fuel, C_RING, 0);
    lv_obj_set_style_bg_color(bar_fuel, C_GREEN, LV_PART_INDICATOR);

    lbl_fuel_pct = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_fuel_pct, C_FG, 0);
    lv_obj_set_style_text_font(lbl_fuel_pct, &lv_font_montserrat_18, 0);
    lv_label_set_text(lbl_fuel_pct, "100%");
    lv_obj_set_pos(lbl_fuel_pct, 200, by);

    lv_obj_t *tp = lv_label_create(scr);
    lv_label_set_text(tp, "COOLANT");
    lv_obj_set_style_text_color(tp, C_DIM, 0);
    lv_obj_set_style_text_font(tp, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(tp, 550, by);

    bar_temp = lv_bar_create(scr);
    lv_obj_set_size(bar_temp, 220, 16);
    lv_obj_set_pos(bar_temp, 550, by + 22);
    lv_bar_set_range(bar_temp, -40, 150);
    lv_obj_set_style_bg_color(bar_temp, C_RING, 0);
    lv_obj_set_style_bg_color(bar_temp, C_AMBER, LV_PART_INDICATOR);

    lbl_temp_val = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_temp_val, C_FG, 0);
    lv_obj_set_style_text_font(lbl_temp_val, &lv_font_montserrat_18, 0);
    lv_label_set_text(lbl_temp_val, "0C");
    lv_obj_set_pos(lbl_temp_val, 720, by);

    /* ODO 3 parça — "ODO" sabit + sayı (sağ-hizalı) + "km" sabit pozisyonda.
     * Sadece sayı label dirty olur, "ODO" ve "km" sabit kalır → render iş yükü ↓ */
    lv_obj_t *odo_lbl = lv_label_create(scr);
    lv_label_set_text(odo_lbl, "ODO");
    lv_obj_set_style_text_color(odo_lbl, C_DIM, 0);
    lv_obj_set_style_text_font(odo_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(odo_lbl, LV_ALIGN_BOTTOM_MID, -64, -22);

    lbl_km = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_km, C_FG, 0);
    lv_obj_set_style_text_font(lbl_km, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(lbl_km, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(lbl_km, 100);
    lv_label_set_text(lbl_km, "0");
    lv_obj_align(lbl_km, LV_ALIGN_BOTTOM_MID, -10, -20);

    lv_obj_t *odo_unit = lv_label_create(scr);
    lv_label_set_text(odo_unit, "km");
    lv_obj_set_style_text_color(odo_unit, C_DIM, 0);
    lv_obj_set_style_text_font(odo_unit, &lv_font_montserrat_18, 0);
    lv_obj_align(odo_unit, LV_ALIGN_BOTTOM_MID, 60, -22);

    lbl_ip = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_ip, C_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_ip, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl_ip, "WiFi: ...");
    lv_obj_align(lbl_ip, LV_ALIGN_BOTTOM_LEFT, 8, -4);

    lbl_fps = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_fps, C_AMBER, 0);
    lv_obj_set_style_text_font(lbl_fps, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl_fps, "R-FPS: --   DR-FPS: --");
    lv_obj_align(lbl_fps, LV_ALIGN_BOTTOM_RIGHT, -8, -4);

    /* icons_anim_init() boot_sequence_task içinden, splash sonrası çağrılır →
     * bulb-check sweep ile birlikte görünür. Burada başlatılırsa splash altında kalır. */
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
    char fbuf[40];
    snprintf(fbuf, sizeof(fbuf), "R-FPS: %d   DR-FPS: %d", rfps_smooth, drfps_smooth);
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
    static int prev_speed = -1, prev_rpm = -1, prev_fuel = -1, prev_temp = -200;
    static int  prev_total_km = -1;

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
        snprintf(buf, sizeof(buf), "%dC", s.temp); lv_label_set_text(lbl_temp_val, buf);
        lv_obj_set_style_bg_color(bar_temp,
            s.temp > 110 ? C_RED : (s.temp < 50 ? C_BLUE : C_GREEN), LV_PART_INDICATOR);
        prev_temp = s.temp;
    }

    snprintf(buf, sizeof(buf), "%c", s.gear); lv_label_set_text(lbl_gear, buf);
    lv_color_t gc = C_GREEN;
    if (s.gear == 'P' || s.gear == 'N') gc = C_BLUE;
    else if (s.gear == 'R')             gc = C_RED;
    lv_obj_set_style_text_color(lbl_gear, gc, 0);

    if (s.total_km != prev_total_km) {
        snprintf(buf, sizeof(buf), "%d", s.total_km);
        lv_label_set_text(lbl_km, buf);
        prev_total_km = s.total_km;
    }

    /* Trip computer — değişen alan başına lazy update */
    static int prev_trip_m = -1, prev_trip_sec = -1;
    static int prev_inst_x10 = -1, prev_range = -1;

    if (s.trip_m != prev_trip_m) {
        snprintf(buf, sizeof(buf), "%d.%01d km",
                 s.trip_m / 1000, (s.trip_m % 1000) / 100);
        lv_label_set_text(lbl_trip_km, buf);
        prev_trip_m = s.trip_m;
    }
    if (s.trip_seconds != prev_trip_sec) {
        int mm = s.trip_seconds / 60;
        int ss = s.trip_seconds % 60;
        snprintf(buf, sizeof(buf), "%02d:%02d", mm, ss);
        lv_label_set_text(lbl_trip_time, buf);
        prev_trip_sec = s.trip_seconds;
    }
    if (s.inst_l100_x10 != prev_inst_x10) {
        snprintf(buf, sizeof(buf), "%d.%d",
                 s.inst_l100_x10 / 10, s.inst_l100_x10 % 10);
        lv_label_set_text(lbl_trip_l100, buf);
        prev_inst_x10 = s.inst_l100_x10;
    }
    if (s.range_km != prev_range) {
        if (s.range_km > 0)
            snprintf(buf, sizeof(buf), "%d km", s.range_km);
        else
            snprintf(buf, sizeof(buf), "-- km");
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
 * Boot sequence — splash + needle sweep
 * ============================================================
 * Akış:
 *   t=0       splash overlay (full screen black + brand text) görünür
 *   t=1.0s    fade-out animasyonu başlar (500ms)
 *   t=1.5s    splash silinir, icons_anim_init() → bulb-check (2s ON)
 *   t=1.5s    needle sweep 0→240 (700ms)
 *   t=2.2s    needle sweep 240→0 (700ms)
 *   t=2.9s    g_boot_done_sem give → demo task uyanır
 *   t=3.5s    icons bulb-check biter, state-driven mode'lara geçer
 *
 * Demo, demo_loop_task başlangıcında semaphore'da bekler. Sweep süresince
 * state'i sadece bu task yazar (yarış yok). */

/* DİKKAT: lv_obj_set_style_opa fullscreen container'da LAYERED rendering yapar
 * → off-screen buffer + alpha blend → 800×480 RGB565 = 768KB her frame → bounce
 *   buffer bandwidth'i yetişmez → yırtılma. Bunun yerine bg_opa ve text_opa ayrı
 *   animate edilir (per-pixel alpha, off-screen YOK). */
static void boot_anim_bg_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}
static void boot_anim_text_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_text_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void boot_sequence_task(void *arg)
{
    (void)arg;

    /* 1) Splash overlay oluştur — siyah arka, marka adı + alt satır */
    lvgl_port_lock();
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *splash = lv_obj_create(scr);
    lv_obj_remove_style_all(splash);
    lv_obj_set_size(splash, LCD_H_RES, LCD_V_RES);
    lv_obj_set_pos(splash, 0, 0);
    lv_obj_set_style_bg_color(splash, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(splash, LV_OPA_COVER, 0);
    lv_obj_clear_flag(splash, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *brand = lv_label_create(splash);
    lv_label_set_text(brand, "HK");
    lv_obj_set_style_text_font(brand, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(brand, C_ACCENT, 0);
    lv_obj_center(brand);
    lvgl_port_unlock();

    /* 2) 700 ms göster */
    vTaskDelay(pdMS_TO_TICKS(700));

    /* 3) Hızlı yumuşak fade-out (250 ms) — bg ve text PARALEL ama AYRI animasyonlar
     *    (style_opa kullanmıyoruz → layered rendering yok → yırtılma yok). */
    lvgl_port_lock();
    lv_anim_t ab;
    lv_anim_init(&ab);
    lv_anim_set_var(&ab, splash);
    lv_anim_set_values(&ab, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&ab, 250);
    lv_anim_set_exec_cb(&ab, boot_anim_bg_opa_cb);
    lv_anim_start(&ab);

    lv_anim_t at;
    lv_anim_init(&at);
    lv_anim_set_var(&at, brand);
    lv_anim_set_values(&at, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&at, 250);
    lv_anim_set_exec_cb(&at, boot_anim_text_opa_cb);
    lv_anim_start(&at);
    lvgl_port_unlock();

    vTaskDelay(pdMS_TO_TICKS(280));   /* 250ms anim + 30ms buffer */

    /* 4) Splash temizle, icon bulb-check başlat (2 sn ON) */
    lvgl_port_lock();
    lv_obj_del(splash);
    icons_anim_init();
    lvgl_port_unlock();

    /* 5) Sweep 0→240 (~700ms): step 8, her adımda 24ms */
    for (int v = 0; v <= 240; v += 8) {
        state_lock();
        g_state.speed = v;
        g_state.rpm   = (v > 0) ? (800 + v * 35) : 800;
        state_unlock();
        vTaskDelay(pdMS_TO_TICKS(24));
    }
    /* 6) Sweep 240→0 (~700ms) */
    for (int v = 240; v >= 0; v -= 8) {
        state_lock();
        g_state.speed = v;
        g_state.rpm   = (v > 0) ? (800 + v * 35) : 800;
        state_unlock();
        vTaskDelay(pdMS_TO_TICKS(24));
    }
    state_lock();
    g_state.speed = 0;
    g_state.rpm   = 800;
    state_unlock();

    /* 7) Demo'yu uyandır */
    xSemaphoreGive(g_boot_done_sem);
    vTaskDelete(NULL);
}

void ui_start_boot_sequence(void)
{
    xTaskCreatePinnedToCore(boot_sequence_task, "boot_seq", 4096, NULL, 4, NULL, 0);
}
