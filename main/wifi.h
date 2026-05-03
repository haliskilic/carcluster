#pragma once
#include <stdbool.h>
#include <stdint.h>

/* WiFi STA — NVS'ten credentials yükler, otomatik bağlanır + reconnect.
 * SSID/password NVS'te saklı; UART komutu (cmd_listener) ile set edilir.
 * Boot'ta credentials yoksa "not configured" log atar, bağlanmaz.
 *
 * Diag UI ve OTA modülü canlı durumu wifi_get_status() ile okur. */

typedef enum {
    WIFI_STATE_DISABLED   = 0,   /* credentials yok */
    WIFI_STATE_CONNECTING = 1,
    WIFI_STATE_CONNECTED  = 2,
    WIFI_STATE_FAILED     = 3,   /* auth fail / SSID not found */
} wifi_state_t;

typedef struct {
    wifi_state_t state;
    char         ssid[33];       /* SSID 32 char max + null */
    char         ip[16];         /* "192.168.1.123" */
    int          rssi;           /* dBm */
} wifi_status_t;

void wifi_init(void);                            /* boot'ta çağrılır */
void wifi_set_credentials(const char *ssid, const char *pass);  /* NVS save + reconnect */
void wifi_get_status(wifi_status_t *out);
