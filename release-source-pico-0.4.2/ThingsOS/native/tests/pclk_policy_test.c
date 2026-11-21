// SPDX-License-Identifier: Apache-2.0
#include "thingsos/pclk_policy.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

int main(void) {
    thingsos_pclk_policy_t p;
    thingsos_pclk_init(&p, 18, 12);
    CHECK(p.current_mhz == 18 && !p.latched && p.recovery_attempts == 0);
    CHECK(!thingsos_pclk_on_frame_ok(&p, 1000, true));

    /* 1st underrun drops to 12 MHz, eligible for bounded recovery. */
    CHECK(thingsos_pclk_on_underrun(&p, 10000) == 12);
    CHECK(p.current_mhz == 12 && !p.latched && p.underruns == 1);
    CHECK(thingsos_pclk_cooldown_remaining_ms(&p, 10000) == THINGSOS_PCLK_COOLDOWN_MS);
    CHECK(thingsos_pclk_cooldown_remaining_ms(&p, 70000) == THINGSOS_PCLK_COOLDOWN_MS - 60000);

    /* Enough frames before cooldown expires must not recover early. */
    for (unsigned i = 0; i < THINGSOS_PCLK_STABLE_FRAMES + 2; ++i)
        CHECK(!thingsos_pclk_on_frame_ok(&p, 10000 + THINGSOS_PCLK_COOLDOWN_MS - 1, true));
    CHECK(p.current_mhz == 12);

    /* Active radio scan (quiet=false) blocks recovery even after cooldown. */
    CHECK(!thingsos_pclk_on_frame_ok(&p, 10000 + THINGSOS_PCLK_COOLDOWN_MS + 5000, false));
    CHECK(p.current_mhz == 12);

    /* Once quiet after cooldown + stable frames, recovers to 18 MHz. */
    CHECK(thingsos_pclk_on_frame_ok(&p, 10000 + THINGSOS_PCLK_COOLDOWN_MS + 6000, true));
    CHECK(p.current_mhz == 18 && p.recovery_attempts == 1 && !p.latched);

    /* Underrun AFTER relapse window is allowed another recovery (up to cap). */
    uint64_t t2 = p.last_recovery_ms + THINGSOS_PCLK_RELAPSE_MS + 1000;
    CHECK(thingsos_pclk_on_underrun(&p, t2) == 12);
    CHECK(!p.latched);
    /* Fewer than STABLE_FRAMES clean frames must not recover even past cooldown. */
    for (unsigned i = 0; i < THINGSOS_PCLK_STABLE_FRAMES - 1; ++i)
        CHECK(!thingsos_pclk_on_frame_ok(&p, t2 + THINGSOS_PCLK_COOLDOWN_MS + 1000 + i, true));
    CHECK(p.current_mhz == 12);
    CHECK(thingsos_pclk_on_frame_ok(&p, t2 + THINGSOS_PCLK_COOLDOWN_MS + 2000, true));
    CHECK(p.current_mhz == 18 && p.recovery_attempts == 2 && !p.latched);

    /* 3rd recovery reaches THINGSOS_PCLK_MAX_RECOVERIES (3). */
    uint64_t t3 = p.last_recovery_ms + THINGSOS_PCLK_RELAPSE_MS + 1000;
    CHECK(thingsos_pclk_on_underrun(&p, t3) == 12);
    CHECK(!p.latched);
    for (unsigned i = 0; i < THINGSOS_PCLK_STABLE_FRAMES - 1; ++i)
        CHECK(!thingsos_pclk_on_frame_ok(&p, t3 + THINGSOS_PCLK_COOLDOWN_MS + i, true));
    CHECK(thingsos_pclk_on_frame_ok(&p, t3 + THINGSOS_PCLK_COOLDOWN_MS + 100, true));
    CHECK(p.current_mhz == 18 && p.recovery_attempts == 3 && !p.latched);

    /* 4th underrun exceeds recovery cap -> permanently latched at 12 MHz. */
    uint64_t t4 = p.last_recovery_ms + THINGSOS_PCLK_RELAPSE_MS + 1000;
    CHECK(thingsos_pclk_on_underrun(&p, t4) == 12);
    CHECK(p.latched && p.current_mhz == 12);
    for (unsigned i = 0; i < 20; ++i)
        CHECK(!thingsos_pclk_on_frame_ok(&p, t4 + THINGSOS_PCLK_COOLDOWN_MS * 10 + i, true));
    CHECK(p.current_mhz == 12 && p.recovery_attempts == 3);

    /* Rapid relapse within RELAPSE_MS immediately latches on 2nd underrun. */
    thingsos_pclk_init(&p, 18, 12);
    CHECK(thingsos_pclk_on_underrun(&p, 5000) == 12);
    for (unsigned i = 0; i < THINGSOS_PCLK_STABLE_FRAMES - 1; ++i)
        CHECK(!thingsos_pclk_on_frame_ok(&p, 5000 + THINGSOS_PCLK_COOLDOWN_MS, true));
    CHECK(thingsos_pclk_on_frame_ok(&p, 5000 + THINGSOS_PCLK_COOLDOWN_MS, true));
    CHECK(p.current_mhz == 18 && p.recovery_attempts == 1);
    CHECK(thingsos_pclk_on_underrun(&p, p.last_recovery_ms + THINGSOS_PCLK_RELAPSE_MS - 1) == 12);
    CHECK(p.latched && p.current_mhz == 12);

    /* Underrun while already at 12 MHz immediately latches. */
    thingsos_pclk_init(&p, 18, 12);
    CHECK(thingsos_pclk_on_underrun(&p, 1000) == 12 && !p.latched);
    CHECK(thingsos_pclk_on_underrun(&p, 2000) == 12 && p.latched);

    puts("pclk_recovery_policy passed");
    return 0;
}
