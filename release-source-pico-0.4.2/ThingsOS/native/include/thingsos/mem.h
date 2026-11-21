// SPDX-License-Identifier: Apache-2.0
#ifndef THINGSOS_MEM_H
#define THINGSOS_MEM_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
/* Operation buffers that are not DMA targets prefer PSRAM: internal SRAM is
   the scarce pool for stacks, ISR and DMA descriptors, while PSRAM keeps
   megabytes free. Blocks allocated here are released with free(), like any
   heap_caps allocation. Only small objects may fall back to the default heap;
   large reader buffers must not consume internal DMA/stack SRAM on failure.
   The no-PSRAM rescue build retains its original internal-heap fallback. */
static inline void* thingsos_psram_alloc(size_t bytes) {
    if (!bytes) return NULL;
#ifdef ESP_PLATFORM
    void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#ifdef CONFIG_SPIRAM_IGNORE_NOTFOUND
    if (p) return p;
#else
    if (p || bytes>4096) return p;
#endif
#endif
    return malloc(bytes);
}
static inline void* thingsos_psram_calloc(size_t items, size_t bytes) {
    if (!items || !bytes || items > SIZE_MAX / bytes) return NULL;
#ifdef ESP_PLATFORM
    void* p = heap_caps_calloc(items, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#ifdef CONFIG_SPIRAM_IGNORE_NOTFOUND
    if (p) return p;
#else
    if (p || items*bytes>4096) return p;
#endif
#endif
    return calloc(items, bytes);
}
/* Growth must stay in PSRAM: realloc() alone would move the block back to the
   internal heap, which is exactly what these buffers must not do. */
static inline void* thingsos_psram_realloc(void* block, size_t bytes) {
    if (!block) return thingsos_psram_alloc(bytes);
    if (!bytes) { free(block); return NULL; }
#ifdef ESP_PLATFORM
    void* p = heap_caps_realloc(block, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#ifdef CONFIG_SPIRAM_IGNORE_NOTFOUND
    if (p) return p;
#else
    if (p || bytes>4096) return p;
#endif
#endif
    return realloc(block, bytes);
}
#endif
