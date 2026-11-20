/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
static inline bool thingsos_refresh_controls_low_flash(bool requested,bool initialized,bool explicit_clean,bool sleep_clear,bool text_ready,unsigned mode){
    return requested&&initialized&&!explicit_clean&&!sleep_clear&&(mode==1||text_ready);
}
/* UI navigation uses reader-style full-pixel white maintenance. Explicit and
   periodic cleans, narrow controls and status updates retain their own paths. */
static inline bool thingsos_refresh_ui_low_flash(bool reader,unsigned keyboard,bool status,bool clean,unsigned mode,bool text_ready){
    return !reader&&!keyboard&&!status&&!clean&&(mode==1||(mode==0&&text_ready));
}
/* Balanced UI frames must use the black-to-black corrected table, exactly like
   an ordinary reader turn: unchanged black icons and labels are otherwise
   held and come back with uneven ink. Fast mode is monochrome DU and stays. */
static inline bool thingsos_refresh_ui_corrected(unsigned mode,bool ready){
    return mode==0 && ready;
}
/* Home-tab cleanup must reach the next full UI frame, never a narrow timer or
   keyboard/reader frame. A failed presentation leaves the request pending. */
static inline bool thingsos_refresh_ui_cleanup(bool pending,bool reader,unsigned keyboard,bool status){
    return pending&&!reader&&!keyboard&&!status;
}
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
/* The on-screen keyboard drives one DU frame per keystroke; timing a GC16
   against those frames puts a full-screen flash inside a typing run (clear
   mode would flash on every key). The timed clean resumes once the keyboard
   is gone, and the application cleans the background after a burst goes idle. */
static inline bool thingsos_refresh_periodic_clean(bool keyboard,unsigned mode,unsigned frames,unsigned every) {
    return !keyboard && thingsos_refresh_periodic(false,mode,frames,every);
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
