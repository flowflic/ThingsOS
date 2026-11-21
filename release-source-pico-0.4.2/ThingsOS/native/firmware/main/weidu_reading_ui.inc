/* SPDX-License-Identifier: Apache-2.0
 * Main-task-only read overlays and asynchronous network handoff.
 */
#define WD_ROOT "/sdcard/.thingsos/weidu"
static bool wd_rt_set(bool enabled) {
    bool previous=app.wd_time_on,previous_rt=app.wd_rt_on;
    app.wd_time_on=app.wd_rt_on=enabled;
    if(!preferences_save()) {
        app.wd_time_on=previous;app.wd_rt_on=previous_rt;
        say("设置保存失败，请重试");
        app.dirty=true;return false;
    } else {
        /* A later enable must not report seconds read while this switch was off. */
        if(app.wd_active)app.wd_accounted=app.runtime.reading_ms;
        say(enabled?"已开启，再次打开书籍后生效":"已关闭，待发时长保留");
    }
    app.dirty=true;
    return true;
}
static void wd_rt_uploaded_text(char* text,size_t size,const thingsos_rt_device_status_t* s) {
    if(!s->today_ready||s->today_online>UINT64_MAX-s->today_native)snprintf(text,size,"今日已上传 暂不可读取");
    else {uint64_t total=s->today_online+s->today_native;
        snprintf(text,size,"今日已上传 %llu 小时 %02u 分 %02u 秒",
            (unsigned long long)(total/3600),(unsigned)(total/60%60),(unsigned)(total%60));}
}
#ifndef THINGSOS_HOST_UI
#include "weidu_readtime_ui.inc"
#endif
static void wd_forget_map(void) {
    reader_cache_clear();
    thingsos_wd_map_free(&app.wd_map);
    app.wd_map_attempted = false;
    app.wd_line_count = app.wd_draw_total = 0;
    app.chapter.underlines = NULL;
    app.chapter.underline_count = 0;
}
static bool wd_request(
    thingsos_weidu_action_t action, const char* id, const thingsos_wd_upload_t* in) {
    if (!media() || !thingsos_network_connected() || wall_time() < WD_SYNC_MIN_TIME) return false;
    if (!thingsos_wd_device_request(action, id, in)) {
        thingsos_weidu_state(&app.weidu);
        return false;
    }
    if (action == WD_DEVICE_UPLOAD || action==WD_DEVICE_REPORT) app.wd_local_failure = false;
    thingsos_weidu_state(&app.weidu);
    return true;
}
static bool wd_thought_same(const thingsos_wd_thought_query_t* a,const thingsos_wd_thought_query_t* b) {
    return !strcmp(a->id,b->id)&&!strcmp(a->uid,b->uid)&&a->begin==b->begin&&a->end==b->end&&a->max_idx==b->max_idx&&!strcmp(a->sync,b->sync);
}
static void wd_thought_close(void) {
    if(!app.wd_thought_open)return;
    if(app.wd_thought_loading)thingsos_weidu_cancel();
    app.wd_thought_open=app.wd_thought_loading=app.wd_thought_full=false;
    if(app.wd_notes_open)app.wd_long_open=false;
    thingsos_runtime_overlay(&app.runtime,app.wd_notes_open && app.wd_notes_reader,now());
}
static void wd_thought_load(bool refresh) {
    app.wd_thought_full=false;app.wd_long_page=0;
    memset(&app.wd_thought_info,0,sizeof(app.wd_thought_info));
    app.wd_thought_cached=thingsos_wd_device_thought_info(&app.wd_thought_query,&app.wd_thought_info);
    app.wd_thought_loading=false;app.wd_thought_error=WD_OK;
    if(refresh || !app.wd_thought_cached){
        app.wd_thought_loading=media() && thingsos_network_connected() && wall_time()>=WD_SYNC_MIN_TIME && thingsos_wd_device_thought_request(&app.wd_thought_query);
        if(!app.wd_thought_loading)app.wd_thought_error=wall_time()<WD_SYNC_MIN_TIME?WD_CLOCK:WD_NETWORK;
    }
    app.dirty=true;
}
static void wd_thought_enter(const char* id,const thingsos_wd_note_t* note) {
    if(!note->range || !note->length || note->begin>UINT32_MAX-note->length){say("此划线缺少可用位置，请刷新划线");return;}
    memset(&app.wd_thought_query,0,sizeof(app.wd_thought_query));
    strlcpy(app.wd_thought_query.id,id,sizeof(app.wd_thought_query.id));
    strlcpy(app.wd_thought_query.uid,note->uid,sizeof(app.wd_thought_query.uid));
    app.wd_thought_query.begin=note->begin;app.wd_thought_query.end=note->begin+note->length;
    size_t quote=strlen(note->text);if(quote>=sizeof(app.wd_thought_quote))quote=sizeof(app.wd_thought_quote)-1;
    while(quote && ((unsigned char)note->text[quote]&192)==128)--quote;
    memcpy(app.wd_thought_quote,note->text,quote);app.wd_thought_quote[quote]=0;
    app.wd_thought_heat=note->heat;
    app.wd_thought_page=app.wd_thought_row=0;
    memset(app.wd_thought_cursor,0,sizeof(app.wd_thought_cursor));
    app.wd_thought_open=true;app.menu=false;app.message[0]=0;
    thingsos_runtime_overlay(&app.runtime,true,now());wd_thought_load(false);
}
static void wd_thought_page_load(unsigned page) {
    app.wd_thought_page=page;app.wd_thought_row=0;
    app.wd_thought_query.max_idx=app.wd_thought_cursor[page].idx;
    strlcpy(app.wd_thought_query.sync,app.wd_thought_cursor[page].sync,sizeof(app.wd_thought_query.sync));
    wd_thought_load(false);
}
static bool wd_cloud_visible_page(void) {
    return app.opened && app.page == THINGSOS_UI_READING && app.wd_active;
}
static const char* wd_active_owner(void) {
    if (strlen(app.wd_owner) == 32) return app.wd_owner;
    if (strlen(app.wd_status.owner) == 32) return app.wd_status.owner;
    return "";
}
static void wd_cloud_book_title(const char* id, char* out, size_t cap) {
    out[0] = 0;
    if (!id || !*id) return;
    if (app.opened && !strcmp(app.wd_id, id) && app.read_title[0]) {
        strlcpy(out, app.read_title, cap);
        return;
    }
    for (size_t i = 0; i < app.book_count; ++i) {
        char cid[64];
        if (thingsos_weidu_cache_id(app.books[i].path, cid, sizeof(cid)) && !strcmp(cid, id)) {
            strlcpy(out, app.books[i].name, cap);
            return;
        }
    }
    for (size_t i = 0; i < app.weidu_count; ++i) {
        if (!strcmp(app.weidu_books[i].id, id)) {
            strlcpy(out, app.weidu_books[i].title, cap);
            return;
        }
    }
}
static bool wd_book_record_by_id(const char* id, thingsos_book_record_t** out) {
    *out = NULL;
    if (!id || !*id) return false;
    for (size_t i = 0; i < app.book_count; ++i) {
        char cid[64];
        if (thingsos_weidu_cache_id(app.books[i].path, cid, sizeof(cid)) && !strcmp(cid, id)) {
            *out = thingsos_records_book(&app.records, app.books[i].id, NULL, NULL);
            return *out != NULL;
        }
    }
    for (size_t i = 0; i < app.recent_count; ++i) {
        thingsos_book_record_t* r = thingsos_records_book(&app.records, app.recent_ids[i], NULL, NULL);
        char cid[64];
        if (r && thingsos_weidu_cache_id(r->path, cid, sizeof(cid)) && !strcmp(cid, id)) {
            *out = r;
            return true;
        }
    }
    return false;
}
static bool wd_cloud_local_summary(const char* cloud_id, unsigned* ch_out, unsigned* pct_out) {
    *ch_out = 0;
    *pct_out = 0;
    if (app.opened && !strcmp(app.wd_id, cloud_id)) {
        *ch_out = app.chapter_index;
        *pct_out = book_progress();
        return true;
    }
    thingsos_book_record_t* r = NULL;
    if (wd_book_record_by_id(cloud_id, &r)) {
        *ch_out = r->chapter;
        *pct_out = r->percent;
        return true;
    }
    return false;
}
static void wd_sync_record_accepted(
    const char* id, const char* owner, const thingsos_wd_progress_t* cloud) {
    thingsos_wd_sync_t sync = {0};
    thingsos_wd_device_sync_load(id, owner, &sync);
    uint32_t wall = wall_time() >= WD_SYNC_MIN_TIME ? (uint32_t)wall_time() : 0;
    uint32_t accepted = wall > cloud->updated ? wall : cloud->updated;
    if (accepted > sync.local_updated) sync.local_updated = accepted;
    if (cloud->updated > sync.uploaded_updated) sync.uploaded_updated = cloud->updated;
    strlcpy(sync.uploaded_uid, cloud->uid, sizeof(sync.uploaded_uid));
    sync.uploaded_offset = cloud->offset;
    sync.dismissed_updated = 0;
    sync.conflict_pending = false;
    memset(&sync.conflict_cloud, 0, sizeof(sync.conflict_cloud));
    thingsos_wd_device_sync_save(id, owner, &sync);
}
static bool wd_position(thingsos_wd_upload_t* in);
static void wd_prepare(void);
static thingsos_wd_sync_relation_t wd_evaluate_reading_cloud(
    const char* owner, const thingsos_wd_progress_t* cloud) {
    if (!cloud || !cloud->valid || !cloud->updated || !app.wd_id[0]) return WD_SYNC_NONE;
    thingsos_wd_sync_t sync = {0};
    thingsos_wd_device_sync_load(app.wd_id, owner, &sync);
    thingsos_wd_upload_t local_pos = {0};
    thingsos_weidu_chapter_t toc = {0};
    bool has_map = wd_position(&local_pos);
    bool has_toc = has_map && thingsos_wd_toc(WD_ROOT, app.wd_id, app.chapter_index, &toc);
    thingsos_wd_sync_relation_t rel = thingsos_wd_progress_compare(
        cloud, has_toc ? toc.uid : NULL, local_pos.offset, has_toc, app.wd_local_updated, &sync);
    if (rel == WD_SYNC_CLOUD_NEWER && has_toc && !strcmp(cloud->uid, toc.uid)) {
        size_t at = 0;
        if (thingsos_wd_local(&app.wd_map, cloud->offset, &at) && at == app.start)
            return WD_SYNC_SAME;
    }
    return rel;
}
static void wd_accept_cloud_position(void) {
    if(!app.wd_active || strcmp(app.wd_id,app.wd_cloud_book) || !app.wd_cloud_progress.valid)return;
    wd_sync_record_accepted(app.wd_id,wd_active_owner(),&app.wd_cloud_progress);
    uint32_t wall=wall_time()>=WD_SYNC_MIN_TIME?(uint32_t)wall_time():0;
    app.wd_local_updated=wall>app.wd_cloud_progress.updated?wall:app.wd_cloud_progress.updated;
    app.wd_anchor_chapter=app.chapter_index;
    app.wd_anchor_start=app.start;
    app.wd_session_moved=false;
    app.wd_cloud_cancelled=false;
}
static bool wd_apply_cloud_jump(void) {
    int index = thingsos_wd_chapter(WD_ROOT, app.wd_id, app.wd_cloud_progress.uid);
    if (index < 0) {
        say("云端章节未定位，保留本机位置");
        return false;
    }
    bool same_chapter = (unsigned)index == app.chapter_index && app.opened && app.chapter.text;
    if (!same_chapter && !load_chapter((unsigned)index)) {
        if (!online_cloud_target(app.wd_cloud_progress.offset)) {
            say("云端章节未定位，保留本机位置");
            return false;
        }
        return true;
    }
    wd_prepare();
    size_t at = 0;
    if (thingsos_wd_local(&app.wd_map, app.wd_cloud_progress.offset, &at))
        app.start = at;
    else
        say("无法精确定位，已到对应章首");
    reader_cache_clear();
    app.runtime.location.chapter = app.chapter_index;
    app.runtime.location.byte_offset = (uint32_t)app.start;
    app.pagination_valid = false;
    pagination_prepare();
    wd_accept_cloud_position();
    save_state();
    return true;
}
static void wd_opened(void) {
    wd_thought_close();
    app.wd_id[0] = 0;
    app.wd_cloud_prompt = false;
    app.wd_notes_open = false;
    app.wd_pull_pending = false;
    if (!thingsos_weidu_cache_id(app.read_path, app.wd_id, sizeof(app.wd_id))) return;
    app.wd_init_pending = true;
    thingsos_wd_device_status(&app.wd_status);
    strlcpy(app.wd_owner, app.wd_status.owner, sizeof(app.wd_owner));
    app.wd_started = app.runtime.reading_ms;
    app.wd_accounted=app.runtime.reading_ms;app.wd_report_after=0;
    app.wd_active = true;
#ifndef THINGSOS_HOST_UI
    wd_rt_begin();
#endif
    app.wd_enter_pending = true;
    app.wd_pull_pending = app.wd_pull_on;
    app.wd_anchor_chapter = app.chapter_index;
    app.wd_anchor_start = app.start;
    app.wd_session_moved = false;
    app.wd_cloud_cancelled = false;
    app.wd_was_online = media() && thingsos_network_connected() && wall_time() >= WD_SYNC_MIN_TIME;
    thingsos_wd_sync_t sync = {0};
    thingsos_wd_device_sync_load(app.wd_id, wd_active_owner(), &sync);
    app.wd_local_updated = sync.local_updated;
    if (sync.conflict_pending && sync.conflict_cloud.valid && app.wd_pull_on) {
        if (wd_evaluate_reading_cloud(wd_active_owner(), &sync.conflict_cloud) == WD_SYNC_CLOUD_NEWER) {
            strlcpy(app.wd_cloud_book, app.wd_id, sizeof(app.wd_cloud_book));
            app.wd_cloud_progress = sync.conflict_cloud;
            app.wd_status.cloud = sync.conflict_cloud;
            app.wd_cloud_prompt = true;
            thingsos_runtime_overlay(&app.runtime, true, now());
        } else {
            sync.conflict_pending = false;
            memset(&sync.conflict_cloud, 0, sizeof(sync.conflict_cloud));
            thingsos_wd_device_sync_save(app.wd_id, wd_active_owner(), &sync);
        }
    }
    if (app.wd_time_on && !app.wd_time_confirmed) {
        app.wd_consent = true;
        thingsos_runtime_overlay(&app.runtime, true, now());
    }
}
static void wd_prepare(void) {
    if (!app.wd_id[0] || !app.epub_open) return;
    if(app.wd_map_attempted) {
        if(app.wd_mapped_chapter==app.chapter_index && (!app.wd_map.source || app.wd_map.text==app.chapter.text))return;
        wd_forget_map();
    }
    app.wd_map_attempted = true;
    app.wd_mapped_chapter = app.chapter_index;
#ifndef THINGSOS_HOST_UI
    uint64_t prepare_at = now();
#endif
    if (!thingsos_wd_map(WD_ROOT, app.wd_id, app.chapter_index, &app.chapter, &app.wd_map)) return;
#ifndef THINGSOS_HOST_UI
    uint64_t map_ms = now() - prepare_at;
#endif
    /* Sequential chapter scan: two file opens, at most 512 records, 128 intervals. */
    reader_cache_clear();
    thingsos_wd_device_lines(
        app.wd_id, app.chapter_index, &app.wd_map, &app.chapter, app.wd_lines, &app.wd_line_count,
        &app.wd_draw_total);
    app.chapter.underlines = app.wd_lines;
    app.chapter.underline_count = app.wd_line_count;
#ifndef THINGSOS_HOST_UI
    ESP_LOGI("ThingsOS_weidu", "Prepare marks chapter=%u bytes=%u map=%u ms notes=%u ms lines=%u",
        app.chapter_index, (unsigned)app.chapter.bytes, (unsigned)map_ms,
        (unsigned)(now() - prepare_at - map_ms), (unsigned)app.wd_line_count);
#endif
    /* Mapping and notes completions can arrive between the first few turns.
       Attach them now and invalidate prefetched pixels, but include the new
       marks in the next requested frame instead of scanning the body again.
       Explicit note navigation already queues its own foreground frame. */
}
static bool wd_position(thingsos_wd_upload_t* in) {
    if (!app.wd_id[0] || !app.opened) return false;
    memset(in, 0, sizeof(*in));
    strlcpy(in->id, app.wd_id, sizeof(in->id));
    in->chapter = app.chapter_index;
    in->percent = book_progress();
    /* A synthetic cover has no source text. The chapter's actual start is 0. */
    if(app.chapter.cover_image && !app.start)return true;
    wd_prepare();
    bool mapped=thingsos_wd_source(&app.wd_map,app.start,&in->offset);
    if(!mapped) {
        /* A failed allocation during the first frame must not latch for the
           whole reading session. Rebuild once at an explicit sync boundary. */
        wd_forget_map();wd_prepare();
        mapped=thingsos_wd_source(&app.wd_map,app.start,&in->offset);
#ifndef THINGSOS_HOST_UI
        ESP_LOGI("ThingsOS_weidu","Position retry chapter=%u local=%u bytes=%u checkpoints=%u mapped=%d",app.chapter_index,
            (unsigned)app.start,(unsigned)app.chapter.bytes,(unsigned)app.wd_map.count,mapped);
#endif
    }
    return mapped;
}
static void wd_finish(void) {
    wd_thought_close();
    if (!app.wd_active) return;
    thingsos_runtime_tick(&app.runtime, now());
#ifndef THINGSOS_HOST_UI
    wd_rt_checkpoint(true);
#endif
    if (app.opened && !app.wd_cloud_prompt &&
        (app.chapter_index != app.wd_anchor_chapter || app.start != app.wd_anchor_start)) {
        app.wd_session_moved = true;
        app.wd_cloud_cancelled = false;
        uint32_t wall = wall_time() >= WD_SYNC_MIN_TIME ? (uint32_t)wall_time() : 0;
        if (wall > app.wd_local_updated) app.wd_local_updated = wall;
    }
    bool unanswered_prompt = app.wd_cloud_prompt;
    bool cancelled_idle = app.wd_cloud_cancelled && !app.wd_session_moved;
    uint64_t elapsed =
        app.runtime.reading_ms >= app.wd_started ? app.runtime.reading_ms - app.wd_started : 0;
    uint64_t pending=app.runtime.reading_ms>=app.wd_accounted?app.runtime.reading_ms-app.wd_accounted:0;
    thingsos_wd_upload_t in;
    bool position = wd_position(&in);
#ifndef THINGSOS_HOST_UI
    ESP_LOGI("ThingsOS_weidu","Reading exit book=%s active_seconds=%u pending_seconds=%u position=%d push=%d time=%d consent=%d moved=%d skip=%d",
        app.wd_id,(unsigned)(elapsed/1000),(unsigned)(pending/1000),position,app.wd_push_on,app.wd_time_on,app.wd_time_confirmed,
        app.wd_session_moved, unanswered_prompt || cancelled_idle);
#endif
    app.wd_active = false;
    app.wd_enter_pending = false;
    app.wd_pull_pending = false;
    app.wd_cloud_prompt = false;
    app.wd_cloud_cancelled = false;
    app.wd_notes_open = false;
    app.wd_consent = false;
    if (unanswered_prompt || cancelled_idle) return;
    if (!position) {
        app.wd_local_failure = true;
        app.wd_status.progress = WD_PROTOCOL;
        app.wd_status.duration = WD_CANCEL;
        return;
    }
    thingsos_wd_sync_t sync = {0};
    thingsos_wd_device_sync_load(app.wd_id, wd_active_owner(), &sync);
    uint32_t wall = wall_time() >= WD_SYNC_MIN_TIME ? (uint32_t)wall_time() : 0;
    if (app.wd_session_moved) {
        if (wall > sync.local_updated) sync.local_updated = wall;
        sync.conflict_pending = false;
        memset(&sync.conflict_cloud, 0, sizeof(sync.conflict_cloud));
    } else if (!sync.local_updated && wall) {
        sync.local_updated = wall;
    }
    if (app.wd_local_updated > sync.local_updated) sync.local_updated = app.wd_local_updated;
    thingsos_wd_device_sync_save(app.wd_id, wd_active_owner(), &sync);
    in.seconds = (uint32_t)(pending/1000>WD_REPORT_MAX_SECONDS?WD_REPORT_MAX_SECONDS:pending/1000);
    in.report = app.wd_time_on && app.wd_time_confirmed && !app.wd_rt_session;
    app.wd_last = in;
    app.wd_last.report = false;
    app.wd_last.seconds = 0;
    app.wd_last_valid = true;
    if (app.wd_push_on && !thingsos_wd_device_schedule(&in)) {
        app.wd_local_failure = true;
        app.wd_status.progress = WD_STORAGE;
        app.wd_status.duration = WD_CANCEL;
        app.wd_status.uploaded_at = (uint32_t)wall_time();
    }
}
static void wd_notes_enter(const char* id, bool reading) {
    if (!id || !*id) {
        say("划线仅对微读书籍生效");
        return;
    }
    strlcpy(app.wd_notes_id, id, sizeof(app.wd_notes_id));
    app.wd_notes_open = true;
    app.message[0] = 0;
    app.wd_notes_reader = reading;
    app.wd_long_open = false;
    app.wd_kind = app.wd_note_start = app.wd_long_page = 0;
    memset(&app.wd_info, 0, sizeof(app.wd_info));
    thingsos_wd_device_notes(id, &app.wd_info);
    app.menu = false;
    app.dirty = true;
    if (reading) thingsos_runtime_overlay(&app.runtime, true, now());
}
static void wd_poll(void) {
#ifndef THINGSOS_HOST_UI
    wd_rt_day_poll();
    if(!app.dirty)wd_rt_checkpoint(false);
#endif
    bool online_ready = media() && thingsos_network_connected() && wall_time() >= WD_SYNC_MIN_TIME;
    if (app.wd_active && app.opened) {
        if (!app.wd_cloud_prompt &&
            (app.chapter_index != app.wd_anchor_chapter || app.start != app.wd_anchor_start)) {
            app.wd_anchor_chapter = app.chapter_index;
            app.wd_anchor_start = app.start;
            app.wd_session_moved = true;
            app.wd_cloud_cancelled = false;
            uint32_t wall = online_ready ? (uint32_t)wall_time() : 0;
            if (wall > app.wd_local_updated) app.wd_local_updated = wall;
        }
        if (!app.wd_was_online && online_ready) {
            app.wd_was_online = true;
            if (app.wd_pull_on && !app.wd_cloud_prompt) app.wd_pull_pending = true;
        } else if (!online_ready) {
            app.wd_was_online = false;
        }
    }
    if (app.wd_cloud_prompt && !wd_cloud_visible_page()) app.wd_cloud_prompt = false;
    thingsos_wd_device_status_t next;
    thingsos_wd_device_status(&next);
    if (next.serial != app.wd_serial) {
        app.wd_serial = next.serial;
        if (app.wd_local_failure && next.action != WD_DEVICE_UPLOAD && next.action!=WD_DEVICE_REPORT &&
            next.action != WD_DEVICE_LOGIN && next.action != WD_DEVICE_LOGOUT) {
            next.progress = app.wd_status.progress;
            next.duration = app.wd_status.duration;
            next.uploaded_at = app.wd_status.uploaded_at;
        } else if (
            next.action == WD_DEVICE_UPLOAD || next.action==WD_DEVICE_REPORT || next.action == WD_DEVICE_LOGIN ||
            next.action == WD_DEVICE_LOGOUT)
            app.wd_local_failure = false;
        app.wd_status = next;
        if (strcmp(app.wd_owner, next.owner)) {
            strlcpy(app.wd_owner, next.owner, sizeof(app.wd_owner));
            app.wd_last_valid = false;
            wd_forget_map();
            wd_thought_close();
            app.wd_notes_open = false;
            app.wd_cloud_prompt = false;
            if (app.wd_active) app.wd_started=app.wd_accounted=app.runtime.reading_ms;
        }
        if(next.action==WD_DEVICE_THOUGHTS && app.wd_thought_open && wd_thought_same(&next.thought_query,&app.wd_thought_query)) {
            app.wd_thought_loading=false;app.wd_thought_error=next.thoughts;
            if(next.thoughts==WD_OK){app.wd_thought_cached=thingsos_wd_device_thought_info(&app.wd_thought_query,&app.wd_thought_info);app.wd_thought_row=0;app.wd_thought_full=false;}
            app.dirty=true;
        }
        if (next.action == WD_DEVICE_NOTES && next.notes == WD_OK && app.wd_notes_open) {
            thingsos_wd_device_notes(app.wd_notes_id, &app.wd_info);
            app.wd_long_open = false;
        }
        if (app.wd_id[0] && !strcmp(next.book, app.wd_id)) {
            if(next.action==WD_DEVICE_ENTER && next.session_ready && app.wd_active) {
                app.wd_accounted=app.runtime.reading_ms;app.wd_report_after=now()+WD_REPORT_INTERVAL_MS;
            }
            if (next.action == WD_DEVICE_NOTES && next.notes == WD_OK) wd_forget_map();
            bool expected_pull = app.wd_pull_pending;
            if (next.action == WD_DEVICE_PULL) app.wd_pull_pending = false;
            if (next.action == WD_DEVICE_PULL && next.pull == WD_OK && next.cloud.valid &&
                wd_cloud_visible_page() && expected_pull) {
                if (wd_evaluate_reading_cloud(next.owner, &next.cloud) == WD_SYNC_CLOUD_NEWER) {
                    strlcpy(app.wd_cloud_book, app.wd_id, sizeof(app.wd_cloud_book));
                    app.wd_cloud_progress = next.cloud;
                    app.wd_cloud_prompt = true;
                    thingsos_runtime_overlay(&app.runtime, true, now());
                } else if (!app.wd_cloud_prompt) {
                    thingsos_wd_sync_t sync = {0};
                    if (thingsos_wd_device_sync_load(app.wd_id, next.owner, &sync) && sync.conflict_pending) {
                        sync.conflict_pending = false;
                        memset(&sync.conflict_cloud, 0, sizeof(sync.conflict_cloud));
                        thingsos_wd_device_sync_save(app.wd_id, next.owner, &sync);
                    }
                }
            }
        }
        if (next.action == WD_DEVICE_UPLOAD && next.conflict && next.cloud.valid &&
            app.wd_pull_on && !app.wd_cloud_prompt && wd_cloud_visible_page() &&
            next.book[0] && !strcmp(app.wd_id, next.book) &&
            wd_evaluate_reading_cloud(next.owner, &next.cloud) == WD_SYNC_CLOUD_NEWER) {
            strlcpy(app.wd_cloud_book, next.book, sizeof(app.wd_cloud_book));
            app.wd_cloud_progress = next.cloud;
            app.wd_cloud_prompt = true;
            thingsos_runtime_overlay(&app.runtime, true, now());
            app.dirty = true;
        }
        if ((app.wd_sync_panel && app.page==THINGSOS_UI_WEIDU) || app.wd_notes_open ||
            (app.wd_cloud_prompt && wd_cloud_visible_page())) app.dirty = true;
    }
    /* Run only after the first frame has been presented. No network in draw. */
    if (app.opened && app.page == THINGSOS_UI_READING && !app.controls_open && !app.dirty && !weidu_foreground_active()) {
        if (app.wd_init_pending) {
            if (!thingsos_weidu_device_init()) return;
            thingsos_wd_device_status(&app.wd_status);
            thingsos_weidu_state(&app.weidu);
            strlcpy(app.wd_owner, app.wd_status.owner, sizeof(app.wd_owner));
            app.wd_init_pending = false;
        }
        /* Keep automatic SD/network work out of the first interactive frame
           and out of active reading. Explicit refresh/sync uses wd_request. */
        if(!app.reader_ready_at || now()<app.reader_ready_at+500 || now()<app.activity_at+500 ||
           app.menu || app.annotation_open || app.annotation_selecting || app.image_view.open ||
           app.footnote_open || thingsos_board_touch_contact_pending())return;
        wd_prepare();
        if(now()<app.reader_ready_at+2000 || now()<app.activity_at+2000 ||
           !thingsos_weidu_background_ready())return;
        if (app.wd_pull_pending && !app.wd_thought_open && !thingsos_weidu_busy()) {
            if (!wd_request(WD_DEVICE_PULL, app.wd_id, NULL)) {
                app.wd_pull_pending = false;
                app.wd_status.pull =
                    wall_time() < WD_SYNC_MIN_TIME                                     ? WD_CLOCK
                    : app.weidu.phase == WD_ERROR && strstr(app.weidu.message, "内存") ? WD_MEMORY
                                                                                       : WD_NETWORK;
            }
            /* Pending remains until this single request completes. */
            else
                app.wd_status.pull = WD_CANCEL;
        }
        if(app.wd_active && !app.wd_rt_session && app.wd_enter_pending && app.wd_push_on && app.wd_time_on && app.wd_time_confirmed &&
           !app.wd_pull_pending && !app.wd_cloud_prompt && !(app.wd_cloud_cancelled && !app.wd_session_moved) &&
           !app.wd_thought_open && !thingsos_weidu_busy() && online_ready) {
            thingsos_wd_upload_t in;
            if(wd_position(&in) && wd_request(WD_DEVICE_ENTER,app.wd_id,&in)) {
                app.wd_enter_pending=false;
            }
        }
        if(app.wd_active && !app.wd_rt_session && !app.wd_enter_pending && app.wd_push_on && app.wd_time_on && app.wd_time_confirmed &&
           app.wd_status.session_ready && !app.wd_status.expired && !strcmp(app.wd_status.book,app.wd_id) &&
           !app.wd_cloud_prompt && !(app.wd_cloud_cancelled && !app.wd_session_moved) &&
           !app.menu && !app.image_view.open && !app.footnote_open && !app.wd_thought_open && !thingsos_weidu_busy() &&
           thingsos_network_connected() && now()>=app.wd_report_after && app.runtime.reading_ms>=app.wd_accounted &&
           app.runtime.reading_ms-app.wd_accounted>=WD_REPORT_INTERVAL_MS) {
            uint64_t delta=app.runtime.reading_ms-app.wd_accounted;
            thingsos_wd_upload_t in;app.wd_report_after=now()+WD_REPORT_INTERVAL_MS;
            if(wd_position(&in)) {
                in.report=true;in.seconds=(uint32_t)(delta/1000>WD_REPORT_MAX_SECONDS?WD_REPORT_MAX_SECONDS:delta/1000);
                if(wd_request(WD_DEVICE_REPORT,in.id,&in)) {
                    if(delta/1000>WD_REPORT_MAX_SECONDS)app.wd_accounted=app.runtime.reading_ms;
                    else app.wd_accounted+=(uint64_t)in.seconds*1000;
#ifndef THINGSOS_HOST_UI
                    ESP_LOGI("ThingsOS_weidu","Reading interval book=%s measured_seconds=%u submitted_seconds=%u",in.id,(unsigned)(delta/1000),(unsigned)in.seconds);
#endif
                }
            }
        }
    }
}
static const char* wd_result(thingsos_weidu_error_t e) {
    return e == WD_OK       ? "成功"
           : e == WD_CANCEL ? "尚未完成"
           : e == WD_AUTH   ? "需重新扫码"
           : e == WD_CLOCK  ? "请先联网校准时间"
                            : thingsos_weidu_error_text(e);
}
static const char* wd_sync_failure_hint(const thingsos_wd_diagnostic_t* d) {
    if(d->error==WD_DNS)return "同步失败：域名解析失败";
    if(d->error==WD_TLS)return "同步失败：安全连接或证书校验失败";
    if(d->error==WD_AUTH)return "同步失败：微读登录失效，请重新扫码";
    if(d->error==WD_CLOCK)return "同步失败：设备时间未校准";
    if(d->error==WD_MEMORY)return "同步失败：可用内存不足";
    if(d->error==WD_STORAGE)return "同步失败：SD 卡读写失败";
    if(d->error==WD_LIMIT)return "同步失败：接口响应超过容量上限";
    if(d->error==WD_UNCONFIRMED)return "时长回执未确认，请勿重复补报";
    if(d->http_status==429)return "同步失败：请求过于频繁，请稍后重试";
    if(d->http_status>=500)return "同步失败：微读服务端暂时异常";
    if(d->http_status && d->http_status!=200)return "同步失败：服务端返回异常状态";
    if(d->error==WD_PROTOCOL)return "同步失败：接口数据或回执不兼容";
    if(d->error==WD_ACCESS)return "同步失败：访问被拒绝或需验证";
    if(d->error==WD_VERIFY)return "同步失败：需到微读官网完成验证";
    if(!d->stage[0])return "同步未完成：请求前检查失败";
    if(d->phase==2)return "同步失败：连接服务器未完成";
    if(!strcmp(d->stage,"READER"))return "同步失败：获取阅读会话未完成";
    if(!strcmp(d->stage,"CONFIG"))return "同步失败：获取计时配置未完成";
    if(!strcmp(d->stage,"POST"))return "同步失败：上传请求未完成";
    if(!strcmp(d->stage,"PULL"))return "同步失败：拉取云端进度未完成";
    return "同步失败：网络请求未完成";
}
static bool wd_sync_diagnostic_text(char* out,size_t cap) {
    const thingsos_wd_diagnostic_t* d=&app.wd_status.diagnostic;
    if(d->error==WD_OK || d->error==WD_CANCEL){if(cap)out[0]=0;return false;}
    snprintf(out,cap,"A%u/%s P%u E%u H%d X%x T%d F%x N%d",d->action,d->stage[0]?d->stage:"PRE",d->phase,
        (unsigned)d->error,d->http_status,(unsigned)d->transport_error,d->tls_error,d->tls_flags,d->socket_error);
    return true;
}
static const char* wd_sync_start_hint(void) {
    if(!media())return "同步未启动：请检查 SD 卡";
    if(!thingsos_network_connected())return "同步未启动：WiFi 未连接";
    if(wall_time()<WD_SYNC_MIN_TIME)return "同步未启动：设备时间未校准";
    if(thingsos_weidu_busy())return "同步未启动：正在下载或处理其他任务";
    return "同步未启动：任务未创建，请稍后重试";
}
static void wd_stamp(const char* prefix, uint32_t stamp, thingsos_weidu_error_t e, int y) {
    char info[160];
    time_t t = stamp;
    struct tm tm;
    if (stamp && localtime_r(&t, &tm))
        snprintf(
            info, sizeof(info), "%s %02d-%02d %02d:%02d · %s", prefix, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, wd_result(e));
    else
        snprintf(info, sizeof(info), "%s · %s", prefix, wd_result(e));
    label(info, 36, y, 24, 612);
}
static const char* wd_thought_empty_hint(void) {
    if(app.wd_thought_loading)return "正在加载这段划线的想法…";
    if(app.wd_thought_error==WD_AUTH)return "微读登录已失效，请重新扫码登录";
    if(app.wd_thought_error==WD_PROTOCOL)return "想法数据不兼容，请刷新后重试";
    if(app.wd_thought_error==WD_CLOCK)return "请先联网校准设备时间";
    if(app.wd_thought_error==WD_MEMORY)return "内存暂不足，请稍后重试";
    if(app.wd_thought_error==WD_NETWORK && thingsos_network_connected())return "加载未完成，请点下方刷新重试";
    return "暂无离线缓存，请联网加载";
}
/* Approved UE v2: 628 x 916 card, with reading visible around it. */
/* Dialog typography is independent of the book style (approved correction).
   These are physical panel pixels; UI scale must not inflate a card label. */
static unsigned wd_thought_rows(void) {return 2;}
static size_t wd_thought_body(thingsos_canvas_t* canvas,const char* text,size_t bytes,size_t start,
                              int x,int y,int width,int height) {
    unsigned scale=app.font.ui_size_percent;app.font.ui_size_percent=0;
    thingsos_font_size(&app.font,28);
    size_t end=thingsos_text_string_page(&app.font,canvas,text,bytes,start,x,y,width,height,150);
    app.font.ui_size_percent=scale;return end;
}
static size_t wd_thought_text_page(thingsos_canvas_t* canvas,size_t start) {
    return wd_thought_body(canvas,app.wd_note.text,strlen(app.wd_note.text),start,57,reader_landscape_frame()?360:570,reader_landscape_frame()?1100:570,reader_landscape_frame()?192:378);
}
static void wd_thought_list_back(void) {
    app.wd_thought_full=false;
    app.wd_thought_row=(app.wd_thought_row/wd_thought_rows())*wd_thought_rows();
}
static void wd_thought_card_frame_at(const char* title,int top,int height) {
    if(reader_landscape_frame()) {
        thingsos_rect(&app.canvas,28,top,1160,height,0);thingsos_rect(&app.canvas,30,top+2,1156,height-4,255);
        centered_label(title,450,top+50,32,316);label("×",1120,top+50,32,42);
    } else {
        thingsos_rect(&app.canvas,28,top,628,height,0);thingsos_rect(&app.canvas,30,top+2,624,height-4,255);
        centered_label(title,230,top+70,36,224);label("×",590,top+70,32,42);
        thingsos_rect(&app.canvas,57,top+100,570,1,180);
    }
}
static void wd_thought_card_frame(const char* title) {
    wd_thought_card_frame_at(title,reader_landscape_frame()?90:170,reader_landscape_frame()?570:916);
}
static bool wd_thought_landscape_draw(void) {
    unsigned scale=app.font.ui_size_percent;app.font.ui_size_percent=0;
    thingsos_font_weight(&app.font,0);
    wd_thought_card_frame("想法");
    if(app.wd_thought_full)label("‹ 列表",57,140,24,140);
    wd_thought_body(&app.canvas,app.wd_thought_quote,strlen(app.wd_thought_quote),0,57,164,1100,84);
    char info[160];snprintf(info,sizeof(info),"%u 人划线%s",(unsigned)app.wd_thought_heat,app.wd_thought_cached?" · 已缓存":"");
    label(info,57,280,22,950);thingsos_rect(&app.canvas,57,295,1100,1,180);
    if(app.wd_thought_full) {
        if(thingsos_wd_device_thought(&app.wd_thought_query,app.wd_thought_row,&app.wd_note)) {
            label(app.wd_note.author[0]?app.wd_note.author:"读书的人",57,330,24,1100);
            app.wd_long_end=wd_thought_text_page(&app.canvas,app.wd_long_page);
            unsigned page=1,total=0;size_t at=0,bytes=strlen(app.wd_note.text);
            do {size_t end=wd_thought_text_page(NULL,at);++total;if(at==app.wd_long_page)page=total;if(end<=at)break;at=end;}while(at<bytes);
            snprintf(info,sizeof(info),"%u / %u",page,total);
        }else{label("缓存无法读取，请返回列表刷新",57,390,25,1100);strlcpy(info,"—",sizeof(info));}
    }else {
        label("读者想法",57,330,28,800);label(app.wd_thought_loading?"取消":"刷新",1060,330,24,97);
        if(app.wd_thought_cached) {
            if(!app.wd_thought_info.count)label("这段划线暂无可见想法",57,410,25,1100);
            for(unsigned i=0;i<2 && app.wd_thought_row+i<app.wd_thought_info.count;++i) {
                if(!thingsos_wd_device_thought(&app.wd_thought_query,app.wd_thought_row+i,&app.wd_note))continue;
                int x=57+(int)i*582;label(app.wd_note.author[0]?app.wd_note.author:"读书的人",x,380,24,518);
                wd_thought_body(&app.canvas,app.wd_note.text,strlen(app.wd_note.text),0,x,400,518,84);
                label("阅读全文 →",x+300,530,21,218);
            }
        }else label(wd_thought_empty_hint(),57,410,25,1100);
        if(app.wd_thought_loading)label("正在加载，可关闭返回阅读",57,565,22,1100);
        else if(app.wd_thought_error!=WD_OK)label(thingsos_weidu_error_text(app.wd_thought_error),57,565,22,1100);
        unsigned total=(app.wd_thought_info.count+1)/2;
        snprintf(info,sizeof(info),"%u / %u%s",app.wd_thought_row/2+1,total?total:1,app.wd_thought_info.more?" +":"");
    }
    weidu_button("上一页",57,580,180);weidu_button("下一页",979,580,180);centered_label(info,450,625,24,316);
    app.font.ui_size_percent=scale;return true;
}
static bool wd_thought_draw(void) {
    if(!app.wd_thought_open)return false;
    if(reader_landscape_frame())return wd_thought_landscape_draw();
    unsigned ui_scale=app.font.ui_size_percent;app.font.ui_size_percent=0;
    thingsos_font_weight(&app.font,0);
    wd_thought_card_frame("想法");
    if(app.wd_thought_full)label("‹ 列表",57,240,24,140);
    label(app.wd_thought_full?"对应划线":"划线原文",57,310,22,570);
    thingsos_rect(&app.canvas,57,330,3,84,100);
    wd_thought_body(&app.canvas,app.wd_thought_quote,strlen(app.wd_thought_quote),0,78,328,549,84);
    char info[160];
    if(app.wd_thought_heat)snprintf(info,sizeof(info),"%u 人划线",(unsigned)app.wd_thought_heat);
    else strlcpy(info,"划线人数未知",sizeof(info));
    label(info,57,450,22,360);
    if(app.wd_thought_cached)label("已缓存",526,450,22,101);
    if(app.wd_thought_full){
        if(thingsos_wd_device_thought(&app.wd_thought_query,app.wd_thought_row,&app.wd_note)){
            label(app.wd_note.author[0]?app.wd_note.author:"读书的人",57,510,24,570);
            label("这段划线下的想法",57,545,21,570);
            app.wd_long_end=wd_thought_text_page(&app.canvas,app.wd_long_page);
            unsigned page=1,total=0;size_t at=0,bytes=strlen(app.wd_note.text);
            do {size_t end=wd_thought_text_page(NULL,at);++total;
                if(at==app.wd_long_page)page=total;
                if(end<=at)break;
                at=end;
            }while(at<bytes);
            snprintf(info,sizeof(info),"%u / %u",page,total);
        }else {label("缓存无法读取，请返回列表刷新",57,620,25,570);strlcpy(info,"—",sizeof(info));}
    }else{
        label("读者想法",57,510,28,230);
        snprintf(info,sizeof(info),"%u 条",(unsigned)app.wd_thought_info.count);label(info,245,510,22,180);
        label(app.wd_thought_loading?"取消":"刷新",560,510,24,67);
        if(app.wd_thought_cached){
            if(!app.wd_thought_info.count)label("这段划线暂无可见想法",57,620,25,570);
            unsigned rows=wd_thought_rows();
            for(unsigned i=0;i<rows && app.wd_thought_row+i<app.wd_thought_info.count;++i){
                if(!thingsos_wd_device_thought(&app.wd_thought_query,app.wd_thought_row+i,&app.wd_note))continue;
                int y=550+(int)i*176;
                label(app.wd_note.author[0]?app.wd_note.author:"读书的人",57,y+30,24,570);
                wd_thought_body(&app.canvas,app.wd_note.text,strlen(app.wd_note.text),0,57,y+48,570,84);
                label("阅读全文 →",440,y+154,21,187);
            }
        }else label(wd_thought_empty_hint(),57,620,25,570);
        if(app.wd_thought_loading)label("正在加载，可关闭返回阅读",57,940,22,570);
        else if(app.wd_thought_error!=WD_OK)label(thingsos_weidu_error_text(app.wd_thought_error),57,940,22,570);
        unsigned rows=wd_thought_rows(),total=(app.wd_thought_info.count+rows-1)/rows;
        snprintf(info,sizeof(info),"%u / %u%s",app.wd_thought_row/rows+1,total?total:1,app.wd_thought_info.more?" +":"");
    }
    thingsos_rect(&app.canvas,57,960,570,1,180);
    weidu_button("上一页",57,990,180);weidu_button("下一页",447,990,180);
    centered_label(info,245,1035,24,194);
    app.font.ui_size_percent=ui_scale;
    return true;
}
static bool wd_thought_tap(int x,int y) {
    if(!app.wd_thought_open)return false;
    if(reader_landscape_frame()) {
        /* Map visible wide controls to the existing action handler. */
        if(x<28 || x>=1188 || y<90 || y>=660 || (x>=1100 && y<150)){wd_thought_close();app.dirty=true;return true;}
        if(app.wd_thought_full && x<210 && y<150){x=100;y=220;}
        else if(y>=580 && y<648){if(x>=57 && x<237)x=100;else if(x>=979 && x<1159)x=500;else return true;y=1010;}
        else if(!app.wd_thought_full && x>=1050 && y>=300 && y<345){x=590;y=510;}
        else if(!app.wd_thought_full && y>=350 && y<550){unsigned row=x>=608;x=100;y=580+(int)row*176;}
        else return true;
    }

    if(x<28 || x>=656 || y<170 || y>=1086 || (x>=570 && y<270)){
        wd_thought_close();app.dirty=true;return true;
    }
    unsigned rows=wd_thought_rows();
    if(app.wd_thought_full){
        if(y<270 && x<210){wd_thought_list_back();app.dirty=true;return true;}
        if(y>=990 && y<1058 && thingsos_wd_device_thought(&app.wd_thought_query,app.wd_thought_row,&app.wd_note)){
            if(x>=57 && x<237 && app.wd_long_page){
                size_t at=0,previous=0;
                while(at<app.wd_long_page){previous=at;size_t end=wd_thought_text_page(NULL,at);if(end<=at)break;at=end;}
                app.wd_long_page=(unsigned)previous;
            }else if(x>=447 && x<627 && app.wd_long_end<strlen(app.wd_note.text))app.wd_long_page=(unsigned)app.wd_long_end;
            app.dirty=true;
        }
        return true;
    }
    if(y>=475 && y<535 && x>=530){if(app.wd_thought_loading)thingsos_weidu_cancel();else wd_thought_load(true);return true;}
    if(y>=550 && y<550+(int)rows*176 && app.wd_thought_cached){unsigned row=(unsigned)(y-550)/176;
        if(app.wd_thought_row+row<app.wd_thought_info.count){app.wd_thought_row+=row;app.wd_thought_full=true;app.wd_long_page=0;app.dirty=true;}return true;}
    if(y>=990 && y<1058 && !app.wd_thought_loading){
        if(x>=57 && x<237){if(app.wd_thought_row>=rows)app.wd_thought_row-=rows;else if(app.wd_thought_page){wd_thought_page_load(app.wd_thought_page-1);if(app.wd_thought_info.count)app.wd_thought_row=((app.wd_thought_info.count-1)/rows)*rows;}}
        else if(x>=447 && x<627){if(app.wd_thought_row+rows<app.wd_thought_info.count)app.wd_thought_row+=rows;
            else if(app.wd_thought_info.more && app.wd_thought_page<15){unsigned next=app.wd_thought_page+1;app.wd_thought_cursor[next].idx=app.wd_thought_info.next_idx;strlcpy(app.wd_thought_cursor[next].sync,app.wd_thought_info.sync,sizeof(app.wd_thought_cursor[next].sync));wd_thought_page_load(next);}
            else say(app.wd_thought_page==15?"已达本次浏览上限":"已到末尾");}
        app.dirty=true;
    }
    return true;
}
static bool wd_draw(void) {
#ifndef THINGSOS_HOST_UI
    if(wd_rt_draw())return true;
#endif
    if(app.wd_thought_open && app.page==THINGSOS_UI_READING)return false;
    if(wd_thought_draw())return true;
    if (app.wd_consent) {
        header("阅读时长同步");
        label("将本次有效阅读秒数上报微信读书", 36, 355, 30, 612);
        label("会写入账号统计，可随时在微读同步中关闭。", 36, 435, 25, 612);
        label("请求结果不确定时不会自动重试或补报。", 36, 495, 25, 612);
        weidu_button("暂不启用", 36, 850, 292);
        weidu_button("同意启用", 356, 850, 292);
        return true;
    }
    if (app.wd_cloud_prompt && wd_cloud_visible_page()) {
        header("云端进度同步");
        const char* cloud_id = app.wd_cloud_book[0] ? app.wd_cloud_book : app.wd_id;
        char title[128] = {0};
        wd_cloud_book_title(cloud_id, title, sizeof(title));
        int y_cursor = 295;
        if (title[0]) {
            label(title, 36, y_cursor, 28, 612);
            y_cursor += 58;
        }
        label("云端进度和本地阅读进度不一致，是否更新为云端进度？", 36, y_cursor, 28, 612);
        y_cursor += 96;
        int cloud_index = thingsos_wd_chapter(WD_ROOT, cloud_id, app.wd_cloud_progress.uid);
        char cloud_info[160], local_info[160];
        if (cloud_index >= 0)
            snprintf(
                cloud_info, sizeof(cloud_info), "云端进度：第 %d 章 · %u%%", cloud_index + 1,
                (unsigned)app.wd_cloud_progress.percent);
        else
            snprintf(cloud_info, sizeof(cloud_info), "云端进度：%u%%", (unsigned)app.wd_cloud_progress.percent);
        label(cloud_info, 36, y_cursor, 26, 612);
        y_cursor += 54;
        unsigned local_ch = 0, local_pct = 0;
        if (wd_cloud_local_summary(cloud_id, &local_ch, &local_pct))
            snprintf(local_info, sizeof(local_info), "本机进度：第 %u 章 · %u%%", local_ch + 1, local_pct);
        else
            snprintf(local_info, sizeof(local_info), "本机进度：暂未记录");
        label(local_info, 36, y_cursor, 26, 612);
        y_cursor += 62;
        label("确认后跳转到最新云端进度；取消则仍以本机上次进度为准。", 36, y_cursor, 23, 612);
        weidu_button("取消", 36, 850, 292);
        weidu_button("确认更新", 356, 850, 292);
        return true;
    }
    if (app.wd_notes_open) {
        header("划线与书评");
        thingsos_font_size(ui_font(),25);
        thingsos_label_right(ui_font(),&app.canvas,"返回",550,154,100);
        static const char* kinds[] = { "热门划线", "我的划线", "热门书评" };
        for (unsigned i = 0; i < 3; ++i) {
            weidu_button(kinds[i],36+(int)i*208,215,196);
            if(i==app.wd_kind)thingsos_rect(&app.canvas,36+(int)i*208,279,196,4,0);
        }
        char info[160];
        snprintf(
            info, sizeof(info), "已缓存 %u / 至少 %u 条%s",
            (unsigned)app.wd_info.count[app.wd_kind], (unsigned)app.wd_info.total[app.wd_kind],
            app.wd_info.limited[app.wd_kind] ? " · 已截断" : "");
        label(info, 36, 320, 23, 612);
        if (app.wd_draw_total > WD_DRAW_LIMIT) {
            snprintf(
                info, sizeof(info), "本章 %u 条，绘制最多 %u 条", (unsigned)app.wd_draw_total,
                WD_DRAW_LIMIT);
            label(info, 36, 350, 22, 612);
        }
        if (app.wd_long_open) {
            thingsos_font_size(&app.font, 28);
            size_t bytes = strlen(app.wd_note.text);
            app.wd_long_end = reader_annotation_page(
                &app.canvas, app.wd_note.text, bytes, app.wd_long_page, 36, 370, 612,
                app.wd_kind<2?400:470, 145);
            if (app.wd_note.truncated) label("正文过长，缓存已截断", 36, app.wd_kind==0?795:885, 23, 612);
            int index = thingsos_wd_chapter(WD_ROOT, app.wd_notes_id, app.wd_note.uid);
            if(app.wd_kind<2)weidu_button(app.wd_kind?"查看我的想法":"查看这段想法",36,825,612);
            weidu_button(
                app.wd_notes_reader && index >= 0 ? "跳到对应章节" : "未定位 / 仅浏览", 36, 915,
                612);
            weidu_button("返回列表", 36, 1015, 292);
            weidu_button(app.wd_long_end < bytes ? "继续阅读" : "已到末尾", 356, 1015, 292);
        } else {
            if (!app.wd_info.count[app.wd_kind])
                label("暂无缓存，联网后可手动刷新", 36, 500, 27, 612);
            for (unsigned i = 0; i < 3 && app.wd_note_start + i < app.wd_info.count[app.wd_kind];
                 ++i) {
                if (!thingsos_wd_device_note(
                        app.wd_notes_id, app.wd_kind, app.wd_note_start + i, &app.wd_note))
                    continue;
                int y = 395 + (int)i * 170;
                reader_annotation_label(app.wd_note.text,36,y,612);
                int index = thingsos_wd_chapter(WD_ROOT, app.wd_notes_id, app.wd_note.uid);
                snprintf(
                    info, sizeof(info), "%.80s%s%.48s%s",
                    index < 0                ? "未定位"
                    : app.wd_note.chapter[0] ? app.wd_note.chapter
                                             : "已定位章节",
                    app.wd_note.author[0] ? " · " : "", app.wd_note.author,
                    app.wd_note.truncated ? " · 文字已截断" : "");
                label(info, 36, y + 48, 22, 612);
                label("全文", 548, y + 82, 22, 100);
                if (app.wd_kind == 0) {
                    snprintf(info, sizeof(info), "热度 %u", (unsigned)app.wd_note.heat);
                    label(info, 36, y + 82, 22, 612);
                }
                thingsos_rect(&app.canvas, 36, y + 115, 612, 1, 210);
            }
            if (app.weidu.busy && app.weidu.background)
                label("正在刷新，可继续离线浏览", 36, 880, 23, 612);
            else if (app.message[0])label(app.message,36,880,23,612);
            else if (
                !strcmp(app.wd_status.book, app.wd_notes_id) && app.wd_status.notes != WD_OK &&
                app.wd_status.notes != WD_CANCEL)
                label(
                    app.wd_status.notes == WD_AUTH ? "需重新扫码；旧缓存保留"
                    : app.wd_status.notes == WD_ACCESS ? "划线接口鉴权未通过，登录与旧缓存保留"
                                                   : "刷新失败，保留上次完整缓存",
                    36, 880, 23, 612);
            else if(!strcmp(app.wd_status.book,app.wd_notes_id)&&app.wd_status.notes==WD_OK)
                label("刷新完成，缓存已保存到 SD 卡",36,880,23,612);
            weidu_button(app.weidu.busy ? "取消刷新" : "刷新三类数据", 36, 915, 612);
            weidu_button("上一页", 36, 1015, 292);
            weidu_button("下一页", 356, 1015, 292);
        }
        return true;
    }
    if (app.wd_sync_panel && app.page == THINGSOS_UI_WEIDU) {
#ifndef THINGSOS_HOST_UI
        wd_rt_ui_day=wd_rt_display_day();
#endif
        header("微读同步");
        label("微读同步",36,154,32,480);
        thingsos_font_size(ui_font(), 25);
        /* The UI font has a 2 px trailing bearing: align visible ink to x=647. */
        thingsos_label_right(ui_font(), &app.canvas, "返回", 550, 154, 100);
        weidu_button(weidu_sync_running()?"书架同步中…":"书架同步", 36, 250, 612);
        char diagnostic[128];
        if(wd_sync_diagnostic_text(diagnostic,sizeof(diagnostic))) {
            label(wd_sync_failure_hint(&app.wd_status.diagnostic),36,345,22,612);
            char* details=strstr(diagnostic," X");
            if(details)*details++=0;
            label(diagnostic,36,369,15,612);
            if(details)label(details,36,393,15,612);
        }
        wd_stamp("上次拉取", app.wd_status.pulled_at, app.wd_status.pull, 438);
        wd_stamp("进度上传", app.wd_status.uploaded_at, app.wd_status.progress, 480);
        char duration[160];
        thingsos_rt_device_status_t totals;thingsos_wd_rt_status(&totals);
        char today[128];wd_rt_uploaded_text(today,sizeof(today),&totals);
        snprintf(duration,sizeof(duration),"阅读时长：%s",today);
        label(duration,36,524,24,612);
        if (app.wd_status.expired) label("需重新登录，待上传时长保留", 36, 577, 24, 612);
        else if(app.wd_status.duration==WD_UNCONFIRMED||totals.queue.unknown)label("待确认时长未计入今日累计",36,577,24,612);
        else if(!totals.today_ready)label("成功记录暂不可读取，待发时长保留",36,577,23,612);
        else if(app.wd_time_on&&!totals.authorized)label("离线补传需授权，可进入时长明细处理",36,577,22,612);
        else label("本机已确认记录 · 按阅读日期归档",36,577,23,612);
        char info[128];
        snprintf(info, sizeof(info), "打开时拉取：%s", app.wd_pull_on ? "开" : "关");
        weidu_button(info, 36, 630, 612);
        snprintf(info, sizeof(info), "退出时上传：%s", app.wd_push_on ? "开" : "关");
        weidu_button(info, 36, 720, 612);
        snprintf(
            info, sizeof(info), "上传阅读时长（含离线）：%s",
            app.wd_time_on ? (app.wd_time_confirmed ? "开" : "待确认") : "关");
        weidu_button(info, 36, 810, 612);
        weidu_button("立即同步进度", 36, 915, 612);
        weidu_button("阅读时长明细", 36, 1000, 612);
        return true;
    }
    return false;
}
static void wd_jump_note(void) {
    int index = thingsos_wd_chapter(WD_ROOT, app.wd_notes_id, app.wd_note.uid);
    if (index < 0) {
        say("未定位，无法跳转");
        return;
    }
    thingsos_wd_note_t* chosen = thingsos_image_buffer(sizeof(*chosen));
    if (!chosen) {
        say("内存不足，请重试");
        return;
    }
    *chosen = app.wd_note;
    if (load_chapter((unsigned)index)) {
        wd_prepare();
        size_t a, b;
        if (thingsos_wd_map_range(&app.wd_map, &app.chapter, chosen, &a, &b)) {
            app.start = a;
            size_t slot =
                app.wd_line_count < WD_DRAW_LIMIT ? app.wd_line_count++ : WD_DRAW_LIMIT - 1;
            app.wd_lines[slot] = (thingsos_text_underline_t){ a, b, app.wd_kind == 1 };
            app.chapter.underlines = app.wd_lines;
            app.chapter.underline_count = app.wd_line_count;
        } else
            say(app.wd_kind == 2 ? "已跳到书评对应章首" : "已跳到对应章首，划线未通过定位校验");
        app.runtime.location.chapter = app.chapter_index;
        app.runtime.location.byte_offset = (uint32_t)app.start;
        reader_cache_clear();
        app.wd_notes_open = false;
        app.wd_long_open = false;
        thingsos_runtime_overlay(&app.runtime, false, now());
        app.dirty = true;
    } else if(online_note_target(chosen))
        say("正在加载划线所在章节，完成后自动定位");
    else
        say("章节无法打开，保留阅读记录");
    free(chosen);
}
static bool wd_tap(int x, int y) {
#ifndef THINGSOS_HOST_UI
    if(wd_rt_tap(x,y))return true;
#endif
    if(wd_thought_tap(x,y))return true;
    if (app.wd_consent) {
        if (y >= 850 && y < 918) {
            bool confirmed=app.wd_time_confirmed;
            if(x>=356)app.wd_time_confirmed=true;
            if(!wd_rt_set(x>=356))app.wd_time_confirmed=confirmed;
            app.wd_consent = false;
            thingsos_runtime_overlay(&app.runtime, false, now());
            app.dirty = true;
        }
        return true;
    }
    if (app.wd_cloud_prompt && wd_cloud_visible_page()) {
        if (y >= 850 && y < 918) {
            const char* target_id = app.wd_cloud_book[0] ? app.wd_cloud_book : app.wd_id;
            const char* owner = wd_active_owner();
            if (x >= 356) {
                app.wd_cloud_prompt = false;
                app.wd_pull_pending = false;
                if (app.opened && app.page == THINGSOS_UI_READING && !strcmp(app.wd_id, target_id))
                    wd_apply_cloud_jump();
            } else {
                thingsos_wd_sync_t sync = {0};
                thingsos_wd_device_sync_load(target_id, owner, &sync);
                if (app.wd_cloud_progress.updated > sync.dismissed_updated)
                    sync.dismissed_updated = app.wd_cloud_progress.updated;
                sync.conflict_pending = false;
                memset(&sync.conflict_cloud, 0, sizeof(sync.conflict_cloud));
                thingsos_wd_device_sync_save(target_id, owner, &sync);
                app.wd_cloud_cancelled = app.opened && !strcmp(app.wd_id, target_id);
                app.wd_cloud_prompt = false;
                app.wd_pull_pending = false;
            }
            thingsos_runtime_overlay(&app.runtime, false, now());
            app.dirty = true;
        }
        return true;
    }
    if (app.wd_notes_open) {
        if (y >= 100 && y < 195 && (x < 112 || (x >= 548 && x < 648))) {
            app.wd_notes_open = false;
            app.wd_long_open = false;
            if (app.wd_notes_reader) thingsos_runtime_overlay(&app.runtime, false, now());
            app.dirty = true;
            return true;
        }
        if (y >= 215 && y < 285 && x >= 36 && x < 648) {
            if((x-36)%208>=196)return true;
            app.wd_kind = (unsigned)(x - 36) / 208;
            app.wd_note_start = 0;
            app.wd_long_open = false;
            app.dirty = true;
            return true;
        }
        if (app.wd_long_open) {
            if(app.wd_kind<2 && y>=825 && y<893){if(app.wd_kind)wd_own_thought_enter(app.wd_notes_id,&app.wd_note);else wd_thought_enter(app.wd_notes_id,&app.wd_note);return true;}
            if (y >= 915 && y < 983 && app.wd_notes_reader) wd_jump_note();
            if (y >= 1015 && y < 1083) {
                if (x < 328)
                    app.wd_long_open = false;
                else if (x >= 356 && app.wd_long_end < strlen(app.wd_note.text))
                    app.wd_long_page = (unsigned)app.wd_long_end;
                app.dirty = true;
            }
        } else {
            if (y >= 350 && y < 860) {
                unsigned row = (unsigned)(y - 350) / 170;
                if (row < 3 && app.wd_note_start + row < app.wd_info.count[app.wd_kind] &&
                    thingsos_wd_device_note(
                        app.wd_notes_id, app.wd_kind, app.wd_note_start + row, &app.wd_note)) {
                    if (app.wd_notes_reader && x < 530 &&
                        thingsos_wd_chapter(WD_ROOT, app.wd_notes_id, app.wd_note.uid) >= 0)
                        wd_jump_note();
                    else
                        app.wd_long_open = true;
                    app.wd_long_page = 0;
                    app.dirty = true;
                }
            }
            if (y >= 915 && y < 983) {
                app.message[0]=0;
                if (app.weidu.busy) {
                    thingsos_weidu_cancel();
                    return true;
                }
                if (!wd_request(WD_DEVICE_NOTES, app.wd_notes_id, NULL))
                    say(wall_time() < WD_SYNC_MIN_TIME ? "请先联网校准时间"
                                                       : "刷新未启动，请联网或等待当前操作结束");
            }
            if (y >= 1015 && y < 1083) {
                if (x < 328)
                    app.wd_note_start = app.wd_note_start > 3 ? app.wd_note_start - 3 : 0;
                else if (x >= 356 && app.wd_note_start + 3 < app.wd_info.count[app.wd_kind])
                    app.wd_note_start += 3;
                app.dirty = true;
            }
        }
        return true;
    }
    if (app.wd_sync_panel && app.page == THINGSOS_UI_WEIDU) {
        if (y >= 100 && y < 195 && x >= 548 && x < 648) {
            app.wd_sync_panel = false;
            app.dirty = true;
        } else if (y >= 250 && y < 318)
            weidu_begin(WD_DEVICE_SYNC, NULL);
        else if (y >= 630 && y < 698) {
            app.wd_pull_on = !app.wd_pull_on;
            preferences_save();
            app.dirty = true;
        } else if (y >= 720 && y < 788) {
            app.wd_push_on = !app.wd_push_on;
            if (!app.wd_push_on) thingsos_wd_device_defer(NULL);
            preferences_save();
            app.dirty = true;
        } else if (y >= 810 && y < 878) {
            if (!app.wd_time_confirmed) {
                app.wd_consent = true;
            } else
                wd_rt_set(!app.wd_time_on);
            app.dirty = true;
        } else if (y >= 915 && y < 983) {
            if (!app.wd_last_valid)
                say("请先打开并阅读一本微读缓存书籍");
            else if (!wd_request(WD_DEVICE_UPLOAD, app.wd_last.id, &app.wd_last))
                say(wd_sync_start_hint());
        }
#ifndef THINGSOS_HOST_UI
        else if(x>=36 && x<648 && y>=1000 && y<1068) {
            app.wd_rt_panel=true;app.wd_rt_details=false;app.dirty=true;
            if(wd_rt_capture)wd_rt_checkpoint(true);
        }
#endif
        return true;
    }
    return false;
}
