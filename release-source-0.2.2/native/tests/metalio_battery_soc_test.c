// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <stdlib.h>
#include "../metalio/components/metalio_bsp/battery_soc.inc"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define SEC(n) ((int64_t)(n) * 1000000LL)

static battery_soc_t fresh(void) {
    battery_soc_t s = {0};
    s.displayed = -1;
    return s;
}

/* Feed one sample per second from t0 for count seconds; return last level. */
static int run(battery_soc_t* s, int64_t* t, int count, uint16_t mv, int16_t ma) {
    int level = -1;
    for (int i = 0; i < count; ++i, *t += SEC(1)) level = battery_soc_update(s, mv, ma, *t);
    return level;
}

int main(void) {
    /* Measured discharge curve: table points map exactly, ends clamp. */
    CHECK(battery_soc_curve(3200, false) == 0.0f);
    CHECK(battery_soc_curve(3300, false) == 0.0f);
    CHECK(battery_soc_curve(3702, false) == 50.0f);
    CHECK(battery_soc_curve(4021, false) == 90.0f);
    CHECK(battery_soc_curve(4211, false) == 100.0f);
    CHECK(battery_soc_curve(4300, false) == 100.0f);
    for (uint16_t mv = 3300; mv < 4300; ++mv)
        CHECK(battery_soc_curve(mv + 1, false) >= battery_soc_curve(mv, false));
    /* Charging keeps the linear 3.30..4.33 V estimate. */
    CHECK(battery_soc_curve(3815, true) == 50.0f);
    CHECK(battery_soc_curve(4330, true) == 100.0f);

    /* First valid sample sets the display directly (after the 3 s settle in the BSP). */
    int64_t t = SEC(10);
    battery_soc_t s = fresh();
    CHECK(battery_soc_update(&s, 3702, -200, t) == 50);

    /* Discharging never rises; a load dip moves at most 1% per 30 s. */
    t += SEC(1);
    CHECK(run(&s, &t, 120, 3800, -200) == 50);
    battery_soc_t d = fresh();
    t = SEC(10);
    battery_soc_update(&d, 3702, -200, t);
    t += SEC(1);
    int level = run(&d, &t, 29, 3600, -400);
    CHECK(level == 50);
    level = run(&d, &t, 2, 3600, -400);
    CHECK(level == 49);
    level = run(&d, &t, 30, 3600, -400);
    CHECK(level == 48);

    /* Plugging in switches curves: the filter restarts, display is kept,
       and the step timer restarts so nothing moves for 30 s. */
    battery_soc_t c = fresh();
    t = SEC(10);
    CHECK(battery_soc_update(&c, 3702, -200, t) == 50);
    t += SEC(1);
    CHECK(battery_soc_update(&c, 4100, 900, t) == 50);
    CHECK(c.filter[0] == battery_soc_curve(4100, true));
    t += SEC(1);
    CHECK(run(&c, &t, 28, 4100, 900) == 50);
    t += SEC(1);
    CHECK(run(&c, &t, 2, 4100, 900) == 51);
    /* Unplugging back to discharge never raises the display. */
    CHECK(run(&c, &t, 60, 3900, -150) <= 51);

    /* Full: 99% until 4.33 V held 30 s, then 100%; latch survives reset
       (standby) while the voltage stays at or above the exit threshold. */
    battery_soc_t f = fresh();
    t = SEC(10);
    battery_soc_update(&f, 4250, 100, t);
    t += SEC(1);
    level = run(&f, &t, 600, 4335, 50);
    CHECK(level == 100 && f.full_latched);
    battery_soc_reset(&f);
    CHECK(f.displayed == -1 && f.full_latched);
    CHECK(battery_soc_update(&f, 4290, -100, t) == 100);
    /* Below the exit threshold the latch clears and the curve takes over. */
    battery_soc_reset(&f);
    t += SEC(1);
    CHECK(battery_soc_update(&f, 4100, -100, t) < 100 && !f.full_latched);

    /* A sampling gap restarts full confirmation. */
    battery_soc_t g = fresh();
    t = SEC(10);
    run(&g, &t, 20, 4335, 50);
    t += SEC(5);
    run(&g, &t, 20, 4335, 50);
    CHECK(!g.full_latched);
    run(&g, &t, 15, 4335, 50);
    CHECK(g.full_latched);

    /* Wake recalibration may cross several steps at once. */
    battery_soc_t w = fresh();
    t = SEC(10);
    battery_soc_update(&w, 3919, -100, t);
    CHECK(w.displayed == 80);
    battery_soc_reset(&w);
    t += SEC(3600);
    CHECK(battery_soc_update(&w, 3663, -100, t) == 40);

    puts("Metalio battery SOC: curves, settle, steps, curve switch, full latch, wake passed");
    return 0;
}
