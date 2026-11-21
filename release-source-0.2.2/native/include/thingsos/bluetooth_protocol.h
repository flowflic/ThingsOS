// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "thingsos/bluetooth.h"
typedef enum {BT_LINE_NONE,BT_LINE_LOCAL,BT_LINE_TX,BT_LINE_SCAN_BEGIN,BT_LINE_SCAN_END,
 BT_LINE_DEVICE,BT_LINE_CONNECTED,BT_LINE_TIMEOUT,BT_LINE_DISCONNECTED} thingsos_bt_line_t;
typedef struct {char bytes[256];size_t used;bool overflow;} thingsos_bt_framer_t;
bool thingsos_bt_address(const char*);
thingsos_bt_line_t thingsos_bt_parse(const char*,thingsos_bt_device_t*);
/* One complete line, including after an overlong line, is consumed per call. */
bool thingsos_bt_feed(thingsos_bt_framer_t*,unsigned char,char out[256]);
bool thingsos_bt_remember(thingsos_bt_status_t*,const thingsos_bt_device_t*);
bool thingsos_bt_forget_record(thingsos_bt_status_t*,const char*);
