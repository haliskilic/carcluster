#include "cmd_listener.h"
#include "ui.h"
#include "screenshot.h"
#include "wifi.h"
#include "ota.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "cmd";

static int parse_modal_tab(const char *s)
{
    if (strcmp(s, "TRIP")    == 0) return 0;
    if (strcmp(s, "DISPLAY") == 0) return 1;
    if (strcmp(s, "LIMITS")  == 0) return 2;
    if (strcmp(s, "DIAG")    == 0) return 3;
    return -1;
}

static void dispatch(char *line)
{
    /* Trim trailing CR/LF */
    size_t L = strlen(line);
    while (L > 0 && (line[L-1] == '\n' || line[L-1] == '\r')) line[--L] = 0;
    if (L == 0) return;

    ESP_LOGI(TAG, "cmd: %s", line);

    if (strcmp(line, "SHOT MAIN") == 0) {
        ui_cmd_close_modal();
        vTaskDelay(pdMS_TO_TICKS(300));     /* render settle */
        screenshot_dump_uart();
        return;
    }
    if (strncmp(line, "SHOT MODAL ", 11) == 0) {
        int tab = parse_modal_tab(line + 11);
        if (tab < 0) {
            ESP_LOGW(TAG, "unknown tab: %s", line + 11);
            return;
        }
        ui_cmd_show_modal(tab);
        vTaskDelay(pdMS_TO_TICKS(400));     /* render + tabview anim */
        screenshot_dump_uart();
        return;
    }
    if (strncmp(line, "WIFI SET ", 9) == 0) {
        /* WIFI SET <ssid> <pass>  — son token password (boşluk içerebilir? hayır,
         * basit parse: ilk space'ten sonra ssid, ondan sonra space'ten sonra pass) */
        char *ssid = line + 9;
        char *space = strchr(ssid, ' ');
        if (!space) {
            printf("[WIFI ERROR usage: WIFI SET <ssid> <pass>]\n");
            fflush(stdout);
            return;
        }
        *space = 0;
        char *pass = space + 1;
        printf("[WIFI SET ssid=%s pass=%.*s***]\n", ssid,
               (pass[0] && pass[1]) ? 2 : 0, pass);
        fflush(stdout);
        wifi_set_credentials(ssid, pass);
        return;
    }
    if (strcmp(line, "WIFI STATUS") == 0) {
        wifi_status_t st;
        wifi_get_status(&st);
        const char *names[] = {"DISABLED", "CONNECTING", "CONNECTED", "FAILED"};
        printf("[WIFI STATUS state=%s ssid=%s ip=%s rssi=%d]\n",
               names[st.state], st.ssid, st.ip, st.rssi);
        fflush(stdout);
        return;
    }
    if (strncmp(line, "OTA URL ", 8) == 0) {
        ota_set_url(line + 8);
        printf("[OTA URL ok %s]\n", line + 8);
        fflush(stdout);
        return;
    }
    if (strcmp(line, "OTA START") == 0) {
        ota_start();
        printf("[OTA STARTED]\n");
        fflush(stdout);
        return;
    }
    if (strcmp(line, "OTA STATUS") == 0) {
        ota_status_t s;
        ota_get_status(&s);
        const char *names[] = {"IDLE", "DOWNLOADING", "VERIFYING", "DONE", "FAILED"};
        printf("[OTA STATUS state=%s pct=%d msg=%s]\n",
               names[s.state], s.pct, s.msg);
        fflush(stdout);
        return;
    }
    ESP_LOGW(TAG, "unknown command");
}

static void cmd_task(void *arg)
{
    (void)arg;
    /* USB-Serial-JTAG driver — host'tan byte byte oku. ESP-IDF console hâlâ
     * stdout (printf) kullanır, çift yön aynı kanal. RX buf 256 byte, TX
     * buf 256 byte (printf zaten kendi buffer'ından flush eder). */
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "usb_serial_jtag driver install failed");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "command listener ready");

    char line[64];
    int idx = 0;
    while (1) {
        uint8_t byte;
        int n = usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(100));
        if (n <= 0) continue;

        if (byte == '\n' || byte == '\r') {
            if (idx > 0) {
                line[idx] = 0;
                dispatch(line);
                idx = 0;
            }
        } else if (idx < (int)sizeof(line) - 1) {
            line[idx++] = (char)byte;
        } else {
            /* overflow — reset */
            idx = 0;
        }
    }
}

void cmd_listener_start(void)
{
    xTaskCreatePinnedToCore(cmd_task, "cmd", 4096, NULL, 1, NULL, 0);
}
