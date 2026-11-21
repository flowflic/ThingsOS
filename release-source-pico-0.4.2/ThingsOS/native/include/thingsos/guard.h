// SPDX-License-Identifier: Apache-2.0
#ifndef THINGSOS_GUARD_H
#define THINGSOS_GUARD_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint32_t magic, attempts, complement;
} thingsos_boot_guard_t;
bool thingsos_guard_begin(thingsos_boot_guard_t* guard, bool power_loss);
void thingsos_guard_healthy(thingsos_boot_guard_t* guard);
#endif
