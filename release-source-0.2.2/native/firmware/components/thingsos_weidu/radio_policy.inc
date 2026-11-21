/* SPDX-License-Identifier: Apache-2.0 */
/* One coordinator owns the external network claim until its prefetch workers
   have stopped. Keep this policy scoped to that job, including online restore. */
typedef struct {
    wifi_ps_type_t previous;
    bool changed;
} weidu_radio_t;

static bool radio_needs_awake(thingsos_weidu_action_t action, bool connected) {
    return action != WD_DEVICE_LOGOUT && (action != WD_DEVICE_RESTORE || connected);
}

static weidu_radio_t radio_awake(void) {
    weidu_radio_t state = {0};
    esp_err_t error = esp_wifi_get_ps(&state.previous);
    if (error != ESP_OK) {
        ESP_LOGW("ThingsOS_weidu", "Radio policy read failed: %d", error);
        return state;
    }
    error = esp_wifi_set_ps(WIFI_PS_NONE);
    state.changed = error == ESP_OK && state.previous != WIFI_PS_NONE;
    wifi_ps_type_t actual = state.previous;
    esp_err_t readback = esp_wifi_get_ps(&actual);
    int8_t tx_power = 0;
    esp_err_t tx_readback = esp_wifi_get_max_tx_power(&tx_power);
    ESP_LOGI("ThingsOS_weidu",
             "Radio exchange previous=%d set=%d actual=%d readback=%d tx_qdbm=%d tx_readback=%d",
             state.previous, error, readback == ESP_OK ? (int)actual : -1,
             readback, tx_readback == ESP_OK ? (int)tx_power : -1, tx_readback);
    if (error != ESP_OK || readback != ESP_OK || actual != WIFI_PS_NONE)
        ESP_LOGW("ThingsOS_weidu", "Radio awake policy not confirmed");
    if (state.changed) vTaskDelay(pdMS_TO_TICKS(100));
    return state;
}

static void radio_restore(weidu_radio_t* state) {
    if (!state->changed) return;
    esp_err_t error = esp_wifi_set_ps(state->previous);
    wifi_ps_type_t actual = WIFI_PS_NONE;
    esp_err_t readback = esp_wifi_get_ps(&actual);
    ESP_LOGI("ThingsOS_weidu", "Radio restore target=%d set=%d actual=%d readback=%d",
             state->previous, error, readback == ESP_OK ? (int)actual : -1, readback);
    if (error != ESP_OK || readback != ESP_OK || actual != state->previous)
        ESP_LOGW("ThingsOS_weidu", "Radio power policy restore not confirmed");
    state->changed = false;
}
