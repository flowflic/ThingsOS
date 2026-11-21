/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "thingsos/content.h"
#include "thingsos/weidu.h"
#include "thingsos/annotations.h"
#define WD_SYNC_MIN_TIME 1790812800u /* 2026-10-01 UTC, current protocol baseline */
#define WD_NOTE_LIMIT 500u
#define WD_REVIEW_LIMIT 50u
#define WD_NOTE_TEXT 4096u
#define WD_DRAW_LIMIT 128u
/* HTTP responses stream to SD, not a RAM body allocation. */
#define WD_RESPONSE_LIMIT (4u * 1024u * 1024u)
#define WD_USER_AGENT "Mozilla/5.0 Chrome/135.0.0 Safari/537.36"
#define WD_REPORT_INTERVAL_MS 30000u
#define WD_REPORT_MAX_SECONDS 60u
/* Only idempotent reads may use this bounded authentication retry. Never time uploads. */
typedef thingsos_weidu_error_t (*thingsos_wd_read_fn)(thingsos_weidu_client_t*,void*);
thingsos_weidu_error_t thingsos_wd_authenticated_read(thingsos_weidu_client_t*,bool,thingsos_wd_read_fn,void*,bool*);
/* Parse an authentication response in RAM only. Output is cleared on failure. */
bool thingsos_wd_key_header(const char*,size_t,char*,size_t);
typedef struct {
    char uid[64], chapter[192], author[128], text[WD_NOTE_TEXT];
    uint32_t begin, length, heat;
    bool range, truncated;
    uint32_t integrity;
} thingsos_wd_note_t;
typedef struct {
    thingsos_wd_note_t note;
    char quote[THINGSOS_ANNOTATION_QUOTE+1],review_id[96];
    uint32_t integrity;
} thingsos_wd_personal_t;
thingsos_weidu_error_t thingsos_wd_personal_refresh(thingsos_weidu_client_t*,const char*);
bool thingsos_wd_personal_info(const char*,const thingsos_weidu_session_t*,const char*,unsigned*);
bool thingsos_wd_personal(const char*,const thingsos_weidu_session_t*,const char*,unsigned,thingsos_wd_personal_t*);
bool thingsos_wd_personal_find(const char*,const thingsos_weidu_session_t*,const char*,const char*,uint32_t,uint32_t,thingsos_wd_personal_t*);
thingsos_weidu_error_t thingsos_wd_annotation_push(thingsos_weidu_client_t*,const char*,const char*,thingsos_annotation_t*,unsigned*);
typedef struct {
    uint32_t count[3], total[3];
    bool limited[3];
} thingsos_wd_notes_info_t;
typedef struct {
    char uid[64];
    uint32_t offset, percent, updated;
    bool valid;
    uint32_t reading_seconds;
    bool reading_seconds_valid;
} thingsos_wd_progress_t;
typedef struct {
    char id[64];
    unsigned chapter;
    uint32_t offset, percent, seconds;
    bool report;
} thingsos_wd_upload_t;
/* RAM-only reader context. Establish while reading, consume once on exit. */
typedef struct {
    char id[64],ps[128],pc[128],token[128];
    uint32_t started,last_seconds,last_end;
    bool entered,consumed;
} thingsos_wd_read_session_t;
thingsos_weidu_error_t thingsos_wd_begin(thingsos_weidu_client_t*,const thingsos_wd_upload_t*,thingsos_wd_read_session_t*);
thingsos_weidu_error_t thingsos_wd_upload_session(thingsos_weidu_client_t*,const thingsos_wd_upload_t*,thingsos_weidu_error_t*,thingsos_wd_read_session_t*);
thingsos_weidu_error_t thingsos_wd_upload_interval(thingsos_weidu_client_t*,const thingsos_wd_upload_t*,thingsos_weidu_error_t*,thingsos_wd_read_session_t*,bool);
typedef struct {
    size_t begin, end;
    bool mine;
} thingsos_wd_line_t;

#define WD_THOUGHT_PAGE 10u
typedef struct {
    char id[64], uid[64];
    uint32_t begin, end, max_idx;
    char sync[40];
} thingsos_wd_thought_query_t;
typedef struct {
    uint32_t count, next_idx, total;
    bool more;
    char sync[40];
} thingsos_wd_thought_info_t;
/* Account-bound, range-specific pages. Response streams to SD; one record in RAM. */
thingsos_weidu_error_t thingsos_wd_thought_refresh(thingsos_weidu_client_t*,const thingsos_wd_thought_query_t*);
bool thingsos_wd_thought_info(const char*,const thingsos_weidu_session_t*,const thingsos_wd_thought_query_t*,thingsos_wd_thought_info_t*);
bool thingsos_wd_thought(const char*,const thingsos_weidu_session_t*,const thingsos_wd_thought_query_t*,unsigned,thingsos_wd_note_t*);
typedef struct {
    uint32_t* source;
    size_t bytes, count, capacity;
    const char* text;
    uint32_t expected;
} thingsos_wd_map_t;
bool thingsos_wd_line_note(const char*,const thingsos_weidu_session_t*,const char*,const char*,const thingsos_wd_map_t*,const thingsos_chapter_t*,size_t,size_t,thingsos_wd_note_t*);

bool thingsos_wd_toc(const char*, const char*, unsigned, thingsos_weidu_chapter_t*);
/* Offline section navigation; independent of font/layout and reading segments. */
bool thingsos_wd_navigation(const char*,const char*,thingsos_epub_t*);
/* First step reads a warm cache or publishes root entries without raw scans;
   subsequent calls process at most one chapter. */
bool thingsos_wd_navigation_step(const char*,const char*,thingsos_epub_t*,bool*);
int thingsos_wd_chapter(const char*, const char*, const char*);
thingsos_weidu_error_t thingsos_wd_notes_refresh(thingsos_weidu_client_t*, const char*);
/* Download/rebuild then cache notes sequentially; a note failure preserves the committed book. */
thingsos_weidu_error_t thingsos_wd_cache_bundle(thingsos_weidu_client_t*,const thingsos_weidu_book_t*,bool,thingsos_weidu_error_t*);
/* UI directory lookup: account/seal/length checks; each displayed record retains its CRC check. */
bool thingsos_wd_notes_view(const char*,const thingsos_weidu_session_t*,const char*,thingsos_wd_notes_info_t*);
bool thingsos_wd_notes_info(
    const char*, const thingsos_weidu_session_t*, const char*, thingsos_wd_notes_info_t*);
bool thingsos_wd_note(
    const char*, const thingsos_weidu_session_t*, const char*, unsigned, unsigned,
    thingsos_wd_note_t*);
thingsos_weidu_error_t thingsos_wd_progress_parse(const char*, thingsos_wd_progress_t*);
thingsos_weidu_error_t thingsos_wd_pull(
    thingsos_weidu_client_t*, const char*, thingsos_wd_progress_t*);
thingsos_weidu_error_t thingsos_wd_upload(
    thingsos_weidu_client_t*, const thingsos_wd_upload_t*, thingsos_weidu_error_t*);
/* Validated against the entire existing chapter. No EPUB rewrite or fuzzy match. */
bool thingsos_wd_map(
    const char*, const char*, unsigned, const thingsos_chapter_t*, thingsos_wd_map_t*);
void thingsos_wd_map_free(thingsos_wd_map_t*);
bool thingsos_wd_map_range(
    const thingsos_wd_map_t*, const thingsos_chapter_t*, const thingsos_wd_note_t*, size_t*,
    size_t*);
bool thingsos_wd_source(const thingsos_wd_map_t*, size_t, uint32_t*);
bool thingsos_wd_local(const thingsos_wd_map_t*, uint32_t, size_t*);
bool thingsos_wd_read_body(
    const thingsos_wd_upload_t*, const thingsos_weidu_chapter_t*, const char*, uint32_t, uint32_t,
    char*, size_t);

/* One account-bound retryable progress record. Time is always stripped. */
bool thingsos_wd_pending_save(const char*, const char*, const thingsos_wd_upload_t*);
bool thingsos_wd_pending_load(const char*, const char*, thingsos_wd_upload_t*);
void thingsos_wd_pending_clear(const char*);
bool thingsos_wd_queue_put(const char*,const char*,const thingsos_wd_upload_t*,uint64_t*);
bool thingsos_wd_queue_peek(const char*,const char*,thingsos_wd_upload_t*,uint64_t*);
bool thingsos_wd_queue_ack(const char*,const char*,const char*,uint64_t);

/* Per-book, per-account multi-device sync metadata (last-writer-wins). */
typedef struct {
    uint32_t local_updated;
    uint32_t dismissed_updated;
    uint32_t uploaded_updated;
    uint32_t uploaded_offset;
    char uploaded_uid[64];
    bool conflict_pending;
    thingsos_wd_progress_t conflict_cloud;
} thingsos_wd_sync_t;
typedef enum {
    WD_SYNC_NONE = 0,
    WD_SYNC_SAME,
    WD_SYNC_LOCAL_NEWER,
    WD_SYNC_DISMISSED,
    WD_SYNC_CLOUD_NEWER
} thingsos_wd_sync_relation_t;
bool thingsos_wd_sync_load(const char*, const char*, const char*, thingsos_wd_sync_t*);
bool thingsos_wd_sync_save(const char*, const char*, const char*, const thingsos_wd_sync_t*);
thingsos_wd_sync_relation_t thingsos_wd_progress_compare(
    const thingsos_wd_progress_t*, const char*, uint32_t, bool, uint32_t,
    const thingsos_wd_sync_t*);

bool thingsos_wd_lines(
    const char*, const thingsos_weidu_session_t*, const char*, const char*,
    const thingsos_wd_map_t*, const thingsos_chapter_t*, thingsos_text_underline_t*, size_t,
    size_t*, size_t*);

/* Existing 16 KiB worker stack plus conservative free-heap headroom. */
bool thingsos_wd_memory_ready(size_t,size_t,size_t,size_t);
