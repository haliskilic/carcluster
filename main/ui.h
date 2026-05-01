#pragma once
void ui_build(void);
void ui_refresh(void);   /* state -> UI; LVGL kilidi içinde çağrılmalı */
void ui_set_ip(const char *ip);
