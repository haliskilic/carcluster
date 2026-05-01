#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/i2c.h"

static const char *TAG = "board";

#define PIN_HSYNC          46
#define PIN_VSYNC          3
#define PIN_DE             5
#define PIN_PCLK           7
#define LCD_PIXEL_CLOCK_HZ (12 * 1000 * 1000)

#define I2C_PORT           I2C_NUM_0
#define PIN_I2C_SCL        9
#define PIN_I2C_SDA        8
#define I2C_FREQ_HZ        400000

#define CH422G_ADDR_WR     0x24
#define BIT_BL_EN   (1 << 2)
#define BIT_LCD_RST (1 << 3)
#define BIT_LCD_VDD (1 << 6)

static esp_lcd_panel_handle_t s_panel;
static uint8_t s_ch422g = 0;
static SemaphoreHandle_t s_ch422g_mutex = NULL;

/* CH422G I2C write — shadow byte + bus operasyonu mutex altında.
 * İleride başka task (touch UI / backlight PWM) bu yazıcıya erişirse race
 * koruma garantili. Şu anda sadece board_init kullanıyor ama defensive. */
static void ch422g_write(uint8_t set_mask, uint8_t clr_mask)
{
    if (s_ch422g_mutex) xSemaphoreTake(s_ch422g_mutex, portMAX_DELAY);
    s_ch422g |=  set_mask;
    s_ch422g &= ~clr_mask;
    i2c_master_write_to_device(I2C_PORT, CH422G_ADDR_WR, &s_ch422g, 1, pdMS_TO_TICKS(100));
    if (s_ch422g_mutex) xSemaphoreGive(s_ch422g_mutex);
}

static void i2c_init(void)
{
    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_I2C_SDA, .scl_io_num = PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE, .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &cfg));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));
}

void board_init(void)
{
    /* I2C + CH422G + LCD reset + backlight */
    s_ch422g_mutex = xSemaphoreCreateMutex();
    if (!s_ch422g_mutex) {
        ESP_LOGE(TAG, "ch422g mutex create failed");
        abort();
    }
    i2c_init();
    ch422g_write(BIT_LCD_VDD | BIT_LCD_RST, 0);  vTaskDelay(pdMS_TO_TICKS(20));
    ch422g_write(0, BIT_LCD_RST);                 vTaskDelay(pdMS_TO_TICKS(20));
    ch422g_write(BIT_LCD_RST, 0);                 vTaskDelay(pdMS_TO_TICKS(120));
    ch422g_write(BIT_BL_EN, 0);
    ESP_LOGI(TAG, "CH422G + LCD power + backlight OK");

    /* RGB panel — 800x480 16-bit, ST7262 */
    esp_lcd_rgb_panel_config_t cfg = {
        .data_width = 16, .bits_per_pixel = 16,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .psram_trans_align = 64,
        .num_fbs = 2,
        .bounce_buffer_size_px = LCD_H_RES * 10,
        .hsync_gpio_num = PIN_HSYNC, .vsync_gpio_num = PIN_VSYNC,
        .de_gpio_num = PIN_DE,       .pclk_gpio_num = PIN_PCLK,
        .disp_gpio_num = -1,
        .data_gpio_nums = {14,38,18,17,10,39,0,45,48,47,21,1,2,42,41,40},
        .timings = {
            .pclk_hz = LCD_PIXEL_CLOCK_HZ,
            .h_res = LCD_H_RES, .v_res = LCD_V_RES,
            /* Minimum porches — panel scan total piksel azalır → FPS yükselir.
             * 12MHz / (803 × 483) ≈ 31 fps (önceki 29 fps'ten) */
            .hsync_pulse_width = 1, .hsync_back_porch = 4, .hsync_front_porch = 4,
            .vsync_pulse_width = 1, .vsync_back_porch = 4, .vsync_front_porch = 4,
            .flags.pclk_active_neg = 1,
        },
        .flags.fb_in_psram = 1,
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_LOGI(TAG, "RGB panel ready");
}

esp_lcd_panel_handle_t board_get_panel(void) { return s_panel; }
