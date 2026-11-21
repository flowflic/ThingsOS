/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "thingsos/keys.h"
#define THINGSOS_HID_FIELDS 192
#define THINGSOS_HID_REPORTS 16
#define THINGSOS_HID_PRESSED 64
typedef struct {uint16_t offset,page,usage,count;int16_t logical_min;uint8_t size,report;bool variable;} thingsos_hid_field_t;
typedef struct {uint8_t id;uint16_t bits;} thingsos_hid_report_t;
typedef struct {unsigned fields,reports;thingsos_hid_field_t field[THINGSOS_HID_FIELDS];thingsos_hid_report_t report[THINGSOS_HID_REPORTS];} thingsos_hid_layout_t;
typedef struct {uint32_t pressed[THINGSOS_HID_PRESSED];unsigned count;} thingsos_hid_pressed_t;
typedef struct {thingsos_hid_pressed_t report[THINGSOS_HID_REPORTS];uint8_t modifiers;bool caps;} thingsos_hid_decoder_t;
typedef struct {uint16_t page,usage;uint8_t modifiers;bool caps;uint32_t generation;uint64_t time_ms;} thingsos_hid_key_t;
typedef void (*thingsos_hid_emit_t)(uint16_t page,uint16_t usage,uint8_t modifiers,bool caps,void* arg);
bool thingsos_hid_parse(thingsos_hid_layout_t*,const uint8_t*,size_t);
bool thingsos_hid_decode(const thingsos_hid_layout_t*,thingsos_hid_decoder_t*,uint8_t report_id,const uint8_t*,size_t,thingsos_hid_emit_t,void*);
char thingsos_hid_ascii(const thingsos_hid_key_t*);
thingsos_key_action_t thingsos_hid_navigation(const thingsos_hid_key_t*);
