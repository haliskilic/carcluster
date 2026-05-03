#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_app_desc.h"
#include "board.h"
#include "lvgl_port.h"
#include "state.h"
#include "ui.h"
#include "demo.h"
#include "persist.h"
#include "trip.h"
#include "cpu_meter.h"
#include "touch.h"
#include "theme.h"
#include "units.h"
#include "limits.h"
#include "cmd_listener.h"
#include "wifi.h"
#include "splash.h"
#include "idle.h"

static const char *TAG = "carcluster";

cluster_state_t g_state = { .gear = 'P', .fuel = 75, .temp = 90 };  /* total_km NVS'ten */
SemaphoreHandle_t g_state_mutex;
volatile TaskHandle_t g_demo_task = NULL;
volatile TaskHandle_t g_ui_task   = NULL;
volatile bool g_demo_paused = false;

void state_init(void)
{
    g_state_mutex = xSemaphoreCreateMutex();
    if (!g_state_mutex) {
        ESP_LOGE(TAG, "state mutex create failed");
        abort();
    }
}

/* board_init'i core 1'de çalışan one-shot task — RGB DMA ISR'ı kaydeden core
 * o ISR'ı taşır. lvgl_task da core 1'de pinli (lvgl_port.c LVGL_TASK_CORE=1).
 * Espressif resmi: aynı core üzerinde RGB ISR + LVGL → PCLK headroom artar
 * (PSRAM contention azalır, cache locality iyileşir). */
static SemaphoreHandle_t s_board_init_done = NULL;

static void board_init_pinned(void *arg)
{
    (void)arg;
    board_init();
    xSemaphoreGive(s_board_init_done);
    vTaskDelete(NULL);
}

static void ui_refresh_task(void *arg)
{
    /* VSYNC notify-driven — her panel scan başlangıcında uyanır,
     * 1 frame içinde state'i okuyup widget'ları günceller. Faz kilidi.
     * TWDT subscribe: 100ms notify timeout = 10s'den çok kısa, hep pet. */
    esp_task_wdt_add(NULL);
    while (1) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        lvgl_port_lock();
        ui_refresh();
        lvgl_port_unlock();
        esp_task_wdt_reset();
    }
}

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_EXT:       return "EXT";
        case ESP_RST_SW:        return "SW";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_INT_WDT:   return "INT_WDT";
        case ESP_RST_TASK_WDT:  return "TASK_WDT";
        case ESP_RST_WDT:       return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "UNKNOWN";
    }
}

void app_main(void)
{
    /* Boot diagnostics — version + reset reason her boot'ta tek bakışta görünür */
    const esp_app_desc_t *app = esp_app_get_description();
    esp_reset_reason_t rst = esp_reset_reason();
    ESP_LOGI(TAG, "=== carcluster v%s (%s %s) ===",
             app->version, app->date, app->time);
    ESP_LOGI(TAG, "Reset reason: %s (%d)", reset_reason_str(rst), rst);
    ESP_LOGI(TAG, "VSYNC-driven phase-locked pipeline");
    state_init();

    /* NVS persistence — total_km'i yükle, autosave task'ını başlat */
    persist_init();
    g_state.total_km = persist_load_total_km(12345);

    /* Bu boot için reset reason counter'ı increment et — fault tracking */
    uint32_t rst_count = persist_inc_reset_counter((int)rst);
    ESP_LOGI(TAG, "Reset count for %s: %lu", reset_reason_str(rst), (unsigned long)rst_count);

    /* Tema palet — ui_build'den önce global C_* renkleri set'le */
    theme_id_t theme = (theme_id_t)persist_load_theme();
    theme_apply(theme);
    ESP_LOGI(TAG, "Theme: %s (%d)", theme_name(theme), theme);

    /* Unit + limits — ui_build içinde formatter ve gauge bandı için */
    unit_init();
    limits_init();

    /* RGB panel'i core 1'de pinli task'tan başlat → ISR core 1'de kaydolur */
    s_board_init_done = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(board_init_pinned, "board_init",
                            4096, NULL, 5, NULL, 1);
    xSemaphoreTake(s_board_init_done, portMAX_DELAY);
    vSemaphoreDelete(s_board_init_done);
    s_board_init_done = NULL;

    /* Önce task'ları yarat — VSYNC ISR registered olmadan önce
     * g_ui_task ve g_demo_task non-NULL olsun (race window kapalı). */
    TaskHandle_t ui_h = NULL;
    xTaskCreatePinnedToCore(ui_refresh_task, "ui_refresh",
                            4096, NULL, 3, &ui_h, 0);
    g_ui_task = ui_h;
    demo_start();

    /* GT911 touch (debug: minimal init only) */
    touch_init();

    /* Şimdi LVGL + VSYNC ISR — handle'lar hazır */
    lvgl_port_init();

    cmd_listener_start();

    /* Splash + cluster build — TEK lvgl_port_lock altında. lvgl_port_lock
     * lv_timer_handler'ı durdurur, render olmaz. ui_build cluster widget'larını
     * yarat, splash_show splash widget'larını yarat (son child = üstte çizilir).
     * Unlock sonrası ilk frame: splash kapsayan cluster'ın üstünde. Böylece
     * kullanıcı cluster'ı build sırasında bir an bile göremez. */
    lvgl_port_lock();
    ui_build();
    ui_refresh();
    ui_set_ip("DEMO");
    splash_show();           /* en son yaratıldı = en üstte render edilir */
    lvgl_port_unlock();

    /* Splash 1500 ms — branding süresi (cluster zaten arkada hazır). */
    vTaskDelay(pdMS_TO_TICKS(1500));

    lvgl_port_lock();
    splash_hide();           /* atomik reveal: tek frame'de cluster görünür */
    lvgl_port_unlock();

    /* Persistence autosave 30 sn'de bir total_km'i NVS'e yazar */
    persist_start_autosave();

    /* Trip computer 1 Hz integrator (mesafe + süre + tüketim + range) */
    trip_start();

    /* CPU meter — idle hook'ları register, sampler task spawn (per-core %) */
    cpu_meter_init();

    /* Idle sleep — demo paused + 30s no touch → backlight off, touch wake */
    idle_init();

    /* WiFi STA — credentials NVS'te varsa otomatik bağlan, yoksa pas geç.
     * cmd_listener splash sırasında zaten başlatıldı. */
    wifi_init();

    ESP_LOGI(TAG, "Ready.");
}
