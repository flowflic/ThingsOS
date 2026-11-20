// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "thingsos/cubox_protocol.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char server[THINGSOS_CUBOX_SERVER_LEN];
    char token[THINGSOS_CUBOX_TOKEN_LEN];
} cubox_config_t;

void cubox_wipe(void* p, size_t n);
bool cubox_config_load(cubox_config_t* out);
bool cubox_config_save(const cubox_config_t* config);
bool cubox_config_clear(void);
