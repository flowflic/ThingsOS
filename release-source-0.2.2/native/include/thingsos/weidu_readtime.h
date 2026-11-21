/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WD_RT_BATCH 512u
#define WD_RT_HOURS 12u
#define WD_RT_CAPACITY 8192u
#define WD_RT_RAM 32u
typedef struct {
    uint64_t id;
    char book[64];
    uint32_t hour, seconds, version, risk, until;
    int32_t zone; /* standard UTC offset in minutes, captured with the interval */
} thingsos_rt_row_t;
typedef struct {
    uint32_t pending, unknown, quarantined, rows;
    bool damaged, full;
} thingsos_rt_summary_t;
typedef struct {
    unsigned count;
    thingsos_rt_row_t rows[WD_RT_BATCH];
} thingsos_rt_batch_t;
typedef struct {
    char owner[33];
    thingsos_rt_row_t row;
    uint32_t milliseconds;
    uint32_t floor;
} thingsos_rt_capture_row_t;
typedef struct {
    thingsos_rt_capture_row_t rows[WD_RT_RAM];
    unsigned count;
    uint32_t nonce, sequence;
    uint32_t saved_rows, saved_seconds;
    bool full;
} thingsos_rt_capture_t;
/* All disk operations have one external owner. No credentials are written here.
   A flushed .sent copy is durable intent; .sent is NEVER replayed, including
   after a restart. A corrupt file is retained and reported, never reset. */
bool thingsos_rt_summary(const char *, const char *, thingsos_rt_summary_t *);
bool thingsos_rt_put(const char *, const char *, const thingsos_rt_row_t *);
bool thingsos_rt_peek(const char *, const char *, uint32_t, thingsos_rt_batch_t *);
bool thingsos_rt_claim(const char *, const char *, const thingsos_rt_batch_t *);
bool thingsos_rt_confirm(const char *, const char *, const thingsos_rt_batch_t *);
bool thingsos_rt_capture(thingsos_rt_capture_t *, const char *, const char *, uint64_t, uint32_t,
                         int32_t, uint32_t);
bool thingsos_rt_flush(thingsos_rt_capture_t *, const char *);
bool thingsos_rt_sha256(const char *, char[65]);
bool thingsos_rt_hash(const void *, size_t, char[65]);

typedef struct {
    char vid[128], access[512], refresh[512], device[64];
} thingsos_rt_credentials_t;
/* Protocol adapter has its own copyleft attribution; see weidu_readtime_protocol.c. */
bool thingsos_rt_sign(const char *const *, size_t, char[65]);
bool thingsos_rt_body(const thingsos_rt_credentials_t *, const thingsos_rt_batch_t *, const char *,
                      const char *, uint64_t, uint32_t, uint32_t, int32_t, char *, size_t);
