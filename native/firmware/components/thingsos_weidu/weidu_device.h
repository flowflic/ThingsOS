/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "thingsos/weidu.h"
#include "thingsos/weidu_reading.h"
#include "thingsos/weidu_mp.h"
#include "thingsos/weidu_stream.h"
#include "thingsos/weidu_readtime.h"
#ifndef WEIDU_DEVICE_API
#define WEIDU_DEVICE_API
#endif
typedef enum {
    WD_DEVICE_LOGIN,
    WD_DEVICE_SYNC,
    WD_DEVICE_DOWNLOAD,
    WD_DEVICE_COVERS,
    WD_DEVICE_RESTORE,
    WD_DEVICE_LOGOUT,
    WD_DEVICE_NOTES, WD_DEVICE_PULL, WD_DEVICE_UPLOAD,
    WD_DEVICE_THOUGHTS,
    WD_DEVICE_MP_ACCOUNTS, WD_DEVICE_MP_ARTICLES, WD_DEVICE_MP_CONTENT,
    WD_DEVICE_ENTER, WD_DEVICE_REPORT, WD_DEVICE_PERSONAL, WD_DEVICE_NOTE_WRITE,
    WD_DEVICE_CHAPTER, WD_DEVICE_SEARCH, WD_DEVICE_SHELF_ADD,
    WD_DEVICE_RT_AUTH, WD_DEVICE_RT_UPLOAD
} thingsos_weidu_action_t;
WEIDU_DEVICE_API bool thingsos_weidu_device_init(void);
WEIDU_DEVICE_API void thingsos_weidu_state(thingsos_weidu_status_t*);
WEIDU_DEVICE_API bool thingsos_weidu_busy(void);
WEIDU_DEVICE_API bool thingsos_weidu_background_ready(void);
WEIDU_DEVICE_API bool thingsos_weidu_start(thingsos_weidu_action_t, const thingsos_weidu_book_t*);
WEIDU_DEVICE_API void thingsos_weidu_cancel(void);
WEIDU_DEVICE_API bool thingsos_mp_device_request(thingsos_weidu_action_t,const thingsos_weidu_book_t*,const thingsos_weidu_book_t*);
WEIDU_DEVICE_API bool thingsos_mp_device_page(const char*,size_t,thingsos_weidu_book_t*,size_t,size_t*,size_t*);
WEIDU_DEVICE_API bool thingsos_mp_device_cached(const thingsos_weidu_book_t*,const thingsos_weidu_book_t*,char*,size_t);
WEIDU_DEVICE_API bool thingsos_weidu_device_shelf(
    size_t, thingsos_weidu_book_t*, size_t, size_t*, size_t*);
WEIDU_DEVICE_API bool thingsos_weidu_device_cached(const char*, char*, size_t);

WEIDU_DEVICE_API void thingsos_weidu_device_covers(const thingsos_weidu_book_t*, size_t);
WEIDU_DEVICE_API unsigned thingsos_weidu_device_cover_serial(void);

WEIDU_DEVICE_API bool thingsos_weidu_device_partial(const char*);
WEIDU_DEVICE_API bool thingsos_weidu_device_chapter(const thingsos_weidu_book_t*,unsigned);
typedef enum {
    WD_START_OK, WD_START_BUSY, WD_START_OFFLINE, WD_START_STORAGE, WD_START_MEMORY,
    WD_START_INIT, WD_START_INVALID
} thingsos_weidu_start_result_t;
WEIDU_DEVICE_API thingsos_weidu_start_result_t thingsos_weidu_device_chapters(
    const thingsos_weidu_book_t*,unsigned,unsigned,bool prepare_cover);

WEIDU_DEVICE_API bool thingsos_weidu_device_search(size_t, thingsos_weidu_book_t*, size_t, size_t*, size_t*, const char*);
WEIDU_DEVICE_API bool thingsos_weidu_device_search_online(const char*);
WEIDU_DEVICE_API bool thingsos_weidu_device_membership(const thingsos_weidu_book_t*,size_t,bool*);

typedef struct {
    char stage[16];
    int http_status,transport_error,tls_error,socket_error;
    unsigned tls_flags;
    unsigned action;
    unsigned phase;
    thingsos_weidu_error_t error;
} thingsos_wd_diagnostic_t;
typedef struct {
    unsigned serial;
    thingsos_weidu_action_t action;
    char book[64], owner[33];
    bool expired;
    thingsos_wd_progress_t cloud;
    thingsos_weidu_error_t pull, progress, duration, notes;
    uint32_t pulled_at, uploaded_at;
    uint32_t duration_seconds,duration_at;
    bool session_ready;
    bool conflict;
    thingsos_wd_diagnostic_t diagnostic;
    thingsos_wd_thought_query_t thought_query;
    thingsos_weidu_error_t thoughts;
    thingsos_weidu_error_t personal,note_error;
    uint64_t note_book;
    uint32_t note_id,note_content_crc,note_state;
    char note_owner[33],note_bookmark_id[96],note_review_id[96];
} thingsos_wd_device_status_t;
WEIDU_DEVICE_API void thingsos_wd_device_status(thingsos_wd_device_status_t*);
WEIDU_DEVICE_API bool thingsos_wd_device_request(thingsos_weidu_action_t,const char*,const thingsos_wd_upload_t*);
WEIDU_DEVICE_API bool thingsos_wd_device_notes(const char*,thingsos_wd_notes_info_t*);
WEIDU_DEVICE_API bool thingsos_wd_device_note(const char*,unsigned,unsigned,thingsos_wd_note_t*);
WEIDU_DEVICE_API bool thingsos_wd_device_thought_request(const thingsos_wd_thought_query_t*);
WEIDU_DEVICE_API bool thingsos_wd_device_thought_info(const thingsos_wd_thought_query_t*,thingsos_wd_thought_info_t*);
WEIDU_DEVICE_API bool thingsos_wd_device_thought(const thingsos_wd_thought_query_t*,unsigned,thingsos_wd_note_t*);
WEIDU_DEVICE_API bool thingsos_wd_device_line_note(const char*,unsigned,const thingsos_wd_map_t*,const thingsos_chapter_t*,size_t,size_t,thingsos_wd_note_t*);
WEIDU_DEVICE_API bool thingsos_wd_device_personal_info(const char*,unsigned*);
WEIDU_DEVICE_API bool thingsos_wd_device_personal(const char*,unsigned,thingsos_wd_personal_t*);
WEIDU_DEVICE_API bool thingsos_wd_device_personal_find(const char*,const char*,uint32_t,uint32_t,thingsos_wd_personal_t*);
WEIDU_DEVICE_API bool thingsos_wd_device_annotation_request(const char*,uint64_t,const char*,const thingsos_annotation_t*);

WEIDU_DEVICE_API bool thingsos_wd_device_defer(const thingsos_wd_upload_t*);
WEIDU_DEVICE_API bool thingsos_wd_device_schedule(const thingsos_wd_upload_t*);
WEIDU_DEVICE_API bool thingsos_wd_device_sync_load(const char*,const char*,thingsos_wd_sync_t*);
WEIDU_DEVICE_API bool thingsos_wd_device_sync_save(const char*,const char*,const thingsos_wd_sync_t*);
/* Called by the UI only after a frame and while idle (or a power-sync gate). */
WEIDU_DEVICE_API void thingsos_wd_device_poll(bool);

typedef struct {
    thingsos_rt_summary_t queue;
    bool authorized, expired, blocked;
    thingsos_weidu_error_t error;
    uint32_t confirmed, confirmed_at;
    uint64_t uploaded; /* Lifetime successful Native seconds for this account. */
    bool uploaded_ready;
    uint64_t today_online,today_native;
    uint32_t today_day;
    bool today_ready;
    uint32_t capture_floor;
} thingsos_rt_device_status_t;
WEIDU_DEVICE_API void thingsos_wd_rt_status(thingsos_rt_device_status_t*);
WEIDU_DEVICE_API bool thingsos_wd_rt_flush(thingsos_rt_capture_t*);
WEIDU_DEVICE_API void thingsos_wd_rt_poll(bool,bool);
WEIDU_DEVICE_API bool thingsos_wd_rt_resume(void);

WEIDU_DEVICE_API bool thingsos_wd_device_lines(const char*,unsigned,const thingsos_wd_map_t*,const thingsos_chapter_t*,thingsos_text_underline_t*,size_t*,size_t*);
