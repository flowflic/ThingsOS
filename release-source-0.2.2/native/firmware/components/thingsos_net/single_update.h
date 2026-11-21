// SPDX-License-Identifier: Apache-2.0
#pragma once
#ifndef THINGSOS_SINGLE_TEST
#include "esp_err.h"
#else
typedef int esp_err_t;
#endif
#include <stddef.h>
#include <stdint.h>
#define THINGSOS_SINGLE_CAPACITY 0xae0000u
#ifndef THINGSOS_UPDATE_FILE
#define THINGSOS_UPDATE_FILE "/sdcard/.thingsos/update.bin"
#endif
#define THINGSOS_UPDATE_PART "/sdcard/.thingsos/update.part"
/* Caller holds the SD/display ownership lock. Only commits a verified SD
   package and selects rescue; never erases the running application. */
esp_err_t thingsos_single_stage(size_t bytes,const uint8_t hash[32],const char* version);
/* Rescue main task only, before networking or panel scan. Missing/invalid
   media leaves rescue selected. Intent persists until successful install. */
esp_err_t thingsos_single_install(void);
void thingsos_single_clear_intent(void);
