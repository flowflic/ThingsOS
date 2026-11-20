// SPDX-License-Identifier: Apache-2.0
#ifndef THINGSOS_DIAGNOSTICS_H
#define THINGSOS_DIAGNOSTICS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Numeric, allowlisted telemetry only. Never pass user text or paths. */
enum { DIAG_BOOT=1, DIAG_HEALTH, DIAG_PAGE, DIAG_SLEEP, DIAG_WAKE, DIAG_POWER,
    DIAG_TASK, DIAG_PACK, DIAG_RESCUE, DIAG_INSTALL, DIAG_FAILURE,
    DIAG_NET_WIFI, DIAG_NET_DNS, DIAG_NET_HTTP, DIAG_NET_TLS, DIAG_NET_METRIC, DIAG_NET_RETRY };
enum { DIAG_REASON_UNKNOWN, DIAG_REASON_GUARD, DIAG_REASON_MANUAL,
    DIAG_REASON_UPDATE, DIAG_REASON_INSTALL_FAILED };
#define DIAG_BOOTS 4
/* The journal lives in PSRAM on the device (EXT_RAM_BSS, mutex-protected), so
   its size costs no internal/DMA SRAM. Failures are also kept in their own
   ring so network breadcrumbs cannot push them out. */
#define DIAG_EVENTS 512
#define DIAG_FAILURES 64
typedef struct {
    uint32_t id, reset, attempts, healthy, reason;
    char version[32], elf[65], running[17], selected[17];
} thingsos_diag_boot_t;
typedef struct {
    uint32_t boot, ms, kind, stage, a, b, utc;
    int32_t code, error;
} thingsos_diag_event_t;
typedef struct {
    uint32_t magic, sequence, boot_count, event_count, next_reason;
    thingsos_diag_boot_t boots[DIAG_BOOTS];
    thingsos_diag_boot_t last_main;
    thingsos_diag_event_t events[DIAG_EVENTS];
    uint32_t failure_count;
    thingsos_diag_event_t failures[DIAG_FAILURES];
    uint32_t checksum;
} thingsos_diag_journal_t;
uint32_t thingsos_diag_crc(const void*,size_t);
bool thingsos_diag_valid(const thingsos_diag_journal_t*);
void thingsos_diag_seal(thingsos_diag_journal_t*);
void thingsos_diag_push(thingsos_diag_journal_t*,thingsos_diag_event_t);
/* Adds a failure to the failure ring only (deduplicated), e.g. when importing. */
void thingsos_diag_keep_failure(thingsos_diag_journal_t*,thingsos_diag_event_t);
typedef bool (*thingsos_diag_sink)(void*,const char*,size_t);
bool thingsos_diag_dump(const thingsos_diag_journal_t*,thingsos_diag_sink,void*);
/* Device adapter; host core deliberately records nothing by default. */
#if defined(ESP_PLATFORM) || defined(THINGSOS_DIAG_TEST)
void thingsos_diag_start(void);
void thingsos_diag_event(unsigned,unsigned,unsigned,unsigned,int,int);
void thingsos_diag_health(void);
void thingsos_diag_reason(unsigned,int);
void thingsos_diag_poll(bool force);
bool thingsos_diag_export(thingsos_diag_sink,void*);
const char* thingsos_diag_reason_text(void);
void thingsos_diag_summary(char*,size_t);
#else
static inline void thingsos_diag_event(unsigned k,unsigned s,unsigned a,unsigned b,int c,int e)
{(void)k;(void)s;(void)a;(void)b;(void)c;(void)e;}
static inline void thingsos_diag_summary(char* p,size_t n){if(n)p[0]=0;}
#endif
#endif
