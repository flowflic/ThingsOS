idf_component_register(
    SRCS "flomo.c" "secure.c"
    INCLUDE_DIRS "."
    REQUIRES thingsos_core thingsos_net esp_http_server esp_http_client esp-tls nvs_flash mbedtls esp_timer
    EMBED_FILES "flomo.html"
)
