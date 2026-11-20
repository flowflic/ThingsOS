// SPDX-License-Identifier: Apache-2.0
void thingsos_board_sleep_wait(void) {
    /* Keep host enumeration while locked on a computer: manual light sleep
       would cut the USB Serial/JTAG PHY. Panel, touch, frontlight and WiFi were
       already suspended by board_suspend(). A wall charger has no USB SOF. */
    if(usb_serial_jtag_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(200));
        return;
    }
    /* The power key reaches the TCA9555, whose INT on GPIO2 falls on any input
       change; that level wakes light sleep. The 2 s timer bounds a missed edge
       and lets the charger task keep its undervoltage watch. Holding the I2C
       lock keeps every transaction outside the sleep window. */
    if(!metalio_i2c_lock(200)) {
        vTaskDelay(pdMS_TO_TICKS(200));
        return;
    }
    gpio_wakeup_enable(METALIO_IOE_INT_GPIO,GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup(2000000);
    esp_err_t err=esp_light_sleep_start();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    gpio_wakeup_disable(METALIO_IOE_INT_GPIO);
    metalio_i2c_unlock();
    /* Timer wakeups occur every two seconds. Keep a minute heartbeat and all
       non-timer/error transitions, rather than flooding the bounded journal. */
    static int64_t logged_at;
    static esp_err_t last_sleep_error;
    int64_t diagnostic_now=esp_timer_get_time();
    uint32_t causes=esp_sleep_get_wakeup_causes();
    if(diagnostic_now-logged_at>=60000000 || err!=last_sleep_error ||
       (causes & ~(1u<<ESP_SLEEP_WAKEUP_TIMER))) {
        thingsos_diag_event(DIAG_WAKE,1,causes,0,err,0);
        logged_at=diagnostic_now;last_sleep_error=err;
    }
    if(err!=ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));
}
