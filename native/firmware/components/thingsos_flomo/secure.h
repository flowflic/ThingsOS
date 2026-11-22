// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "thingsos/flomo_protocol.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char token[THINGSOS_FLOMO_TOKEN_LEN];
} flomo_config_t;

void flomo_wipe(void* p, size_t n);
bool flomo_config_load(flomo_config_t* out);
bool flomo_config_save(const flomo_config_t* config);
bool flomo_config_clear(void);
