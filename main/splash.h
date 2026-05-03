#pragma once

/* Boot splash — siyah arka plan üzerinde HK monogram. board_init bitip
 * lvgl_port_init hazır olur olmaz gösterilir; ui_build paralel çalışır
 * (cluster widget'ları lv_scr_act üzerinde). Splash lv_layer_top()'a koyulur,
 * cluster'ı tamamen kapatır. ui_build + ui_refresh tamamlanınca
 * splash_hide() çağrılır → atomik reveal, transit flicker yok.
 *
 * Min süre yok — cluster hazır olur olmaz kaybolur. */

void splash_show(void);   /* lvgl_port_lock altında çağrılır */
void splash_hide(void);   /* lvgl_port_lock altında çağrılır */
