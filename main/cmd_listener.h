#pragma once

/* Host'tan USB-Serial-JTAG üzerinden gelen komut satırlarını dinler.
 * Otomatik screenshot ve test araçları için debug interface.
 *
 * Protokol: ASCII satırlar, \n veya \r terminatorlü.
 *
 *   SHOT MAIN              → modal'ı kapat, ana cluster ekranını çek
 *   SHOT MODAL TRIP        → settings modal aç + Trip sekmesi → çek
 *   SHOT MODAL DISPLAY     → Display sekmesi → çek
 *   SHOT MODAL LIMITS      → Limits sekmesi → çek
 *   SHOT MODAL DIAG        → Diag sekmesi → çek
 *
 * Cevap: aynı komut için screenshot.c [SHOT-BEGIN ... SHOT-END] dökümü.
 * Listener LVGL state'ini değiştirmek için ui_cmd_* helper'larını çağırır
 * (lvgl_port_lock altında). */

void cmd_listener_start(void);
