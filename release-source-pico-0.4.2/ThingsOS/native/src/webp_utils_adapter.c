// SPDX-License-Identifier: Apache-2.0
/* Include libc before remapping calls: MSVC's dllimport must stay on libc. */
#include <stdlib.h>
#include "webp_memory.h"
#define malloc thingsos_webp_malloc
#define calloc thingsos_webp_calloc
#define free thingsos_webp_free
#include "../vendor/libwebp/src/utils/utils.c"
