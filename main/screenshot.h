#pragma once

/* Screenshot dump via UART — LVGL screen snapshot → RGB565 → base64 → printf.
 *
 * Cihazda PNG encoder yok (zlib + libpng ağır). Host tarafında
 * tools/grab_screenshot.py serial'ı dinler, [SHOT-BEGIN ... SHOT-END]
 * markerları arasındaki base64'ü decode eder, RGB565 → RGB888 → PNG yapar.
 *
 * Buffer: 800×480×2 = 768 KB PSRAM (geçici, dump sonrası free).
 * Süre: ~115200 baud → ~1.5 dk (bir kerelik tool olarak kabul edilir).
 *
 * Trigger: Settings → Diag sekmesindeki "Screenshot" butonu. Long-press
 * modal'ı kapatmadan tetiklemek için modal_close çağrılmaz. */

void screenshot_dump_uart(void);
