// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>

bool thingsos_is_webp(const unsigned char*, size_t);
bool thingsos_webp_info(const unsigned char*, size_t, unsigned*, unsigned*);
bool thingsos_decode_webp(const unsigned char*, size_t, unsigned, unsigned,
                          unsigned char**, unsigned*, unsigned*);
