idf_component_register(SRCS "weidu_device.c" "network_probe.c" INCLUDE_DIRS "." REQUIRES thingsos_core thingsos_libs thingsos_net lwip
 esp_http_client esp-tls tcp_transport esp_timer esp_psram esp_wifi nvs_flash mbedtls thingsos_board
 EMBED_TXTFILES "certs/weread_root.pem")

# Keep stack-size evidence separate from on-device high-water measurements.
target_compile_options(${COMPONENT_LIB} PRIVATE -fstack-usage)
