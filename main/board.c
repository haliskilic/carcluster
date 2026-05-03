#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

static const char *TAG = "board";

#define PIN_HSYNC          46
#define PIN_VSYNC          3
#define PIN_DE             5
#define PIN_PCLK           7
/* PCLK 16 MHz: Waveshare resmi + community-proven (ESPHome, paulhamsh, iamfaraz).
 * Bounce buffer + bb_invalidate_cache + same-core LVGL kombinasyonuyla
 * 800×484×~52Hz tear-free scan (Espressif FAQ + benchmark doğrular). */
#define LCD_PIXEL_CLOCK_HZ (16 * 1000 * 1000)

#define I2C_PORT           I2C_NUM_0
#define PIN_I2C_SCL        9
#define PIN_I2C_SDA        8
#define I2C_FREQ_HZ        400000

#define CH422G_ADDR_WR     0x24
/* BIT_* tanımları board.h'de — touch.c da paylaşıyor */

static esp_lcd_panel_handle_t s_panel;
static uint8_t s_ch422g = 0;
static SemaphoreHandle_t s_ch422g_mutex = NULL;

/* CH422G I2C write — shadow byte + bus operasyonu mutex altında.
 * Public API: touch.c TP_RST için, ileride backlight PWM vs. */
void board_ch422g_write(uint8_t set_mask, uint8_t clr_mask)
{
    if (s_ch422g_mutex) xSemaphoreTake(s_ch422g_mutex, portMAX_DELAY);
    s_ch422g |=  set_mask;
    s_ch422g &= ~clr_mask;
    i2c_master_write_to_device(I2C_PORT, CH422G_ADDR_WR, &s_ch422g, 1, pdMS_TO_TICKS(100));
    if (s_ch422g_mutex) xSemaphoreGive(s_ch422g_mutex);
}

/* I²C bus recovery — soft reset (idf.py flash, esp_restart) sırasında bağlı
 * slave (GT911 / CH422G) önceki transaction'da takılı kalmış olabilir. NXP
 * UM10204 bus recovery: SDA released, SCL'i 9 kez toggle et — slave bekleme
 * döngüsünden çıkar — sonra STOP üret.
 *
 * KRİTİK: önce gpio_set_level(HIGH) çağrılır, SONRA pin output yapılır.
 * Aksi halde gpio_config()'in default level=0 değeri pin'i bir mikrosaniye
 * LOW çekiyor — bu kısa pulse slave'leri yanlış START olarak yorumlatıp
 * bus'u daha kötü duruma sokuyor. (V1 fix bunu yapıp touch'ı bozmuştu.) */
static void i2c_bus_recover(void)
{
    /* 1) Output level register'ına önce 1 yaz — pin output yapılınca high gelir */
    gpio_set_level(PIN_I2C_SDA, 1);
    gpio_set_level(PIN_I2C_SCL, 1);

    /* 2) Pull-up + open-drain output mode — external pull-up'lar zaten var,
     *    iç pull-up backup. OD modunda HIGH = high-Z, LOW = aktif sürülür. */
    gpio_set_pull_mode(PIN_I2C_SDA, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_I2C_SCL, GPIO_PULLUP_ONLY);
    gpio_set_direction(PIN_I2C_SDA, GPIO_MODE_OUTPUT_OD);
    gpio_set_direction(PIN_I2C_SCL, GPIO_MODE_OUTPUT_OD);
    esp_rom_delay_us(10);

    /* 3) SCL 9 puls — slave incomplete transaction'dan çıkar (SDA released kalır) */
    for (int i = 0; i < 9; i++) {
        gpio_set_level(PIN_I2C_SCL, 0);
        esp_rom_delay_us(5);
        gpio_set_level(PIN_I2C_SCL, 1);
        esp_rom_delay_us(5);
    }

    /* 4) STOP condition: SDA LOW iken SCL HIGH iken SDA → HIGH transition */
    gpio_set_level(PIN_I2C_SDA, 0);
    esp_rom_delay_us(5);
    gpio_set_level(PIN_I2C_SCL, 1);
    esp_rom_delay_us(5);
    gpio_set_level(PIN_I2C_SDA, 1);
    esp_rom_delay_us(5);

    ESP_LOGI(TAG, "I2C bus recovery done");
}

static void i2c_init(void)
{
    i2c_bus_recover();

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
    board_ch422g_write(BIT_LCD_VDD | BIT_LCD_RST, 0);  vTaskDelay(pdMS_TO_TICKS(20));
    board_ch422g_write(0, BIT_LCD_RST);                 vTaskDelay(pdMS_TO_TICKS(20));
    board_ch422g_write(BIT_LCD_RST, 0);                 vTaskDelay(pdMS_TO_TICKS(120));
    board_ch422g_write(BIT_BL_EN, 0);
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
            /* Waveshare/ESPHome community-proven porches @ 16 MHz:
             * H = pulse 4, back 8,  front 8  → total 820 px
             * V = pulse 4, back 16, front 16 → total 516 lines
             * 16M / (820 × 516) ≈ 38 Hz panel scan, signal integrity sağlam. */
            .hsync_pulse_width = 4, .hsync_back_porch = 8,  .hsync_front_porch = 8,
            .vsync_pulse_width = 4, .vsync_back_porch = 16, .vsync_front_porch = 16,
            .flags.pclk_active_neg = 1,
        },
        .flags.fb_in_psram = 1,
        /* Bounce buffer mode: PSRAM contention'ı azaltır, yüksek PCLK headroom.
         * bb_invalidate_cache: bounce buffer DMA okumadan önce cache invalidate
         * → CPU PSRAM'e yazdığı veri DMA'ya temiz görünür (Espressif gerekli). */
        .flags.bb_invalidate_cache = 1,
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_LOGI(TAG, "RGB panel ready");
}

esp_lcd_panel_handle_t board_get_panel(void) { return s_panel; }
