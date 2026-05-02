#pragma once
#include <stdint.h>

/* User-adjustable thresholds — NVS persist.
 *   rpm_redline:     RPM redline başlangıç (default 7000) — gauge color band
 *                    rebuild gerekir, reboot-required uygulanır.
 *   coolant_warn_c:  soğutma suyu uyarı eşiği °C (default 110) — live trigger,
 *                    reboot gerekmiyor. */

typedef struct {
    int rpm_redline;       /* 5000..9000 RPM */
    int coolant_warn_c;    /* 90..120 °C */
} limits_t;

#define LIMITS_RPM_DEFAULT   7000
#define LIMITS_RPM_MIN       5000
#define LIMITS_RPM_MAX       9000

#define LIMITS_TEMP_DEFAULT  110
#define LIMITS_TEMP_MIN      90
#define LIMITS_TEMP_MAX      120

void           limits_init(void);             /* NVS'ten yükle */
const limits_t *limits_get(void);             /* runtime cache pointer */
void           limits_set(const limits_t *l); /* NVS'e yaz */
