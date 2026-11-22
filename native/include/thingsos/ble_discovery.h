/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "thingsos/bluetooth.h"
#include "thingsos/ble_hid.h"
#define THINGSOS_BLE_FOUND 32
#define THINGSOS_BLE_SAVED 4
typedef struct {thingsos_bt_device_t device;int8_t rssi;uint8_t address_type;bool connectable,complete_name;} thingsos_ble_device_t;
typedef enum {THINGSOS_BLE_IDLE,THINGSOS_BLE_STARTING,THINGSOS_BLE_CONNECTING,THINGSOS_BLE_PAIRING,
 THINGSOS_BLE_DISCOVERING,THINGSOS_BLE_CONNECTED,THINGSOS_BLE_DISCONNECTING,THINGSOS_BLE_ERROR} thingsos_ble_phase_t;
typedef enum {THINGSOS_BLE_PAIR_NONE,THINGSOS_BLE_PAIR_DISPLAY,THINGSOS_BLE_PAIR_CONFIRM,THINGSOS_BLE_PAIR_INPUT} thingsos_ble_pair_t;
typedef struct {unsigned serial,count,saved_count;bool scanning,full,connected,bonded,hid_ready,automatic;
 thingsos_ble_phase_t phase;thingsos_ble_pair_t pairing;uint32_t passkey,generation;
 char message[120];thingsos_ble_device_t current,identity,last,saved[THINGSOS_BLE_SAVED],devices[THINGSOS_BLE_FOUND];} thingsos_ble_status_t;
/* Returns true for discovery/name/capability changes, not RSSI-only samples.
   RSSI is still sampled in the supplied state. Public/random addresses
   remain distinct; scan responses can enrich a prior unnamed advertisement. */
bool thingsos_ble_observe(thingsos_ble_status_t*,const char* address,uint8_t address_type,int8_t rssi,bool connectable,const uint8_t* data,size_t bytes);
bool thingsos_ble_same(const thingsos_ble_device_t*,const thingsos_ble_device_t*);
/* False when only the address is known (the name falls back to the address). */
bool thingsos_ble_named(const thingsos_ble_device_t*);
bool thingsos_ble_remember(thingsos_ble_status_t*,const thingsos_ble_device_t*);
bool thingsos_ble_forget_record(thingsos_ble_status_t*,const thingsos_ble_device_t*);
#ifndef THINGSOS_HOST_UI
void thingsos_ble_init(void);
/* Stop the BLE host/controller before light sleep (false: still starting, retry);
   resume restarts the automatic reconnect it interrupted. */
bool thingsos_ble_suspend(void);
void thingsos_ble_resume(void);
bool thingsos_ble_scan(bool start);
void thingsos_ble_status(thingsos_ble_status_t*);
bool thingsos_ble_connect(const thingsos_ble_device_t*);
bool thingsos_ble_disconnect(void);
bool thingsos_ble_forget(const thingsos_ble_device_t*);
bool thingsos_ble_automatic(bool);
bool thingsos_ble_pair_reply(bool accept,uint32_t passkey);
bool thingsos_ble_input(thingsos_hid_key_t*);
/* Lock-free summary for the memory sampler: 1 running, 2 connected, 4 HID ready. */
unsigned thingsos_ble_flags(void);
#endif
