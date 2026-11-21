// SPDX-License-Identifier: Apache-2.0
#include "thingsos/pclk_policy.h"
#include <string.h>

void thingsos_pclk_init(thingsos_pclk_policy_t* p, unsigned nominal_mhz, unsigned safe_mhz) {
    if (!p) return;
    memset(p, 0, sizeof(*p));
    if (safe_mhz > nominal_mhz) safe_mhz = nominal_mhz;
    p->nominal_mhz = nominal_mhz;
    p->safe_mhz = safe_mhz;
    p->current_mhz = nominal_mhz;
}

unsigned thingsos_pclk_on_underrun(thingsos_pclk_policy_t* p, uint64_t now_ms) {
    if (!p) return 0;
    ++p->underruns;
    p->stable_frames = 0;
    bool relapse = p->has_recovered && now_ms >= p->last_recovery_ms &&
                   (now_ms - p->last_recovery_ms) < THINGSOS_PCLK_RELAPSE_MS;
    if (p->current_mhz <= p->safe_mhz || relapse ||
        p->recovery_attempts >= THINGSOS_PCLK_MAX_RECOVERIES) {
        p->latched = true;
    }
    p->current_mhz = p->safe_mhz;
    p->last_underrun_ms = now_ms;
    p->has_underrun = true;
    return p->current_mhz;
}

bool thingsos_pclk_on_frame_ok(thingsos_pclk_policy_t* p, uint64_t now_ms, bool quiet) {
    if (!p || p->current_mhz >= p->nominal_mhz || p->latched) return false;
    if (p->stable_frames < THINGSOS_PCLK_STABLE_FRAMES) ++p->stable_frames;
    if (!quiet || !p->has_underrun) return false;
    if (p->stable_frames < THINGSOS_PCLK_STABLE_FRAMES) return false;
    if (now_ms < p->last_underrun_ms || (now_ms - p->last_underrun_ms) < THINGSOS_PCLK_COOLDOWN_MS)
        return false;
    if (p->recovery_attempts >= THINGSOS_PCLK_MAX_RECOVERIES) {
        p->latched = true;
        return false;
    }
    p->current_mhz = p->nominal_mhz;
    p->stable_frames = 0;
    ++p->recovery_attempts;
    p->last_recovery_ms = now_ms;
    p->has_recovered = true;
    return true;
}

uint64_t thingsos_pclk_cooldown_remaining_ms(const thingsos_pclk_policy_t* p, uint64_t now_ms) {
    if (!p || p->current_mhz >= p->nominal_mhz || p->latched || !p->has_underrun) return 0;
    if (now_ms <= p->last_underrun_ms) return THINGSOS_PCLK_COOLDOWN_MS;
    uint64_t elapsed = now_ms - p->last_underrun_ms;
    return elapsed >= THINGSOS_PCLK_COOLDOWN_MS ? 0 : THINGSOS_PCLK_COOLDOWN_MS - elapsed;
}
