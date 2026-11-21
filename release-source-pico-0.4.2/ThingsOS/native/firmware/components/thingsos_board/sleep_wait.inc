// SPDX-License-Identifier: Apache-2.0
void thingsos_board_sleep_wait(void) {
    /* Manual light sleep bypasses IDF's USB power-management lock and cuts the
       S3 USB Serial/JTAG PHY. Keep host enumeration while locked on a computer;
       panel, touch and WiFi were already suspended by board_suspend(). A wall
       charger has no USB SOF and does not prevent battery-saving light sleep. */
    if(usb_serial_jtag_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(200));
        return;
    }
    /* Bounded timer wake lets the PMU distinguish SHORT from LONG without touch wake. */
    esp_sleep_enable_timer_wakeup(200000);
    esp_err_t err=esp_light_sleep_start();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    if(err!=ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));
}
