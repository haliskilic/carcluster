idf_component_register(
    SRCS
        "main.c"
        "board.c"
        "ui.c"
        "icons.c"
        "lvgl_port.c"
        "demo.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_lcd
        esp_psram
        driver
)
