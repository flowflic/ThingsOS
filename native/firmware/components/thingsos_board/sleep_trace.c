// SPDX-License-Identifier: Apache-2.0
/* Sleep/wake breadcrumbs and panic capture that survive a watchdog reset.

   The diagnostics journal lives in PSRAM and is lost on reset; only the last
   event survives in RTC memory. That cannot tell whether an interrupt-watchdog
   reset happened while entering light sleep, inside it, while resuming or
   later. This module keeps the current power stage and, through a wrapped
   esp_panic_handler, the faulting core and both cores' PC/RA in RTC_NOINIT
   memory, then reports them as numeric DIAG_TRACE events on the next boot.
   Behaviour of sleep, wake and panic handling is unchanged. */
#include "board.h"
#include "thingsos/diagnostics.h"
#include <string.h>
#include <time.h>
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_private/panic_internal.h"
#include "riscv/rvruntime-frames.h"
#include "soc/soc_caps.h"
#include "esp_freertos_hooks.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TRACE_MAGIC 0x534C5452u /* "SLTR" */
#define PANIC_MAGIC 0x50414E43u /* "PANC" */

typedef struct {
    uint32_t magic;
    uint32_t stage;
    uint32_t prev_stage;
    uint32_t stage_utc;
    uint32_t sleeps;      /* light-sleep returns since the last suspend */
    uint32_t last_causes; /* wakeup-cause bitmap of the last light sleep */
    int32_t last_err;     /* esp_light_sleep_start() result */
    uint32_t panic_magic;
    uint32_t panic_exception;
    uint32_t panic_core;
    uint32_t panic_stage;
    uint32_t pc[SOC_CPU_CORES_NUM];
    uint32_t ra[SOC_CPU_CORES_NUM];
    uint32_t mcause;
    uint32_t mtval;
    /* Per-core FreeRTOS tick breadcrumbs: how many ticks each core has serviced
       since the hooks were registered, and the task running there. If both cores stop taking interrupts, these
       freeze at the moment each core stopped. */
    uint32_t core_tick[SOC_CPU_CORES_NUM];
    char core_task[SOC_CPU_CORES_NUM][8];
    /* Finer main-loop position (THINGSOS_MARK_*) and the tick when it was set. */
    uint32_t mark;
    uint32_t mark_tick;
} sleep_trace_t;

static RTC_NOINIT_ATTR sleep_trace_t trace;

static uint32_t trace_now(void) {
    time_t t = time(NULL);
    return t >= 1577836800 && (uint64_t)t <= 4102444799u ? (uint32_t)t : 0;
}

static void trace_reset(void) {
    memset(&trace, 0, sizeof(trace));
    trace.magic = TRACE_MAGIC;
}

void thingsos_board_trace_stage(unsigned stage) {
    if (trace.magic != TRACE_MAGIC) trace_reset();
    if (stage == THINGSOS_TRACE_SUSPEND_BEGIN) trace.sleeps = 0;
    if (trace.stage != stage) trace.prev_stage = trace.stage;
    trace.stage = stage;
    trace.stage_utc = trace_now();
}

void thingsos_board_trace_sleep_return(uint32_t causes, int err) {
    if (trace.magic != TRACE_MAGIC) trace_reset();
    ++trace.sleeps;
    trace.last_causes = causes;
    trace.last_err = err;
    thingsos_board_trace_stage(THINGSOS_TRACE_SLEEP_RETURN);
}

static uint32_t hook_ticks[SOC_CPU_CORES_NUM]; /* core-local tick-hook counts */

void thingsos_board_trace_mark(unsigned mark) {
    if (trace.magic != TRACE_MAGIC) return;
    trace.mark = mark;
    trace.mark_tick = hook_ticks[0]; /* same base as core_tick[0] */
}

/* Tick hooks run in the tick ISR, also while the flash cache is disabled, so
   they call nothing that lives in flash: pcTaskGetName() and
   xTaskGetCurrentTaskHandleForCore() are flash-resident in this build. The
   running TCB comes from the kernel's DRAM array and the name from the
   StaticTask_t mirror of the TCB, read only when the TCB is in internal RAM. */
extern void* volatile pxCurrentTCBs[SOC_CPU_CORES_NUM];
static void* traced_task[SOC_CPU_CORES_NUM];
static void IRAM_ATTR trace_tick_core(unsigned core) {
    if (trace.magic != TRACE_MAGIC) return;
    /* Plain per-core count of serviced ticks (no kernel lock in the ISR). */
    trace.core_tick[core] = ++hook_ticks[core];
    void* task = pxCurrentTCBs[core];
    if (task == traced_task[core]) return;
    traced_task[core] = task;
    char* dst = trace.core_task[core];
    if (task && esp_ptr_internal(task)) {
        const char* name = (const char*)((const StaticTask_t*)task)->ucDummy7;
        unsigned i = 0;
        for (; i < sizeof(trace.core_task[core]) && name[i]; ++i) dst[i] = name[i];
        for (; i < sizeof(trace.core_task[core]); ++i) dst[i] = 0;
    } else {
        for (unsigned i = 0; i < sizeof(trace.core_task[core]); ++i) dst[i] = task ? '?' : 0;
    }
}
static void IRAM_ATTR trace_tick_core0(void) { trace_tick_core(0); }
#if SOC_CPU_CORES_NUM > 1
static void IRAM_ATTR trace_tick_core1(void) { trace_tick_core(1); }
#endif

static int32_t pack4(const char* s) {
    return (int32_t)((uint32_t)(uint8_t)s[0] | ((uint32_t)(uint8_t)s[1] << 8) |
                     ((uint32_t)(uint8_t)s[2] << 16) | ((uint32_t)(uint8_t)s[3] << 24));
}

void thingsos_board_trace_awake(void) {
    if (trace.magic == TRACE_MAGIC && trace.stage == THINGSOS_TRACE_AWAKE) return;
    thingsos_board_trace_stage(THINGSOS_TRACE_AWAKE);
}

/* Runs in panic context, possibly with the flash cache disabled: IRAM only,
   plain stores to RTC memory, no library calls. */
void __real_esp_panic_handler(panic_info_t* info);
void IRAM_ATTR __wrap_esp_panic_handler(panic_info_t* info) {
    if (trace.magic == TRACE_MAGIC && info) {
        trace.panic_exception = (uint32_t)info->exception;
        trace.panic_core = (uint32_t)info->core;
        trace.panic_stage = trace.stage;
        for (unsigned i = 0; i < SOC_CPU_CORES_NUM; ++i) {
            const RvExcFrame* f = (const RvExcFrame*)g_exc_frames[i];
            trace.pc[i] = f ? (uint32_t)f->mepc : 0;
            trace.ra[i] = f ? (uint32_t)f->ra : 0;
        }
        const RvExcFrame* own = (const RvExcFrame*)info->frame;
        trace.mcause = own ? (uint32_t)own->mcause : 0;
        trace.mtval = own ? (uint32_t)own->mtval : 0;
        trace.panic_magic = PANIC_MAGIC;
    }
    __real_esp_panic_handler(info);
}

void thingsos_board_trace_report(void) {
    esp_reset_reason_t reset = esp_reset_reason();
    bool valid = trace.magic == TRACE_MAGIC && reset != ESP_RST_POWERON;
    if (valid) {
        bool abnormal = reset == ESP_RST_INT_WDT || reset == ESP_RST_TASK_WDT || reset == ESP_RST_WDT ||
                        reset == ESP_RST_PANIC || reset == ESP_RST_BROWNOUT || reset == ESP_RST_CPU_LOCKUP ||
                        reset == ESP_RST_UNKNOWN || trace.panic_magic == PANIC_MAGIC;
        if (abnormal) {
            uint32_t now = trace_now();
            int age = now && trace.stage_utc && now >= trace.stage_utc ? (int)(now - trace.stage_utc) : -1;
            /* stage 0: power stage when the previous boot ended. */
            thingsos_diag_event(DIAG_TRACE, 0, trace.stage, trace.prev_stage, trace.last_err, age);
            /* stage 1: light-sleep cycles since suspend and the last wake causes. */
            thingsos_diag_event(DIAG_TRACE, 1, trace.sleeps, trace.last_causes, (int)reset, 0);
            if (trace.panic_magic == PANIC_MAGIC) {
                /* stage 2: panic kind, faulting core, stage at panic, mcause/mtval. */
                thingsos_diag_event(DIAG_TRACE, 2, trace.panic_exception,
                                    trace.panic_core | (trace.panic_stage << 8),
                                    (int)trace.mcause, (int)trace.mtval);
                /* stage 3/4: PC and RA of each core (0 when that core left no frame). */
                for (unsigned i = 0; i < SOC_CPU_CORES_NUM && i < 2; ++i) {
                    thingsos_diag_event(DIAG_TRACE, 3 + i, i, 0, (int)trace.pc[i], (int)trace.ra[i]);
                }
            }
            /* stage 5: main-loop mark, its tick, and each core's last serviced tick. */
            thingsos_diag_event(DIAG_TRACE, 5, trace.mark, trace.mark_tick,
                                (int)trace.core_tick[0], SOC_CPU_CORES_NUM > 1 ? (int)trace.core_tick[1] : 0);
            /* stage 6: first 8 characters of the task on core 0 (a,b) and core 1 (code,errno), ASCII little-endian. */
            thingsos_diag_event(DIAG_TRACE, 6, (unsigned)pack4(trace.core_task[0]), (unsigned)pack4(trace.core_task[0] + 4),
                                SOC_CPU_CORES_NUM > 1 ? pack4(trace.core_task[1]) : 0,
                                SOC_CPU_CORES_NUM > 1 ? pack4(trace.core_task[1] + 4) : 0);
        }
    }
    trace_reset();
    trace.stage = THINGSOS_TRACE_BOOT;
    trace.stage_utc = trace_now();
    static bool hooked;
    if (!hooked) {
        hooked = true;
        esp_register_freertos_tick_hook_for_cpu(trace_tick_core0, 0);
#if SOC_CPU_CORES_NUM > 1
        esp_register_freertos_tick_hook_for_cpu(trace_tick_core1, 1);
#endif
    }
}
