// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "thingsos/graphics.h"
#define THINGSOS_PANEL_BYTES (1216u * 684u / 2u)
/* E0470 inverted portrait: logical (x,y) -> physical (y,683-x).
   Landscape 1216x684 uses native physical order (x,y).
   Does not allocate, change waveform, or alter the old-frame baseline. */
bool thingsos_panel_pack(const thingsos_canvas_t*, unsigned char* packed, size_t bytes,
                         bool monochrome);
/* Portrait uses even logical rows; landscape uses native rows.
   Other physical bytes are left untouched. */
bool thingsos_panel_pack_rows(const thingsos_canvas_t*, unsigned char*, size_t, bool monochrome,
                              unsigned first, unsigned rows);
/* Pack and compare against the last submitted frame without another canvas.
   Changed logical rows are even-aligned; rows=0 means identical 4-bit pixels. */
bool thingsos_panel_pack_diff(const thingsos_canvas_t*, unsigned char*, size_t, bool monochrome,
                             unsigned* first, unsigned* rows);
