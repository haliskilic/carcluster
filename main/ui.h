#pragma once
void ui_build(void);
void ui_refresh(void);   /* state -> UI; LVGL kilidi içinde çağrılmalı */
void ui_set_ip(const char *ip);

/* Boot sequence — splash overlay (logo + fade) + needle sweep (0→max→0).
 * Sonunda g_boot_done_sem'i give eder, demo task uyanır.
 * ui_build sonrasında bir kere çağırılmalı; arka plan task'ı spawn eder. */
void ui_start_boot_sequence(void);
