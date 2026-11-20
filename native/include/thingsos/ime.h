// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "thingsos/input.h"
#define THINGSOS_IME_CANDIDATE_BYTES 25
#define THINGSOS_IME_PAGE_SIZE 3
void thingsos_input_clear(thingsos_input_t*);
void thingsos_input_cursor(thingsos_input_t*, size_t character);
bool thingsos_ime_letter(thingsos_input_t*, char);
void thingsos_ime_backspace(thingsos_input_t*);
size_t thingsos_ime_count(const thingsos_input_t*);
bool thingsos_ime_candidate(const thingsos_input_t*, size_t, char[THINGSOS_IME_CANDIDATE_BYTES]);
bool thingsos_ime_commit(thingsos_input_t*, size_t);
void thingsos_ime_page(thingsos_input_t*, int);
