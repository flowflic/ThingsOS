// SPDX-License-Identifier: Apache-2.0
#ifndef THINGSOS_TOUCH_H
#define THINGSOS_TOUCH_H
#include <stdbool.h>
#include <stdint.h>
/* CST836U packet contact count is authoritative. Unused slots contain stale data. */
static inline bool thingsos_touch_primary(const uint8_t raw[15], uint16_t* x, uint16_t* y) {
    unsigned count = raw[2] & 15;
    if (count == 0 || count > 2) return false;
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t* p = raw + 3 + i * 6;
        unsigned event = p[0] >> 6, id = p[2] >> 4;
        if (id > 1 || event == 1 || event == 3) continue;
        *x = ((uint16_t)(p[0] & 15) << 8) | p[1];
        *y = ((uint16_t)(p[2] & 15) << 8) | p[3];
        return true;
    }
    return false;
}
#endif
