// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool thingsos_record_denoise_open(void);
bool thingsos_record_denoise_feed(const int16_t*, size_t, int16_t*, size_t, size_t*);
bool thingsos_record_denoise_finish(int16_t*, size_t, size_t*);
void thingsos_record_denoise_close(void);
