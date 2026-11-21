idf_component_register(SRCS "ble_scan.c" "../../../src/ble_discovery.c" "../../../src/ble_hid.c"
 INCLUDE_DIRS "../../../include" REQUIRES bt thingsos_bluetooth esp_timer esp_rom nvs_flash esp_psram)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-misleading-indentation)
