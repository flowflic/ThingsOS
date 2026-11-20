// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Build an equal-pixel hold view of a 16x16, 2-bit epdiy phase LUT.
   All changed-pixel actions remain exact. Source and target must be disjoint. */
bool thingsos_refresh_mask_equal(const uint8_t* source,unsigned phases,uint8_t* target,size_t bytes);
/* GC16 changed-pixel action sequence plus one source white-to-white erase
   pulse. Keeps all trailing holds, with no black pulse on stable background. */
bool thingsos_refresh_text_wave(const uint8_t* source,unsigned phases,uint8_t* target,size_t bytes);
/* DU black prepass: preserve every source transition and drive stable black
   in the last two white-to-black saturation phases, without a white pulse. */
bool thingsos_refresh_black_base(const uint8_t* source,unsigned phases,uint8_t* target,size_t bytes);
/* Union a physical rectangle into epdiy's line and nibble column masks. */
bool thingsos_refresh_include_rect(bool* rows,unsigned height,uint8_t* columns,unsigned width,
                                  unsigned x,unsigned y,unsigned w,unsigned h);
