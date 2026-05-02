#include "limits.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "limits";

static limits_t s_lim = {
    .rpm_redline    = LIMITS_RPM_DEFAULT,
    .coolant_warn_c = LIMITS_TEMP_DEFAULT,
};

#define NS   "carcluster"
#define KEY  "limits"

static void clamp(limits_t *l)
{
    if (l->rpm_redline < LIMITS_RPM_MIN)  l->rpm_redline = LIMITS_RPM_MIN;
    if (l->rpm_redline > LIMITS_RPM_MAX)  l->rpm_redline = LIMITS_RPM_MAX;
    if (l->coolant_warn_c < LIMITS_TEMP_MIN) l->coolant_warn_c = LIMITS_TEMP_MIN;
    if (l->coolant_warn_c > LIMITS_TEMP_MAX) l->coolant_warn_c = LIMITS_TEMP_MAX;
}

void limits_init(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(s_lim);
        nvs_get_blob(h, KEY, &s_lim, &sz);
        nvs_close(h);
    }
    clamp(&s_lim);
    ESP_LOGI(TAG, "limits: redline=%d coolant=%d°C",
             s_lim.rpm_redline, s_lim.coolant_warn_c);
}

const limits_t *limits_get(void) { return &s_lim; }

void limits_set(const limits_t *l)
{
    s_lim = *l;
    clamp(&s_lim);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, KEY, &s_lim, sizeof(s_lim));
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "limits saved: redline=%d coolant=%d°C",
             s_lim.rpm_redline, s_lim.coolant_warn_c);
}
