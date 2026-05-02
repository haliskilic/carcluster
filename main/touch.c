#include "touch.h"
#include "board.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "touch";

#define I2C_PORT       I2C_NUM_0
#define GT911_ADDR     0x5D
#define GT911_INT_PIN  GPIO_NUM_4

#define REG_PROD_ID    0x8140
#define REG_STATUS     0x814E
#define REG_POINT1     0x814F   /* track id, x_lo, x_hi, y_lo, y_hi, size_lo, size_hi */

static struct {
    int  x, y;
    bool pressed;
} s_state = { 0, 0, false };

static SemaphoreHandle_t s_mutex;

static esp_err_t gt911_read(uint16_t reg, uint8_t *data, size_t len)
{
    uint8_t reg_buf[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    return i2c_master_write_read_device(I2C_PORT, GT911_ADDR,
                                        reg_buf, 2, data, len,
                                        pdMS_TO_TICKS(100));
}

static esp_err_t gt911_write_u8(uint16_t reg, uint8_t val)
{
    uint8_t buf[3] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), val };
    return i2c_master_write_to_device(I2C_PORT, GT911_ADDR, buf, 3,
                                      pdMS_TO_TICKS(100));
}

static void touch_poll_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(20));   /* 50 Hz */

        uint8_t status;
        if (gt911_read(REG_STATUS, &status, 1) != ESP_OK) continue;

        bool ready    = (status & 0x80) != 0;
        int  n_points = status & 0x0F;

        if (!ready) continue;

        if (n_points >= 1) {
            uint8_t pt[7];
            if (gt911_read(REG_POINT1, pt, 7) == ESP_OK) {
                int x = pt[1] | (pt[2] << 8);
                int y = pt[3] | (pt[4] << 8);
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                s_state.x = x;
                s_state.y = y;
                s_state.pressed = true;
                xSemaphoreGive(s_mutex);
            }
        } else {
            /* status valid but no points → release */
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_state.pressed = false;
            xSemaphoreGive(s_mutex);
        }

        /* Status registerini sıfırla — GT911 protokolü gereği (sonraki sample için) */
        gt911_write_u8(REG_STATUS, 0);
    }
}

void touch_init(void)
{
    /* DEBUG: STEP 2 — mutex + GPIO4 + CH422G TP_RST cycle */
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "touch mutex create failed");
        abort();
    }

    /* GPIO4 (GT911 INT) output low — i2c addr 0x5D selection */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << GT911_INT_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(GT911_INT_PIN, 0);

    /* CH422G TP_RST low → high cycle */
    board_ch422g_write(0, BIT_TP_RST);
    vTaskDelay(pdMS_TO_TICKS(10));
    board_ch422g_write(BIT_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* GPIO4'ü INPUT'a çevirmek V1.2'de display'i kapatıyor (muhtemelen
     * panel/backlight'la görünmeyen bir bağlantı). Polling-based touch read
     * için GT911 INT pin'ine ihtiyacımız yok — GPIO4 OUTPUT LOW kalsın. */

    /* Probe GT911 — I2C okuyabiliyor muyuz? */
    uint8_t prod_id[4] = {0};
    if (gt911_read(REG_PROD_ID, prod_id, 4) == ESP_OK) {
        ESP_LOGI(TAG, "GT911 ready, ID: %c%c%c%c (%02X %02X %02X %02X)",
                 prod_id[0], prod_id[1], prod_id[2], prod_id[3],
                 prod_id[0], prod_id[1], prod_id[2], prod_id[3]);
    } else {
        ESP_LOGW(TAG, "GT911 probe failed — touch may not work");
    }

    xTaskCreatePinnedToCore(touch_poll_task, "touch", 3072, NULL, 2, NULL, 0);
}

void touch_get_state(int *x, int *y, bool *pressed)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *x = s_state.x;
    *y = s_state.y;
    *pressed = s_state.pressed;
    xSemaphoreGive(s_mutex);
}
