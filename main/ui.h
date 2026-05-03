#pragma once
void ui_build(void);
void ui_refresh(void);   /* state -> UI; LVGL kilidi içinde çağrılmalı */
void ui_set_ip(const char *ip);

/* Cmd listener (auto-screenshot tooling) için external API — lvgl_port_lock
 * dahili olarak alınır. tab_id: 0=Trip 1=Display 2=Limits 3=Diag */
void ui_cmd_show_modal(int tab_id);
void ui_cmd_close_modal(void);
