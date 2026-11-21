// SPDX-License-Identifier: Apache-2.0
#include "thingsos/guard.h"
#define MAGIC 0x54484731u
void thingsos_guard_healthy(thingsos_boot_guard_t* g) {
    if (!g) return;
    g->magic = MAGIC;
    g->attempts = 0;
    g->complement = ~g->attempts;
}
bool thingsos_guard_begin(thingsos_boot_guard_t* g, bool power_loss) {
    if (!g) return false;
    if (power_loss || g->magic != MAGIC || g->complement != ~g->attempts || g->attempts > 3)
        thingsos_guard_healthy(g);
    if (g->attempts < 3) ++g->attempts;
    g->complement = ~g->attempts;
    return g->attempts >= 3;
}
