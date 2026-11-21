idf_component_register(SRCS "ima.c" "secure.c" "../../../src/ima_protocol.c"
 INCLUDE_DIRS "../../../include" PRIV_INCLUDE_DIRS "../../../vendor/cjson"
 REQUIRES thingsos_net thingsos_audio thingsos_board thingsos_libs esp_http_client esp_http_server esp_timer esp_psram nvs_flash mbedtls)
target_add_binary_data(${COMPONENT_LIB} "ima.html" TEXT)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-misleading-indentation -Werror=frame-larger-than=4096 -fstack-usage)
