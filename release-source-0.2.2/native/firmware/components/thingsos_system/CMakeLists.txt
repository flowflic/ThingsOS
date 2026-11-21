idf_component_register(SRCS "system_monitor_device.c" INCLUDE_DIRS "."
 REQUIRES thingsos_core thingsos_board esp_hw_support esp_timer esp_system esp_partition spi_flash app_update bootloader_support)
