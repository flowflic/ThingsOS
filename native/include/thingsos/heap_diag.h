/* SPDX-License-Identifier: Apache-2.0 */
/* One-shot main-SRAM heap snapshots for locating persistent allocations that
   split the largest free block (ThingsOS_heapdiag lines). Snapshots live in
   PSRAM; the walk callback only records (no allocation or logging under the
   heap lock). Device only. */
#pragma once
#ifdef ESP_PLATFORM
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

#define THINGSOS_HEAP_DIAG_BLOCKS 1024
typedef struct { uintptr_t addr; uint32_t size; bool used; } thingsos_heap_diag_block_t;
typedef struct {
    unsigned count;
    bool overflow;
    thingsos_heap_diag_block_t blocks[THINGSOS_HEAP_DIAG_BLOCKS];
} thingsos_heap_diag_snapshot_t;

static bool thingsos_heap_diag_walker(walker_heap_into_t heap, walker_block_info_t block, void* user) {
    (void)heap;
    thingsos_heap_diag_snapshot_t* s = user;
    if (s->count >= THINGSOS_HEAP_DIAG_BLOCKS) { s->overflow = true; return false; }
    s->blocks[s->count++] = (thingsos_heap_diag_block_t){ (uintptr_t)block.ptr, (uint32_t)block.size, block.used };
    return true;
}
/* Main HP SRAM only (on ESP32-S31 every HP RAM region is DMA capable). */
static inline void thingsos_heap_diag_take(thingsos_heap_diag_snapshot_t* s) {
    s->count = 0;
    s->overflow = false;
    heap_caps_walk(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA, thingsos_heap_diag_walker, s);
}
static inline bool thingsos_heap_diag_has(const thingsos_heap_diag_snapshot_t* s, uintptr_t addr, uint32_t size) {
    for (unsigned i = 0; i < s->count; ++i)
        if (s->blocks[i].used && s->blocks[i].addr == addr && s->blocks[i].size == size) return true;
    return false;
}
/* Log used blocks present in `after` but not in `before`; `mid` tells whether a
   block already existed at the intermediate point. */
static inline void thingsos_heap_diag_report(const char* tag, const thingsos_heap_diag_snapshot_t* before,
                                             const thingsos_heap_diag_snapshot_t* mid,
                                             const thingsos_heap_diag_snapshot_t* after) {
    uint32_t largest = 0; uintptr_t largest_at = 0;
    for (unsigned i = 0; i < after->count; ++i)
        if (!after->blocks[i].used && after->blocks[i].size > largest) {
            largest = after->blocks[i].size; largest_at = after->blocks[i].addr;
        }
    ESP_LOGI("ThingsOS_heapdiag", "%s: blocks before=%u mid=%u after=%u overflow=%d/%d/%d largest_free=%u at %p",
             tag, before->count, mid ? mid->count : 0, after->count, before->overflow, mid ? mid->overflow : 0,
             after->overflow, (unsigned)largest, (void*)largest_at);
    unsigned reported = 0;
    for (unsigned i = 0; i < after->count && reported < 48; ++i) {
        const thingsos_heap_diag_block_t* b = &after->blocks[i];
        if (!b->used || thingsos_heap_diag_has(before, b->addr, b->size)) continue;
        uint32_t prev_free = i > 0 && !after->blocks[i - 1].used ? after->blocks[i - 1].size : 0;
        uint32_t next_free = i + 1 < after->count && !after->blocks[i + 1].used ? after->blocks[i + 1].size : 0;
        char hex[3 * 32 + 1] = { 0 };
        uint32_t n = b->size < 32 ? b->size : 32;
        for (uint32_t k = 0; k < n; ++k)
            snprintf(hex + 3 * k, sizeof(hex) - 3 * k, "%02x ", ((const uint8_t*)b->addr)[k]);
        ESP_LOGI("ThingsOS_heapdiag", "%s: new %p size=%u at_mid=%d prev_free=%u next_free=%u data=%s",
                 tag, (void*)b->addr, (unsigned)b->size, mid ? thingsos_heap_diag_has(mid, b->addr, b->size) : -1,
                 (unsigned)prev_free, (unsigned)next_free, hex);
        ++reported;
    }
    ESP_LOGI("ThingsOS_heapdiag", "%s: %u new persistent block(s) reported", tag, reported);
}
#endif
