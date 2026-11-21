idf_component_register(
    SRCS "metalio_board.c" "metalio_epd_timing.c" "metalio_sd.c" "ft6336u.c"
         "metalio_power.c" "pcf8563.c" "metalio_frontlight.c" "metalio_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES epdiy esp_driver_i2c esp_driver_gpio esp_driver_sdmmc sdmmc fatfs esp_driver_ledc esp_timer esp_driver_uart esp_driver_i2s
    PRIV_REQUIRES esp_rom freertos
)
