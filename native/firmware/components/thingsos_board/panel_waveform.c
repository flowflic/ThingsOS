// SPDX-License-Identifier: Apache-2.0
/* Vendor-aligned panel waveform for the Metalio E-Ink4-Plus.
 *
 * The template tables in `e0470_epaper_waveform` ship two complete sets:
 *   E0470_WAVEFORM       trimmed GC16 36 / GL16 37 + threshold DU 20
 *   E0470_FULL_WAVEFORM  full GC16 48 / GL16 48 + source DU 20
 *
 * The vendor 2.0.76 firmware keeps the untrimmed GC16/GL16 and replaces the
 * source DU with the threshold one, so this module assembles that same mode
 * set from the two exported waves instead of editing the reference tables.
 * `EpdWaveformMode` structs are read-only and owned by the component, so the
 * assembled wave only needs its own mode pointer array. */
#include "panel_waveform.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "e0470_epaper_waveform.h"
#include "epdiy.h"
#include "thingsos/refresh_mask.h"

static const EpdWaveformMode* s_modes[3];
static EpdWaveform s_waveform;
static bool s_backed;
static const EpdWaveformMode* s_turn_modes[3];
static EpdWaveform s_turn_waveform;
static bool s_turn_backed;
static uint8_t s_fast_luts[THINGSOS_PANEL_DU_PHASES * 64];
static EpdWaveformPhases s_fast_phases;
static const EpdWaveformPhases* s_fast_ranges[1];
static EpdWaveformMode s_fast_mode;
static const EpdWaveformMode* s_fast_modes[3];
static EpdWaveform s_fast_waveform;
static bool s_fast_ready;

bool thingsos_panel_text_wave(const uint8_t* source,unsigned phases,uint8_t* target,size_t bytes) {
    return thingsos_refresh_text_wave(source,phases,target,bytes);
}

static bool reader_black_correction(const uint8_t* source,unsigned phases,uint8_t* target,size_t bytes) {
    if(!thingsos_panel_text_wave(source,phases,target,bytes))return false;
    /* Only (to=0,from=0): retain this source table's complete correction,
       including erase and trailing holds. All other transitions keep the
       existing low-flash view, including its single stable-white tick.
       The hold view skips black pixels that were black before too; the page
       still rasterises them, so they must keep a drive action or one glyph
       shows uneven ink between re-inked and untouched edges. */
    for(unsigned p=0;p<phases;++p) {
        size_t at=(size_t)p*64;
        target[at]=(target[at]&0x3f)|(source[at]&0xc0);
    }
    return true;
}

bool thingsos_panel_reader_text_wave(const uint8_t* source,unsigned phases,uint8_t* target,size_t bytes) {
    return phases==THINGSOS_PANEL_TURN_PHASES && reader_black_correction(source,phases,target,bytes);
}

bool thingsos_panel_ui_text_wave(const uint8_t* source,unsigned phases,uint8_t* target,size_t bytes) {
    return phases==THINGSOS_PANEL_TURN_PHASES && reader_black_correction(source,phases,target,bytes);
}

bool thingsos_panel_reader_ripple_wave(const uint8_t* source,unsigned phases,uint8_t* target,size_t bytes) {
    return phases==THINGSOS_PANEL_GC16_PHASES && reader_black_correction(source,phases,target,bytes);
}

static const EpdWaveformMode* mode_of(const EpdWaveform* wave, int type) {
    if (wave == NULL) return NULL;
    for (int i = 0; i < wave->num_modes; i++) {
        const EpdWaveformMode* mode = wave->mode_data[i];
        if (mode != NULL && mode->type == (uint8_t)type && mode->temp_ranges == 1 &&
            mode->range_data != NULL && mode->range_data[0] != NULL)
            return mode;
    }
    return NULL;
}

const EpdWaveform* thingsos_panel_waveform(void) {
    if (s_backed) return &s_waveform;
    /* DU must stay the threshold table: the source DU only drives dest 0/15,
       so every intermediate gray would be left undriven. */
    const EpdWaveformMode* du = mode_of(&E0470_WAVEFORM, MODE_DU);
    const EpdWaveformMode* gc16 = mode_of(&E0470_FULL_WAVEFORM, MODE_GC16);
    const EpdWaveformMode* gl16 = mode_of(&E0470_FULL_WAVEFORM, MODE_GL16);
    if (du == NULL || gc16 == NULL || gl16 == NULL) return &E0470_WAVEFORM;
    s_modes[0] = du;
    s_modes[1] = gc16;
    s_modes[2] = gl16;
    s_waveform.num_modes = 3;
    s_waveform.num_temp_ranges = 1;
    s_waveform.mode_data = s_modes;
    s_waveform.temp_intervals = E0470_WAVEFORM.temp_intervals;
    s_backed = true;
    return &s_waveform;
}

bool thingsos_panel_waveform_is_backed(void) {
    thingsos_panel_waveform();
    return s_backed;
}

const EpdWaveform* thingsos_panel_menu_waveform(void) {
    static uint8_t lut[THINGSOS_PANEL_GC16_PHASES * 64];
    static EpdWaveformPhases phases;
    static const EpdWaveformPhases* ranges[] = { &phases };
    static const EpdWaveformMode mode = { .type = MODE_GC16, .temp_ranges = 1, .range_data = ranges };
    static const EpdWaveformMode* modes[] = { &mode };
    static EpdWaveform wave = { .num_modes = 1, .num_temp_ranges = 1, .mode_data = modes };
    if (wave.temp_intervals) return &wave;

    const EpdWaveform* base = thingsos_panel_waveform();
    const EpdWaveformPhases* source = e0470_waveform_phases(base, MODE_GC16);
    if (!base->temp_intervals || !source || source->phases != THINGSOS_PANEL_GC16_PHASES ||
        !reader_black_correction(source->luts, source->phases, lut, sizeof(lut))) return NULL;

    /* Retain the full source transitions for old strokes becoming white.
       For already-white pixels retain only the last two source white pulses. */
    unsigned white_pulses = 0;
    for (int p = source->phases - 1; p >= 0 && white_pulses < 2; --p) {
        size_t at = (size_t)p * 64 + 63;
        if ((source->luts[at] & 3) != 2) continue;
        lut[at] = (lut[at] & 0xfc) | 2;
        ++white_pulses;
    }
    if (white_pulses != 2) return NULL;
    phases = *source;
    phases.luts = lut;
    wave.temp_intervals = base->temp_intervals;
    return &wave;
}

const EpdWaveform* thingsos_panel_dark_waveform(void) {
    static uint8_t gc_lut[THINGSOS_PANEL_GC16_PHASES*64],du_lut[THINGSOS_PANEL_DU_PHASES*64];
    static EpdWaveformPhases gc_phases,du_phases;
    static const EpdWaveformPhases* gc_ranges[]={&gc_phases};
    static const EpdWaveformPhases* du_ranges[]={&du_phases};
    static const EpdWaveformMode gc_mode={.type=MODE_GC16,.temp_ranges=1,.range_data=gc_ranges};
    static const EpdWaveformMode du_mode={.type=MODE_DU,.temp_ranges=1,.range_data=du_ranges};
    static const EpdWaveformMode* modes[]={&gc_mode,&du_mode};
    static EpdWaveform dark={.num_modes=2,.num_temp_ranges=1,.mode_data=modes};
    if(dark.temp_intervals)return &dark;
    const EpdWaveform* base=thingsos_panel_waveform();
    const EpdWaveformPhases* gc=e0470_waveform_phases(base,MODE_GC16);
    const EpdWaveformPhases* du=e0470_waveform_phases(base,MODE_DU);
    if(!base->temp_intervals || !gc || gc->phases!=THINGSOS_PANEL_GC16_PHASES ||
       !du || du->phases!=THINGSOS_PANEL_DU_PHASES ||
       !thingsos_refresh_mask_equal(gc->luts,gc->phases,gc_lut,sizeof(gc_lut)) ||
       !thingsos_refresh_black_base(du->luts,du->phases,du_lut,sizeof(du_lut)))return NULL;
    gc_phases=*gc;gc_phases.luts=gc_lut;
    du_phases=*du;du_phases.luts=du_lut;
    dark.temp_intervals=base->temp_intervals;
    return &dark;
}

const EpdWaveform* thingsos_panel_fast_waveform(void) {
    if (s_fast_ready) return &s_fast_waveform;
    const EpdWaveform* base = thingsos_panel_waveform();
    const EpdWaveformMode* du = mode_of(base, MODE_DU);
    if (!s_backed || !du || base->num_modes != 3 || du->range_data[0]->phases != THINGSOS_PANEL_DU_PHASES ||
        !du->range_data[0]->luts) return base;
    s_fast_phases = *du->range_data[0];
    memcpy(s_fast_luts, s_fast_phases.luts, sizeof(s_fast_luts));
    unsigned ticks = 0;
    /* Reuse the last two source white-going phases. Leave every changed
       transition and the trailing release/hold phases byte-for-byte intact.
       Unlike GC16, stable white is never driven towards black. */
    for (int p = s_fast_phases.phases - 1; p >= 0 && ticks < 2; --p) {
        bool white = false;
        for (unsigned from = 0; from < 15; ++from) {
            unsigned at = (unsigned)p * 64 + 60 + from / 4;
            if (((s_fast_luts[at] >> (6 - 2 * (from % 4))) & 3) == 2) white = true;
        }
        if (white) {
            unsigned at = (unsigned)p * 64 + 63;
            s_fast_luts[at] = (s_fast_luts[at] & 0xfc) | 2;
            ++ticks;
        }
    }
    if (ticks != 2) return base;
    s_fast_phases.luts = s_fast_luts;
    s_fast_ranges[0] = &s_fast_phases;
    s_fast_mode = *du;
    s_fast_mode.range_data = s_fast_ranges;
    for (unsigned i = 0; i < base->num_modes; ++i)
        s_fast_modes[i] = base->mode_data[i] == du ? &s_fast_mode : base->mode_data[i];
    s_fast_waveform = *base;
    s_fast_waveform.mode_data = s_fast_modes;
    s_fast_ready = true;
    return &s_fast_waveform;
}

const EpdWaveform* thingsos_panel_turn_waveform(void) {
    if (s_turn_backed) return &s_turn_waveform;
    /* E0470_WAVEFORM already carries the supplier trim (GC16 36 / GL16 37) and
       the threshold DU; e0470_waveform_init() must have run first. */
    const EpdWaveformMode* du = mode_of(&E0470_WAVEFORM, MODE_DU);
    const EpdWaveformMode* gc16 = mode_of(&E0470_WAVEFORM, MODE_GC16);
    const EpdWaveformMode* gl16 = mode_of(&E0470_WAVEFORM, MODE_GL16);
    if (du == NULL || gc16 == NULL || gl16 == NULL) {
        ESP_LOGW("ThingsOS_board", "Trimmed turn waveform unavailable; using the full table");
        return thingsos_panel_waveform();
    }
    s_turn_modes[0] = du;
    s_turn_modes[1] = gc16;
    s_turn_modes[2] = gl16;
    s_turn_waveform.num_modes = 3;
    s_turn_waveform.num_temp_ranges = 1;
    s_turn_waveform.mode_data = s_turn_modes;
    s_turn_waveform.temp_intervals = E0470_WAVEFORM.temp_intervals;
    s_turn_backed = true;
    return &s_turn_waveform;
}
const EpdWaveform* thingsos_panel_navigation_waveform(bool reader,bool keyboard_layout){
    return !reader&&!keyboard_layout?thingsos_panel_waveform():thingsos_panel_turn_waveform();
}
