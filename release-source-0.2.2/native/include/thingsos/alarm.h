// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define THINGSOS_ALARM_LIMIT 4
#define THINGSOS_ALARM_DAILY 0x7f
#define THINGSOS_ALARM_WEEKDAYS 0x3e
#define THINGSOS_ALARM_WEEKEND 0x41
/* Bit 0 is Sunday. Repeat with days==0 denotes an unused slot. Dates are local
   YYYYMMDD, inclusive; zero bounds mean unrestricted for recurring alarms. */
#define THINGSOS_ALARM_REPEAT 0
#define THINGSOS_ALARM_ONCE 1
typedef struct {
    uint8_t enabled, hour, minute, days, volume, mode, reserved[2];
    char music[192];
    uint32_t last_day, start_day, end_day;
} thingsos_alarm_t;
/* Preserve the exact old on-flash layout for read-only migration. */
typedef struct {
    uint8_t enabled, hour, minute, days, volume, reserved[3];
    char music[192];
    uint32_t last_day;
} thingsos_alarm_v1_t;
bool thingsos_alarm_used(const thingsos_alarm_t* alarm);
bool thingsos_alarm_valid(const thingsos_alarm_t* alarm);
bool thingsos_alarm_from_v1(const thingsos_alarm_v1_t* old,thingsos_alarm_t* alarm);
bool thingsos_alarm_expired(const thingsos_alarm_t* alarm,time_t current);
/* Returns the local YYYYMMDD occurrence, without mutating the record.
   At boot only the current minute is eligible; gaps are capped at five minutes.
   last_day is a high-water mark, including across backwards clock changes. */
uint32_t thingsos_alarm_due(const thingsos_alarm_t* alarm, time_t previous, time_t current);
