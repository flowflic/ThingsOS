// SPDX-License-Identifier: Apache-2.0
/* Compile the pinned upstream decoder unchanged with local symbol names.
   Its ChaN license and configuration remain in native/vendor/tjpgd. */
#include "jpeg_decoder.h"
#define jd_prepare thingsos_jpeg_prepare
#define jd_decomp thingsos_jpeg_decomp
#include "../vendor/tjpgd/tjpgd.c"
