#pragma once
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

#define LCD_H_RES 800
#define LCD_V_RES 480

void board_init(void);
esp_lcd_panel_handle_t board_get_panel(void);
