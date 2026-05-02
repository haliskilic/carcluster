#pragma once
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include <stdint.h>

#define LCD_H_RES 800
#define LCD_V_RES 480

/* CH422G EXIO bit map (Waveshare ESP32-S3-Touch-LCD-7 V1.2):
 *   EXIO1: Touch panel reset (TP_RST)
 *   EXIO2: Backlight enable (BL_EN)
 *   EXIO3: LCD reset (LCD_RST)
 *   EXIO6: LCD VDD enable (LCD_VDD) */
#define BIT_TP_RST  (1u << 1)
#define BIT_BL_EN   (1u << 2)
#define BIT_LCD_RST (1u << 3)
#define BIT_LCD_VDD (1u << 6)

void board_init(void);
esp_lcd_panel_handle_t board_get_panel(void);

/* CH422G I2C write (mutex-protected) — touch.c TP_RST için kullanır */
void board_ch422g_write(uint8_t set_mask, uint8_t clr_mask);
