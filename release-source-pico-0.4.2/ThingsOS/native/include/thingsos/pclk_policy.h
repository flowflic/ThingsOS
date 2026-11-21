// SPDX-License-Identifier: Apache-2.0
#ifndef THINGSOS_PCLK_POLICY_H
#define THINGSOS_PCLK_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/* Bounded automatic PCLK recovery policy after an LCD line-queue underrun:
 * - Starts at nominal_mhz (18 MHz on Read Pico).
 * - On any underrun, drops immediately to safe_mhz (12 MHz).
 * - Recovers back to nominal_mhz only after BOTH a cooldown period and
 *   consecutive clean frames while the system is quiet (no active radio scan).
 * - If another underrun occurs within the relapse window after recovery, or
 *   once recovery attempts reach the per-boot cap, permanently latches at
 *   safe_mhz until reboot to avoid repeated full-clear redraws. */

#define THINGSOS_PCLK_COOLDOWN_MS 120000ULL
#define THINGSOS_PCLK_RELAPSE_MS 60000ULL
#define THINGSOS_PCLK_STABLE_FRAMES 6u
#define THINGSOS_PCLK_MAX_RECOVERIES 3u

typedef struct {
    unsigned nominal_mhz;
    unsigned safe_mhz;
    unsigned current_mhz;
    unsigned underruns;
    unsigned stable_frames;
    unsigned recovery_attempts;
    uint64_t last_underrun_ms;
    uint64_t last_recovery_ms;
    bool has_underrun;
    bool has_recovered;
    bool latched;
} thingsos_pclk_policy_t;

void thingsos_pclk_init(thingsos_pclk_policy_t* p, unsigned nominal_mhz, unsigned safe_mhz);
unsigned thingsos_pclk_on_underrun(thingsos_pclk_policy_t* p, uint64_t now_ms);
/* Returns true if current_mhz transitioned from safe_mhz back to nominal_mhz. */
bool thingsos_pclk_on_frame_ok(thingsos_pclk_policy_t* p, uint64_t now_ms, bool quiet);
uint64_t thingsos_pclk_cooldown_remaining_ms(const thingsos_pclk_policy_t* p, uint64_t now_ms);

#endif
