idf_component_register(SRCS "main.c" "../../vendor/qrcode/qrcodegen.c" INCLUDE_DIRS "." "../../vendor/qrcode" EMBED_FILES "assets/keyboard-sans.ttf" REQUIRES thingsos_core thingsos_board thingsos_net thingsos_weidu thingsos_audio thingsos_bluetooth thingsos_ble thingsos_ima thingsos_cubox thingsos_flomo thingsos_system thingsos_libs app_update esp_timer nvs_flash esp_psram mbedtls)

# Chapters must stay off the 32 KiB UI task stack.
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror=frame-larger-than=4096 -fstack-usage)
