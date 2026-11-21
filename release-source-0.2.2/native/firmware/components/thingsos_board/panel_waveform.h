// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "epd_waveform.h"

/* Panel waveform for the Metalio E-Ink4-Plus (E0470A14-AF, 34-pin 8-bit).
   The mode set and phase counts mirror the vendor 2.0.76 firmware's
   MY_WAVEFORM, which is the reference for ghosting on this panel:

     DU    20 phases, threshold table: dest 0-7 -> black, 8-15 -> white
     GC16  48 phases, full table (no boot trim)
     GL16  48 phases, full table (no boot trim) plus the white-background tick

   The Read Pico reference build trims GC16/GL16 down to 36/37 phases at boot
   to save about 150 ms per full refresh. That trim also removes the erase and
   saturation margin, which is exactly the margin this panel needs to leave no
   residue behind, so the Metalio board keeps the untrimmed tables.

   Call `e0470_waveform_init()` once first: it builds the threshold DU table
   and applies the GL16 white tick to the full table. */
#define THINGSOS_PANEL_DU_PHASES 20
#define THINGSOS_PANEL_GC16_PHASES 48
#define THINGSOS_PANEL_GL16_PHASES 48

/* Reading transitions and keyboard layouts use trimmed reference tables:
   GC16 36 / GL16 37 phases. That is the Read Pico page-turn table and the
   difference is about a quarter of the scan time (36 vs 48 phases); the clean
   and periodic refreshes above keep the untrimmed set. Full UI navigation
   explicitly selects the complete table through the selector below. */
#define THINGSOS_PANEL_TURN_PHASES 36

/// Waveform used by clean/full and masked panel updates on this board. Never
/// NULL: if a mode is missing it falls back to the trimmed `E0470_WAVEFORM`.
const EpdWaveform* thingsos_panel_waveform(void);

/// Page-turn waveform (trimmed GC16 36 / GL16 37 + threshold DU). Falls back to
/// thingsos_panel_waveform() when the trimmed modes are unavailable.
const EpdWaveform* thingsos_panel_turn_waveform(void);
/* Full UI pages need the Metalio erase margin; preserve the trimmed reader and
   keyboard navigation tables. This does not add a second scan. */
const EpdWaveform* thingsos_panel_navigation_waveform(bool reader,bool keyboard_layout);

/* Reader DU: two white-only maintenance pulses on stable white, within the
   original active phases. Must be delivered with a full-pixel scan. */
const EpdWaveform* thingsos_panel_fast_waveform(void);
/* Full GC16 with equal-pixel hold, plus a DU black prepass that maintains
   stable black. NULL when the source tables cannot support both modes. */
const EpdWaveform* thingsos_panel_dark_waveform(void);

/* Same low-flash text view as Pico: changed pixels plus one white tick.
   Panel power lifetime is Metalio-specific; no source/timing changes.
   Unchanging black pixels are held, so this is only a fallback view. */
bool thingsos_panel_text_wave(const uint8_t* source,unsigned phases,uint8_t* target,size_t bytes);

/* Ordinary balanced reading: 36-phase low-flash view restoring only the
   source black-to-black correction. Unchanged black glyph pixels are then
   re-driven like the vendor table, which removes the uneven ink inside one
   glyph. */
bool thingsos_panel_reader_text_wave(const uint8_t* source,unsigned phases,uint8_t* target,size_t bytes);

/* Home/nested UI frames use the same corrected low-flash view as the reader:
   unchanged black icons and labels showed the same uneven ink as reader text.
   Identical bytes to thingsos_panel_reader_text_wave(); callers share one LUT. */
bool thingsos_panel_ui_text_wave(const uint8_t* source,unsigned phases,uint8_t* target,size_t bytes);
const EpdWaveform* thingsos_panel_menu_waveform(void);

/* Low-flash ripple: preserve the 48-phase source's black-to-black correction,
   with the same white maintenance and other transitions as the text view. */
bool thingsos_panel_reader_ripple_wave(const uint8_t* source,unsigned phases,uint8_t* target,size_t bytes);

/// True when the assembled waveform has the vendor phase counts above, so the
/// exact-shape views (equal-pixel hold, text, stagger) may be built from it.
bool thingsos_panel_waveform_is_backed(void);
