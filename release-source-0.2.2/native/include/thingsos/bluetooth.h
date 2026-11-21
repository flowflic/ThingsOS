// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define THINGSOS_BT_SAVED 4
#define THINGSOS_BT_FOUND 32
typedef struct {char address[13],name[96];} thingsos_bt_device_t;
/* BH audio wire addresses are least-significant byte first. Do not use the
   display address in module commands or apply this conversion to BLE. */
void thingsos_bt_display_address(const thingsos_bt_device_t*,char out[18]);
const char* thingsos_bt_display_name(const thingsos_bt_device_t*);
typedef enum {THINGSOS_BT_OFF,THINGSOS_BT_STARTING,THINGSOS_BT_READY,THINGSOS_BT_SCANNING,
 THINGSOS_BT_CONNECTING,THINGSOS_BT_CONNECTED,THINGSOS_BT_ERROR} thingsos_bt_phase_t;
typedef struct {bool enabled,automatic,connected,busy;unsigned serial,saved_count,found_count;
 thingsos_bt_phase_t phase;thingsos_bt_device_t saved[THINGSOS_BT_SAVED],found[THINGSOS_BT_FOUND],current;
 char last[13],message[160];} thingsos_bt_status_t;
#ifndef THINGSOS_HOST_UI
void thingsos_bluetooth_init(void);
void thingsos_bluetooth_status(thingsos_bt_status_t*);
bool thingsos_bluetooth_power(bool);
bool thingsos_bluetooth_scan(bool);
bool thingsos_bluetooth_connect(const thingsos_bt_device_t*);
bool thingsos_bluetooth_disconnect(void);
bool thingsos_bluetooth_forget(const char* address);
bool thingsos_bluetooth_automatic(bool);
bool thingsos_bluetooth_enabled(void);
bool thingsos_bluetooth_connected(void);
/* A recording needs the module's local-codec mode; it may start while Bluetooth
   is enabled but idle, and must wait for an active audio link to end. */
bool thingsos_bluetooth_audio_ready(bool microphone);
/* The audio path reports link loss when the module stops clocking I2S in
   wireless mode, which is what a powered-off headset looks like. The service
   applies it on its own task, so the module state keeps a single owner. */
void thingsos_bluetooth_link_lost(void);
#endif
