/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
/* Software-white pixels may still retain physical marks from a prior page. */
static inline bool thingsos_refresh_full_scope(bool clean,bool transition) {
    return clean || transition;
}
static inline bool thingsos_refresh_reader_wave(bool reader,bool initialized,bool transition,
                                               bool clean,bool requested,unsigned mode) {
    return reader && initialized && !clean && !transition && requested && mode==0;
}
/* An intermediate DU may lead into cleanup but must never consume it. */
static inline bool thingsos_refresh_periodic(bool intermediate,unsigned mode,unsigned frames,unsigned every) {
    return !intermediate && (mode==2 || frames+1>=every);
}
/* Reading controls borrow the page waveform without spending its cleanup
   budget. Explicit recovery/first-frame clears remain outside this deferral. */
static inline bool thingsos_refresh_automatic_clean(bool reader_ui,bool maintenance,bool editor,
                                                    bool policy,unsigned mode,unsigned frames,unsigned every) {
    return !reader_ui && (policy || (!maintenance && !editor &&
        thingsos_refresh_periodic(false,mode,frames,every)));
}
static inline bool thingsos_refresh_wave_budget(unsigned mode,unsigned frames,unsigned every) {
    return mode==0 && (frames+2<every || frames+1>=every);
}

/* Balanced final pages erase changed text with grayscale GC16; DU is reserved
   for the user's fast mode and the optional intermediate animation. */
static inline bool thingsos_refresh_reader_quality(bool reader,bool intermediate,unsigned mode) {
    return reader && !intermediate && mode==0;
}
