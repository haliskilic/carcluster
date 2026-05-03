#include "wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi";

#define NS          "carcluster"
#define KEY_SSID    "wifi_ssid"
#define KEY_PASS    "wifi_pass"

#define BIT_GOT_IP  BIT0
#define BIT_FAIL    BIT1

static volatile wifi_state_t s_state = WIFI_STATE_DISABLED;
static char     s_ssid[33] = {0};
static char     s_ip[16]   = "0.0.0.0";
static int      s_rssi     = 0;
static int      s_retry    = 0;
#define MAX_RETRY  5

static EventGroupHandle_t s_evt;
static esp_netif_t       *s_netif = NULL;
static bool               s_inited = false;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        s_state = WIFI_STATE_CONNECTING;
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry < MAX_RETRY) {
            s_retry++;
            ESP_LOGW(TAG, "disconnect, retry %d/%d", s_retry, MAX_RETRY);
            esp_wifi_connect();
        } else {
            s_state = WIFI_STATE_FAILED;
            xEventGroupSetBits(s_evt, BIT_FAIL);
            ESP_LOGW(TAG, "connect failed after %d retries", MAX_RETRY);
        }
        strcpy(s_ip, "0.0.0.0");
        s_rssi = 0;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        s_retry  = 0;
        s_state  = WIFI_STATE_CONNECTED;
        xEventGroupSetBits(s_evt, BIT_GOT_IP);
        ESP_LOGI(TAG, "connected: %s, IP %s", s_ssid, s_ip);
    }
}

/* RSSI'yi periyodik 2 Hz okuyup s_rssi cache'ler — Diag UI canlı görsün */
static void rssi_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (s_state == WIFI_STATE_CONNECTED) {
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                s_rssi = ap.rssi;
            }
        }
    }
}

/* WiFi stack'i bir kez init eder. Tekrar set_credentials'da sadece config update */
static void wifi_stack_init_once(void)
{
    if (s_inited) return;
    s_inited = true;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    s_evt = xEventGroupCreate();
    xTaskCreatePinnedToCore(rssi_task, "wifi_rssi", 2048, NULL, 1, NULL, 0);
}

static bool load_credentials(char *ssid_out, char *pass_out)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t s = 33, p = 65;
    bool ok = (nvs_get_str(h, KEY_SSID, ssid_out, &s) == ESP_OK)
           && (nvs_get_str(h, KEY_PASS, pass_out, &p) == ESP_OK);
    nvs_close(h);
    return ok && ssid_out[0] != 0;
}

static void connect_with(const char *ssid, const char *pass)
{
    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid,     ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, pass, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wc.sta.pmf_cfg.capable    = true;
    wc.sta.pmf_cfg.required   = false;

    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    s_retry = 0;
    s_state = WIFI_STATE_CONNECTING;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    /* esp_wifi_start tekrarlanırsa zaten çalışıyorsa OK döner — start sonrası
     * STA_START event'i auto connect tetikler. Disconnect→reconnect için
     * disconnect+connect kullanabiliriz. */
    esp_err_t r = esp_wifi_start();
    if (r == ESP_OK) {
        ESP_LOGI(TAG, "wifi started, connecting to '%s'", ssid);
    } else if (r == ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGE(TAG, "wifi not init");
    } else {
        /* Already started — direct connect */
        esp_wifi_disconnect();
        esp_wifi_connect();
    }
}

void wifi_init(void)
{
    char ssid[33] = {0}, pass[65] = {0};
    if (!load_credentials(ssid, pass)) {
        ESP_LOGI(TAG, "WiFi not configured (UART komut: WIFI SET <ssid> <pass>)");
        return;
    }
    wifi_stack_init_once();
    connect_with(ssid, pass);
}

void wifi_set_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, KEY_SSID, ssid);
        nvs_set_str(h, KEY_PASS, pass);
        nvs_commit(h);
        nvs_close(h);
    }
    wifi_stack_init_once();
    connect_with(ssid, pass);
}

void wifi_get_status(wifi_status_t *out)
{
    /* State'i fact'lardan türet — internal s_state event sıralaması veya
     * race nedeniyle stale kalabiliyor (özellikle wifi_set_credentials
     * sonrası GOT_IP işlemi tamamlandıktan sonra connect_with'in CONNECTING
     * yazımı). IP set ise CONNECTED kabul edilir, DHCP başarılı demek. */
    bool has_ip = (strcmp(s_ip, "0.0.0.0") != 0);
    if (has_ip)                            out->state = WIFI_STATE_CONNECTED;
    else if (s_state == WIFI_STATE_FAILED) out->state = WIFI_STATE_FAILED;
    else if (s_ssid[0])                    out->state = WIFI_STATE_CONNECTING;
    else                                   out->state = WIFI_STATE_DISABLED;

    strncpy(out->ssid, s_ssid, sizeof(out->ssid) - 1);
    out->ssid[sizeof(out->ssid) - 1] = 0;
    strncpy(out->ip, s_ip, sizeof(out->ip) - 1);
    out->ip[sizeof(out->ip) - 1] = 0;
    out->rssi = s_rssi;
}
