// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "esp_err.h"
#include "thingsos/network_state.h"
#include "thingsos/release.h"
void thingsos_network_release_state(thingsos_release_state_t*);
bool thingsos_network_release_checking(void);
esp_err_t thingsos_network_release_start(bool install);
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
extern SemaphoreHandle_t thingsos_io_lock;
esp_err_t thingsos_network_start(bool rescue);
void thingsos_network_healthy(void);
esp_err_t thingsos_network_service_poll(void);
const char* thingsos_network_ssid(void);
const char* thingsos_network_password(void);
const char* thingsos_network_token(void);
bool thingsos_network_busy(void);
bool thingsos_network_external_begin(void);
void thingsos_network_external_end(void);
esp_err_t thingsos_network_connect(const char* ssid, const char* password);
esp_err_t thingsos_enter_rescue(void);
typedef struct {
    char ssid[33];
    int rssi;
    bool open;
} thingsos_wifi_result_t;
esp_err_t thingsos_network_scan_start(void);
esp_err_t thingsos_network_scan_poll(thingsos_wifi_result_t*,size_t,size_t*,bool* done);
bool thingsos_network_connected(void);
void thingsos_network_address(bool hotspot,char* out,size_t capacity);
esp_err_t thingsos_network_mode(bool hotspot);
esp_err_t thingsos_network_forget(void);
/* Disconnect STA and suspend automatic retries until the next explicit connect.
   Saved profiles/passwords and the transfer hotspot are retained. */
esp_err_t thingsos_network_disconnect(void);
void thingsos_network_service(bool enabled);
void thingsos_network_auto_time(bool enabled);
void thingsos_network_library(const char* path);

bool thingsos_network_saved(const char* ssid);
esp_err_t thingsos_network_reconnect(const char* ssid);
