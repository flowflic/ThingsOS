// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
typedef enum {
    THINGSOS_KEYS_LOWER,
    THINGSOS_KEYS_UPPER,
    THINGSOS_KEYS_SYMBOLS
} thingsos_key_mode_t;
#define THINGSOS_IME_COMPOSITION_LIMIT 64
typedef struct {
    char text[1025];
    size_t length, cursor, limit;
    bool secret, reveal, chinese;
    char composition[THINGSOS_IME_COMPOSITION_LIMIT+1];
    size_t candidate_page;
    thingsos_key_mode_t mode;
} thingsos_input_t;
bool thingsos_input_open(thingsos_input_t*, const char*, size_t, bool);
bool thingsos_input_insert(thingsos_input_t*, const char*);
void thingsos_input_backspace(thingsos_input_t*);
void thingsos_input_move(thingsos_input_t*, int);
void thingsos_input_display(const thingsos_input_t*, char*, size_t);
/* 40 character keys, then Shift, symbol mode, space, delete, left, right, show, cancel, confirm. */
const char* thingsos_input_key(const thingsos_input_t*, unsigned);
int thingsos_input_key_hit(int, int);
