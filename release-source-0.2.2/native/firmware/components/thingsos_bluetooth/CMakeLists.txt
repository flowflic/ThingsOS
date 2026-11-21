idf_component_register(SRCS "bluetooth.c" "../../../src/bluetooth_protocol.c"
 INCLUDE_DIRS "../../../include"
 REQUIRES metalio_bsp thingsos_audio nvs_flash esp_timer esp_rom)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-misleading-indentation)
