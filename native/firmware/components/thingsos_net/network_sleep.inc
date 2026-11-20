// SPDX-License-Identifier: Apache-2.0
void thingsos_network_sleep(void) {
    /* The board stops the radio next. That deliberate disconnect must not
       consume retries, and time spent asleep must not expire a connect job. */
    if (wifi_control) xSemaphoreTake(wifi_control, portMAX_DELAY);
    wifi_suspended = true;
    wifi_station_cancel();
    portENTER_CRITICAL(&state_guard);
    if (scan_active) { scan_ready = true; scan_status = ESP_FAIL; }
    portEXIT_CRITICAL(&state_guard);
    if (wifi_control) xSemaphoreGive(wifi_control);
}
esp_err_t thingsos_network_wake(void) {
    wifi_suspended = false;
    if (!network_ready) return ESP_OK;
    radio_up = false;
    bool reconnect = radio_allowed && !user_disconnected && reconnect_enabled && saved_ssid[0];
    if (rescue_mode) {
        esp_err_t error = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (error != ESP_OK) return error;
    } else if (!reconnect) {
        esp_err_t error = esp_wifi_stop();
        if (error == ESP_OK || error == ESP_ERR_WIFI_NOT_STARTED) return ESP_OK;
        ESP_LOGW("ThingsOS_network", "WiFi wake stop failed: %s", esp_err_to_name(error));
        return error;
    }
    esp_err_t error = wifi_radio_start();
    if (error != ESP_OK) {
        snprintf(wifi_failure, sizeof(wifi_failure), "WiFi 唤醒失败，请在设置中重试");
        return error;
    }
    if (reconnect) {
        if (wifi_station_has_address()) {
            /* A failed display suspend can leave the driver connected. */
            portENTER_CRITICAL(&state_guard);
            station_join = (wifi_join_t){.associated = true, .has_ip = true};
            portEXIT_CRITICAL(&state_guard);
        } else wifi_station_request();
        ESP_LOGI("ThingsOS_network", "WiFi wake: saved station enabled");
    }
    return ESP_OK;
}
