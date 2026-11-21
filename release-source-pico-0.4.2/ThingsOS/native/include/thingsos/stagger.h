/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#define THINGSOS_TURN_BANDS 8
#define THINGSOS_TURN_TICK_US 11090
#define THINGSOS_TURN_BAND_GAP 1
/* Portrait logical X maps to reversed physical Y on this board. */
static inline int thingsos_turn_phase(unsigned row,unsigned rows,bool forward,unsigned tick,unsigned phases,unsigned gap) {
    if(!rows || row>=rows || !phases || !gap)return -1;
    unsigned band=row*THINGSOS_TURN_BANDS/rows;
    if(!forward)band=THINGSOS_TURN_BANDS-1-band;
    unsigned delay=band*gap;
    return tick<delay || tick-delay>=phases ? -1 : (int)(tick-delay);
}
