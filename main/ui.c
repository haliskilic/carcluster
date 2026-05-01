#include "ui.h"
#include "state.h"
#include "icons.h"
#include "lvgl_port.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "esp_timer.h"

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
static lv_meter_indicator_t *ind_rpm_arc, *ind_rpm_needle, *ind_rpm_redline;
static lv_meter_indicator_t *ind_speed_arc, *ind_speed_needle;
static lv_obj_t *lbl_speed_val;   /* RPM ortadaki sayı kaldırıldı */
static lv_obj_t *lbl_gear, *lbl_km, *lbl_ip;
static lv_obj_t *bar_fuel, *bar_temp, *lbl_fuel_pct, *lbl_temp_val;
static lv_obj_t *lbl_fps;

/* Animation context: dummy var pointer (her unique var için 1 anim slot) */
static int32_t s_anim_speed_var = 0;
static int32_t s_anim_rpm_var   = 0;

static void cb_anim_speed(void *o, int32_t v)
{
    s_anim_speed_var = v;   /* track current displayed value */
    int spd = v > 240 ? 240 : (v < 0 ? 0 : v);
    lv_meter_set_indicator_end_value(meter_speed, ind_speed_arc, spd);
    lv_meter_set_indicator_value(meter_speed, ind_speed_needle, spd);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", (int)v);
    lv_label_set_text(lbl_speed_val, buf);
}

static void cb_anim_rpm(void *o, int32_t v)
{
    s_anim_rpm_var = v;
    int rpm_x1 = v > 9000 ? 9 : (v < 0 ? 0 : v / 1000);
    lv_meter_set_indicator_end_value(meter_rpm, ind_rpm_arc, rpm_x1);
    lv_meter_set_indicator_value(meter_rpm, ind_rpm_needle, rpm_x1);
}

static void smooth_set(int32_t *current, int target,
                       void (*cb)(void*, int32_t), int duration_ms)
{
    if (*current == target) return;
    /* from = halihazırda görünen değer (cb içinde tutuluyor) → kesintisiz akış */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, current);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, *current, target);
    lv_anim_set_time(&a, duration_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    /* NOT setting *current = target — cb yapıyor her frame'de */
}

static lv_obj_t *ic_turn_l, *ic_turn_r;
static lv_obj_t *ic_high, *ic_low;
static lv_obj_t *ic_brake, *ic_abs, *ic_airbag, *ic_seat;
static lv_obj_t *ic_engine, *ic_battery, *ic_oil, *ic_coolant, *ic_fuel_low;

static lv_obj_t *make_meter(int cx, int cy, int size,
                            int v_min, int v_max,
                            int n_majors, int n_minor_per_major,
                            lv_color_t arc_col, int redline_start,
                            lv_meter_indicator_t **ret_arc_ind,
                            lv_meter_indicator_t **ret_needle,
                            lv_meter_indicator_t **ret_redline)
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
    int total_minor = (n_majors - 1) * n_minor_per_major + 1;
    lv_meter_set_scale_ticks(m, sc, total_minor, 2, 8, C_DIM);
    lv_meter_set_scale_major_ticks(m, sc, n_minor_per_major, 4, 14, C_FG, 12);
    lv_meter_set_scale_range(m, sc, v_min, v_max, 270, 135);

    /* Redline (RPM için kırmızı bant) */
    if (redline_start > 0 && ret_redline) {
        *ret_redline = lv_meter_add_arc(m, sc, 6, C_RED, -8);
        lv_meter_set_indicator_start_value(m, *ret_redline, redline_start);
        lv_meter_set_indicator_end_value(m, *ret_redline, v_max);
    }

    /* Arc fill (değer indicator) */
    *ret_arc_ind = lv_meter_add_arc(m, sc, 12, arc_col, -22);
    lv_meter_set_indicator_start_value(m, *ret_arc_ind, v_min);
    lv_meter_set_indicator_end_value(m, *ret_arc_ind, v_min);

    /* Needle */
    *ret_needle = lv_meter_add_needle_line(m, sc, 4, lv_color_hex(0xffffff), -10);
    lv_meter_set_indicator_value(m, *ret_needle, v_min);

    return m;
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

    /* Sol: RPM kadranı (0-9 x1000, 10 major her 1'de, redline 7'den) */
    meter_rpm = make_meter(LX, CY, ARC_R, 0, 9, 10, 5, C_RED, 7,
                           &ind_rpm_arc, &ind_rpm_needle, &ind_rpm_redline);

    lv_obj_t *u1 = lv_label_create(scr);
    lv_obj_set_style_text_color(u1, C_DIM, 0);
    lv_obj_set_style_text_font(u1, &lv_font_montserrat_18, 0);
    lv_label_set_text(u1, "RPM x 1000");
    lv_obj_align_to(u1, meter_rpm, LV_ALIGN_CENTER, 0, 50);

    /* Sağ: Hız kadranı (0-240, 13 major her 20'de) */
    meter_speed = make_meter(RX, CY, ARC_R, 0, 240, 13, 4, C_ACCENT, 0,
                             &ind_speed_arc, &ind_speed_needle, NULL);

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

    lbl_km = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_km, C_FG, 0);
    lv_obj_set_style_text_font(lbl_km, &lv_font_montserrat_24, 0);
    lv_label_set_text(lbl_km, "ODO 0 km");
    lv_obj_align(lbl_km, LV_ALIGN_BOTTOM_MID, 0, -20);

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

    if (s.speed < 0)   s.speed = 0;
    if (s.speed > 999) s.speed = 999;
    if (s.rpm < 0)     s.rpm = 0;
    if (s.rpm > 9999)  s.rpm = 9999;

    /* Direct set — animasyon kaldırıldı, sadece değer değişince güncelle */
    static int prev_speed = -1, prev_rpm = -1, prev_fuel = -1, prev_temp = -200;

    if (s.speed != prev_speed) {
        smooth_set(&s_anim_speed_var, s.speed, cb_anim_speed, 35);
        prev_speed = s.speed;
    }

    if (s.rpm != prev_rpm) {
        smooth_set(&s_anim_rpm_var, s.rpm, cb_anim_rpm, 35);
        prev_rpm = s.rpm;
    }

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

    snprintf(buf, sizeof(buf), "ODO %d km", s.total_km); lv_label_set_text(lbl_km, buf);

    /* Telltale: sadece state değiştiyse çiz (ikon canvas yeniden çizimi pahalı) */
    static cluster_state_t prev_flags = {0};
    bool fl = s.fuel_low_warn || (s.fuel < 15);
    if (s.left_blink    != prev_flags.left_blink)    icon_set_active(ic_turn_l,   s.left_blink);
    if (s.right_blink   != prev_flags.right_blink)   icon_set_active(ic_turn_r,   s.right_blink);
    if (s.high_beam     != prev_flags.high_beam)     icon_set_active(ic_high,     s.high_beam);
    if (s.low_beam      != prev_flags.low_beam)      icon_set_active(ic_low,      s.low_beam);
    if (s.brake_warn    != prev_flags.brake_warn)    icon_set_active(ic_brake,    s.brake_warn);
    if (s.abs_warn      != prev_flags.abs_warn)      icon_set_active(ic_abs,      s.abs_warn);
    if (s.airbag_warn   != prev_flags.airbag_warn)   icon_set_active(ic_airbag,   s.airbag_warn);
    if (s.seatbelt_warn != prev_flags.seatbelt_warn) icon_set_active(ic_seat,     s.seatbelt_warn);
    if (s.engine_warn   != prev_flags.engine_warn)   icon_set_active(ic_engine,   s.engine_warn);
    if (s.battery_warn  != prev_flags.battery_warn)  icon_set_active(ic_battery,  s.battery_warn);
    if (s.oil_warn      != prev_flags.oil_warn)      icon_set_active(ic_oil,      s.oil_warn);
    if (s.coolant_warn  != prev_flags.coolant_warn)  icon_set_active(ic_coolant,  s.coolant_warn);
    if (fl              != prev_flags.fuel_low_warn) icon_set_active(ic_fuel_low, fl);
    prev_flags = s;
    prev_flags.fuel_low_warn = fl;
}

void ui_set_ip(const char *ip)
{
    char buf[40];
    snprintf(buf, sizeof(buf), "WiFi: %s :23", ip);
    lv_label_set_text(lbl_ip, buf);
}
