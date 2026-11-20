// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "esp_err.h"
#include "thingsos/network_state.h"
#include "thingsos/release.h"
void thingsos_network_release_state(thingsos_release_state_t*);
bool thingsos_network_release_checking(void);
esp_err_t thingsos_network_release_start(bool install);
/* Connects once to the release host in the background after link-up: the first
   outbound TCP connection after association can be refused for up to ~90 s. */
void thingsos_network_warmup_start(void);
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
/* WiFi radio/mode changes are blocked only by a real transfer or job, not by
   an audio session's own claim (play, pause or record). */
bool thingsos_network_wifi_busy(void);
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
bool thingsos_network_connecting(void);
void thingsos_network_address(bool hotspot,char* out,size_t capacity);
esp_err_t thingsos_network_mode(bool hotspot);
esp_err_t thingsos_network_forget(void);
/* Disconnect STA and suspend automatic retries until the next explicit connect.
   Saved profiles/passwords and the transfer hotspot are retained. */
esp_err_t thingsos_network_disconnect(void);
/* User radio switch. Unlike disconnect, also disables AP and reconnect/NTP. */
esp_err_t thingsos_network_radio(bool enabled);
bool thingsos_network_radio_enabled(void);
/* Only regular uploads can be cancelled; OTA is never interrupted here. */
bool thingsos_network_cancel_upload(void);
void thingsos_network_service(bool enabled);
void thingsos_network_auto_time(bool enabled);
void thingsos_network_library(const char* path);

bool thingsos_network_saved(const char* ssid);
esp_err_t thingsos_network_reconnect(const char* ssid);
/* Coordinate the board's radio stop/start with the STA retry budget. Wake is
   asynchronous and preserves a user radio-off or explicit disconnect. */
void thingsos_network_sleep(void);
esp_err_t thingsos_network_wake(void);
