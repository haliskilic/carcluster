#pragma once

/* Idle sleep — demo pause durumunda + N saniye dokunma yoksa backlight off.
 * Touch event geldiğinde uyanır (idle_mark_activity → BL_EN restore).
 * IGN signal donanım gelince bu mantık IGN_OFF ile genişletilir.
 *
 * Demo running iken activity her zaman fresh sayılır (idle threshold tetiklenmez).
 * Sadece demo paused iken sayaç işler. */

void idle_init(void);
void idle_mark_activity(void);   /* touch event veya kullanıcı eylemi sonrası */
