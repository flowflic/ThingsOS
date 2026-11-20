/* SPDX-License-Identifier: Apache-2.0 */
/* Why the last synchronous scan failed, for the diagnostics journal.
 * Written by scan_adapter.c at the end of a failed refresh, taken by board.c
 * on the main task right after the draw call returns. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
    SCAN_FAULT_EOF_MISMATCH = 1u << 0,   /* LCD DMA EOF descriptor not in the refill window */
    SCAN_FAULT_DESCRIPTOR = 1u << 1,     /* GDMA descriptor error */
    SCAN_FAULT_CKV = 1u << 2,            /* CKV pulse count / counter fault */
    SCAN_FAULT_FEED_EMPTY = 1u << 3,     /* producer saw the ISR take an empty queue */
    SCAN_FAULT_FEED_FULL = 1u << 4,      /* producer queue stayed full for 1 s */
    SCAN_FAULT_PREFIX = 1u << 5,         /* prefill prefix not committed (abort/timeout) */
    SCAN_FAULT_FRAME_TIMEOUT = 1u << 6,  /* LCD frame did not finish within 2 s */
    SCAN_FAULT_FEED_TIMEOUT = 1u << 7,   /* producer did not finish within 2 s */
    SCAN_FAULT_LCD_UNDERRUN = 1u << 8,   /* LCD peripheral hardware underrun */
    SCAN_FAULT_OTHER = 1u << 15,
};

typedef struct {
    uint32_t flags;       /* SCAN_FAULT_* */
    uint32_t phase;       /* waveform phase (frame) that failed */
    uint32_t consumed;    /* lines the LCD took in that phase */
    uint32_t gap_us;      /* longest EOF-to-EOF interrupt gap in the refresh */
    uint32_t isr_us;      /* longest EOF handler run */
    uint32_t catchups;    /* coalesced EOFs caught up in the refresh */
} thingsos_scan_fault_t;

/* Returns and clears the last failure record; false when none is pending. */
bool thingsos_scan_take_fault(thingsos_scan_fault_t* out);
