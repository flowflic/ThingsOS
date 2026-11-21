idf_component_register(SRCS "network.c" "single_update.c" INCLUDE_DIRS "." REQUIRES esp_http_client esp-tls esp_wifi esp_netif esp_event esp_http_server app_update esp_app_format nvs_flash thingsos_libs mbedtls fatfs thingsos_core thingsos_board esp_timer)
target_add_binary_data(${COMPONENT_LIB} "web.html" TEXT)
target_add_binary_data(${COMPONENT_LIB} "web_sha256.js" TEXT)

target_add_binary_data(${COMPONENT_LIB} "web_image.js" TEXT)
