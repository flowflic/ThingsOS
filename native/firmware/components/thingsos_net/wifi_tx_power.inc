/* SPDX-License-Identifier: Apache-2.0 */
/* Match the Metalio board's nominal 12 dBm request. The SDK may quantize
   48 quarter-dBm to a lower supported level, so verify an upper bound. */
#define WIFI_TX_POWER_LIMIT_QDBM 48
static esp_err_t wifi_tx_power_limit(void) {
    esp_err_t error = esp_wifi_set_max_tx_power(WIFI_TX_POWER_LIMIT_QDBM);
    if (error != ESP_OK) {
        ESP_LOGE("ThingsOS_network", "WiFi TX limit request failed: %s", esp_err_to_name(error));
        return error;
    }
    int8_t actual = 0;
    error = esp_wifi_get_max_tx_power(&actual);
    if (error != ESP_OK) {
        ESP_LOGE("ThingsOS_network", "WiFi TX limit readback failed: %s", esp_err_to_name(error));
        return error;
    }
    if (actual < 8 || actual > WIFI_TX_POWER_LIMIT_QDBM) {
        ESP_LOGE("ThingsOS_network", "WiFi TX limit not confirmed: request_qdbm=%d actual_qdbm=%d",
                 WIFI_TX_POWER_LIMIT_QDBM, actual);
        return ESP_FAIL;
    }
    ESP_LOGI("ThingsOS_network", "WiFi TX limit request_qdbm=%d actual_qdbm=%d",
             WIFI_TX_POWER_LIMIT_QDBM, actual);
    return ESP_OK;
}
