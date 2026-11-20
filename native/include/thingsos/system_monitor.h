/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {uint64_t total,free,largest;bool valid;} thingsos_system_memory_t;
typedef struct {
    uint32_t pclk_mhz,last_refresh_ms;
    bool refresh_valid;
    uint32_t underrun_count;
    uint64_t underrun_last_seconds;
    bool underrun_during_clear,last_present_failed;
} thingsos_system_display_t;
typedef struct {
    uint32_t cpu_mhz,cpu_max_mhz;
    uint16_t cpu_used_bp; /* 10000 = both cores fully occupied. */
    bool cpu_valid,cpu_error;
    thingsos_system_memory_t sram,psram,flash;
    thingsos_system_display_t display;
} thingsos_system_snapshot_t;
typedef struct {
    uint64_t stamp[2],idle[2];
    uint16_t used_bp;
    bool initialized,valid;
} thingsos_system_cpu_window_t;
/* Per-core monotonic microsecond clocks and completed idle runtimes. */
bool thingsos_system_cpu_step(thingsos_system_cpu_window_t*,const uint64_t[2],const uint64_t[2],uint16_t*);
bool thingsos_system_flash_layout(uint64_t,uint64_t,uint64_t,thingsos_system_memory_t*);
unsigned thingsos_system_ratio(uint64_t used,uint64_t total,unsigned width);
