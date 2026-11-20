/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
/* The official pure LUT trimming unit includes epdiy.h but only needs waveform
   types. Keep ESP peripheral headers outside this host-only test target. */
#include "epd_waveform.h"

/* Draw mode numbers as defined by epdiy.h; only the panel assembly needs them. */
enum EpdDrawMode { MODE_DU = 0x1, MODE_GC16 = 0x2, MODE_GL16 = 0x5 };
