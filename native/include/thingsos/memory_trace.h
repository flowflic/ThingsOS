/* SPDX-License-Identifier: Apache-2.0 */
/* Memory telemetry over the USB console (ThingsOS_memtrace lines).
   Line format follows the Read Pico sampler (event/subject/result plus
   sram_* and dma_* pools) and adds the ESP32-S31 split:
     sram_*  MALLOC_CAP_INTERNAL|8BIT  -- what the current start gates read;
             on S31 it also matches the 32 KiB LP RAM.
     dma_*   MALLOC_CAP_INTERNAL|DMA   -- main HP SRAM only (all S31 HP RAM
             is DMA capable); the real headroom for stacks and DMA buffers.
     lp_*    MALLOC_CAP_RTCRAM         -- slow LP RAM; an INTERNAL request
             falls back here when main SRAM is fragmented.
     psram_* MALLOC_CAP_SPIRAM|8BIT
   Numeric only: never pass titles, paths or credentials. */
#pragma once

#ifdef ESP_PLATFORM
#include <stdint.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "soc/soc.h"

/* Region of an address: 1 main SRAM, 2 LP RAM (slow on S31), 3 PSRAM, 0 other.
   Pass the address of a local to locate the calling task's stack. */
static inline int thingsos_memory_region(const void* p) {
    uintptr_t a = (uintptr_t)p;
#if defined(SOC_LP_RAM_LOW) && defined(SOC_LP_RAM_HIGH)
    if (a >= SOC_LP_RAM_LOW && a < SOC_LP_RAM_HIGH) return 2;
#endif
    if (esp_ptr_external_ram(p)) return 3;
    if (esp_ptr_internal(p)) return 1;
    return 0;
}

/* Run from task context; heap_caps_get_info takes the heap locks. */
static inline void thingsos_memory_trace(const char* event, int subject, int result) {
    const uint32_t caps[4] = { MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
                               MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA,
                               MALLOC_CAP_RTCRAM,
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT };
    multi_heap_info_t info[4];
    unsigned total[4];
    for (unsigned i = 0; i < 4; ++i) {
        heap_caps_get_info(&info[i], caps[i]);
        total[i] = (unsigned)heap_caps_get_total_size(caps[i]);
    }
    ESP_LOGI("ThingsOS_memtrace",
             "event=%s subject=%d result=%d "
             "sram_free=%u sram_total=%u sram_largest=%u sram_min=%u "
             "dma_free=%u dma_total=%u dma_largest=%u dma_min=%u "
             "lp_free=%u lp_total=%u lp_largest=%u lp_min=%u "
             "psram_free=%u psram_total=%u psram_largest=%u psram_min=%u",
             event, subject, result,
             (unsigned)info[0].total_free_bytes, total[0], (unsigned)info[0].largest_free_block, (unsigned)info[0].minimum_free_bytes,
             (unsigned)info[1].total_free_bytes, total[1], (unsigned)info[1].largest_free_block, (unsigned)info[1].minimum_free_bytes,
             (unsigned)info[2].total_free_bytes, total[2], (unsigned)info[2].largest_free_block, (unsigned)info[2].minimum_free_bytes,
             (unsigned)info[3].total_free_bytes, total[3], (unsigned)info[3].largest_free_block, (unsigned)info[3].minimum_free_bytes);
}
#else
static inline int thingsos_memory_region(const void* p) { (void)p; return 0; }
static inline void thingsos_memory_trace(const char* event, int subject, int result) {
    (void)event;
    (void)subject;
    (void)result;
}
#endif
