/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"

/* Run from task context. DMA is a subset of internal SRAM. */
static inline void thingsos_memory_trace(const char* event, int subject, int result) {
    const uint32_t sram_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t dma_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
    const uint32_t psram_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    multi_heap_info_t sram, dma, psram;
    heap_caps_get_info(&sram, sram_caps);
    heap_caps_get_info(&dma, dma_caps);
    heap_caps_get_info(&psram, psram_caps);
    ESP_LOGI("ThingsOS_memtrace",
             "event=%s subject=%d result=%d sram_free=%u sram_total=%u sram_largest=%u sram_min=%u "
             "dma_free=%u dma_total=%u dma_largest=%u dma_min=%u psram_free=%u psram_total=%u psram_largest=%u psram_min=%u",
             event, subject, result,
             (unsigned)sram.total_free_bytes, (unsigned)heap_caps_get_total_size(sram_caps),
             (unsigned)sram.largest_free_block, (unsigned)sram.minimum_free_bytes,
             (unsigned)dma.total_free_bytes, (unsigned)heap_caps_get_total_size(dma_caps),
             (unsigned)dma.largest_free_block, (unsigned)dma.minimum_free_bytes,
             (unsigned)psram.total_free_bytes, (unsigned)heap_caps_get_total_size(psram_caps),
             (unsigned)psram.largest_free_block, (unsigned)psram.minimum_free_bytes);
}
#else
static inline void thingsos_memory_trace(const char* event, int subject, int result) {
    (void)event;
    (void)subject;
    (void)result;
}
#endif
