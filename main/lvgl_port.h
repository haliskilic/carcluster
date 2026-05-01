#pragma once
#include "lvgl.h"
#include <stdint.h>

void lvgl_port_init(void);
void lvgl_port_lock(void);
void lvgl_port_unlock(void);
uint32_t lvgl_port_get_flush_count(void);
uint32_t lvgl_port_get_vsync_count(void);
