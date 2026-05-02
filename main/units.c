#include "units.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "units";
static unit_t s_unit = UNIT_METRIC;

#define NS  "carcluster"
#define KEY "unit"

void unit_init(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t v = 0;
        if (nvs_get_u8(h, KEY, &v) == ESP_OK) s_unit = (unit_t)v;
        nvs_close(h);
    }
    ESP_LOGI(TAG, "unit: %s", s_unit == UNIT_IMPERIAL ? "imperial" : "metric");
}

unit_t unit_get(void) { return s_unit; }

void unit_set(unit_t u)
{
    s_unit = u;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, KEY, (uint8_t)u);
        nvs_commit(h);
        nvs_close(h);
    }
}

const char *unit_speed_label(void)  { return s_unit == UNIT_IMPERIAL ? "mph"   : "km/h"; }
const char *unit_dist_label(void)   { return s_unit == UNIT_IMPERIAL ? "mi"    : "km";   }
const char *unit_temp_label(void)   { return s_unit == UNIT_IMPERIAL ? "°F"    : "°C";   }
const char *unit_consum_label(void) { return s_unit == UNIT_IMPERIAL ? "MPG"   : "L/100"; }

int unit_conv_speed(int kmh)
{
    /* mph = kmh × 0.621371 — int-only (×621/1000) */
    return s_unit == UNIT_IMPERIAL ? (kmh * 621) / 1000 : kmh;
}

uint32_t unit_conv_dist_km(uint32_t km)
{
    return s_unit == UNIT_IMPERIAL ? (km * 621u) / 1000u : km;
}

int unit_conv_temp_c(int c)
{
    /* °F = (°C × 9/5) + 32 */
    return s_unit == UNIT_IMPERIAL ? (c * 9) / 5 + 32 : c;
}

int unit_conv_l100_x10(int l100_x10)
{
    /* MPG (US) = 235.214 / (L/100km). l100_x10=80 (8.0) → mpg=29.4 → mpg_x10=294
     * mpg_x10 = 23521 / l100_x10. */
    if (s_unit != UNIT_IMPERIAL) return l100_x10;
    if (l100_x10 < 1) return 0;
    return 23521 / l100_x10;
}
