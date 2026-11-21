// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
enum { LOCK_PIN_DIGITS=6, LOCK_PIN_ITERATIONS=10000 };
enum { POWER_PIN_LEFT=36, POWER_PIN_TOP=550, POWER_PIN_WIDTH=612, POWER_PIN_HEIGHT=135 };
typedef enum { PIN_SETUP, PIN_CHANGE, PIN_DISABLE, PIN_UNLOCK } lock_pin_purpose_t;
typedef enum { PIN_VERIFY, PIN_NEW, PIN_CONFIRM } lock_pin_stage_t;
/* One NVS blob owns enabled state and verifier together. Never store the PIN. */
typedef struct {
    uint32_t magic, version, iterations;
    uint8_t enabled, reserved[3], salt[16], verifier[32];
} lock_pin_record_t;
typedef struct {
    lock_pin_record_t record;
    bool enabled, required, fault, verified;
    lock_pin_purpose_t purpose;
    lock_pin_stage_t stage;
    char entry[LOCK_PIN_DIGITS+1], draft[LOCK_PIN_DIGITS+1], notice[96];
    unsigned length;
} lock_pin_state_t;
