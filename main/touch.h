#pragma once
#include <stdbool.h>

/* GT911 touch driver — Waveshare ESP32-S3-Touch-LCD-7 V1.2.
 *
 * Init sequence (timing-critical, esphome #15019 ~25% boot fail without it):
 *   1) GPIO4 (INT) output LOW  — selects I2C addr 0x5D
 *   2) CH422G EXIO1 (TP_RST) LOW
 *   3) Wait 10ms
 *   4) CH422G EXIO1 HIGH (release reset)
 *   5) Wait 50ms (GT911 internal init)
 *   6) GPIO4 to input (high-Z)
 *
 * Polling task (50 Hz) reads touch state, updates shared snapshot.
 * lvgl_port indev callback reads snapshot. */

void touch_init(void);
void touch_get_state(int *x, int *y, bool *pressed);
