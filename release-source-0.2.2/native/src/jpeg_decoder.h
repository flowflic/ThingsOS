// SPDX-License-Identifier: Apache-2.0
#pragma once
/* S31's ROM linker script assigns jd_prepare/jd_decomp unconditionally.
   Keep our grayscale/scaling decoder's ABI and symbols independent of ROM. */
#define jd_prepare thingsos_jpeg_prepare
#define jd_decomp thingsos_jpeg_decomp
#include "../vendor/tjpgd/tjpgd.h"
#undef jd_prepare
#undef jd_decomp
