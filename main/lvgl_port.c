#include "lvgl_port.h"
#include "board.h"
#include "state.h"
#include "touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"

static const char *TAG = "lvgl_port";

#define LVGL_TICK_MS    2
#define LVGL_TASK_PRIO  4
#define LVGL_TASK_CORE  1
#define LVGL_TASK_STACK 8192
#define BUF_LINES       160  /* Phase 2: 80→160 → frame başına 6→3 partial flush */
#define LVGL_PERIOD_MS  5    /* sabit periyot — dinamik yerine */

static SemaphoreHandle_t s_lvgl_mutex;
static esp_lcd_panel_handle_t s_panel;
static volatile uint32_t s_flush_count = 0;
static volatile uint32_t s_vsync_count = 0;

uint32_t lvgl_port_get_flush_count(void) { return s_flush_count; }
uint32_t lvgl_port_get_vsync_count(void) { return s_vsync_count; }

/* VSYNC ISR — panel scan başlangıcında demo + ui_refresh task'larını
 * uyandırır. Faz kilit: tüm task'lar her zaman aynı 35 ms slot'unda işler. */
static IRAM_ATTR bool on_vsync(esp_lcd_panel_handle_t panel,
                               const esp_lcd_rgb_panel_event_data_t *evt,
                               void *user_ctx)
{
    s_vsync_count++;
    BaseType_t hp = pdFALSE;
    if (g_demo_task) vTaskNotifyGiveFromISR(g_demo_task, &hp);
    if (g_ui_task)   vTaskNotifyGiveFromISR(g_ui_task,   &hp);
    return hp == pdTRUE;
}

/* flush_cb: lv_timer_handler içinden çağrılır → s_lvgl_mutex zaten tutulur
 * (lvgl_task içeride mutex altında lv_timer_handler_run_in_period çağırır).
 * Yani burada açıkça lock almaya gerek yok. */
static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color)
{
    /* Bloksuz — VSYNC bekleme yok. Panel num_fbs=2 + bounce buffer ile
     * kendi swap'ı yönetir, tearing yok (12 MHz PCLK'da test edildi). */
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, color);
    s_flush_count++;
    lv_disp_flush_ready(drv);
}

static void tick_cb(void *arg) { lv_tick_inc(LVGL_TICK_MS); }

/* GT911 touch → LVGL pointer indev. touch.c'nin shared snapshot'undan okur. */
static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    int x, y;
    bool pressed;
    touch_get_state(&x, &y, &pressed);
    if (pressed) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

static void lvgl_task(void *arg)
{
    /* lv_timer_handler_run_in_period: idiomatic LVGL pattern, sabit periyot */
    while (1) {
        if (xSemaphoreTake(s_lvgl_mutex, portMAX_DELAY) == pdTRUE) {
            lv_timer_handler_run_in_period(LVGL_PERIOD_MS);
            xSemaphoreGive(s_lvgl_mutex);
            vTaskDelay(pdMS_TO_TICKS(LVGL_PERIOD_MS));
        }
    }
}

void lvgl_port_init(void)
{
    s_panel = board_get_panel();
    s_lvgl_mutex = xSemaphoreCreateMutex();
    if (!s_lvgl_mutex) {
        ESP_LOGE(TAG, "lvgl mutex create failed");
        abort();
    }

    esp_lcd_rgb_panel_event_callbacks_t cbs = { .on_vsync = on_vsync };
    esp_lcd_rgb_panel_register_event_callbacks(s_panel, &cbs, NULL);

    lv_init();

    static lv_disp_draw_buf_t draw_buf;
    lv_color_t *b1 = heap_caps_malloc(LCD_H_RES * BUF_LINES * sizeof(lv_color_t),
                                      MALLOC_CAP_SPIRAM);
    lv_color_t *b2 = heap_caps_malloc(LCD_H_RES * BUF_LINES * sizeof(lv_color_t),
                                      MALLOC_CAP_SPIRAM);
    if (!b1 || !b2) {
        ESP_LOGE(TAG, "LVGL draw buffer alloc failed (%dx%d px)",
                 LCD_H_RES, BUF_LINES);
        abort();
    }
    lv_disp_draw_buf_init(&draw_buf, b1, b2, LCD_H_RES * BUF_LINES);

    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res = LCD_H_RES;
    drv.ver_res = LCD_V_RES;
    drv.flush_cb = flush_cb;
    drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&drv);

    /* Touch indev — pointer type, GT911'den okur */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    const esp_timer_create_args_t targs = { .callback = tick_cb, .name = "lvtick" };
    esp_timer_handle_t tick;
    esp_timer_create(&targs, &tick);
    esp_timer_start_periodic(tick, LVGL_TICK_MS * 1000);

    xTaskCreatePinnedToCore(lvgl_task, "lvgl", LVGL_TASK_STACK, NULL,
                            LVGL_TASK_PRIO, NULL, LVGL_TASK_CORE);
    ESP_LOGI(TAG, "LVGL ready (VSYNC notify, bloksuz flush)");
}

void lvgl_port_lock(void)   { xSemaphoreTake(s_lvgl_mutex, portMAX_DELAY); }
void lvgl_port_unlock(void) { xSemaphoreGive(s_lvgl_mutex); }
