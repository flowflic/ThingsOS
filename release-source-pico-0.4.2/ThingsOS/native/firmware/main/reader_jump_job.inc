// SPDX-License-Identifier: Apache-2.0
/* The UI owns the index, font and SD. Yield to its event loop after a few
   layout rows; do not run a second renderer or hold it in a full-book loop. */
static void reader_jump_pending_tap(int x,int y);
static void reader_jump_cancel(void) {
    if(!app.jump_pending)return;
    app.jump_pending=false;app.menu=app.jump=true;app.dirty=true;
    ESP_LOGI("ThingsOS_reader","Jump cancelled after=%u ms pages=%u",
        (unsigned)(now()-app.jump_started_at),(unsigned)app.page_count);
}
static bool reader_jump_finish(void) {
    size_t target=app.jump_by_percent
        ?thingsos_page_percent((unsigned)app.jump_target,app.page_count):app.jump_target;
    if(!go_page(target)) {
        app.jump_pending=false;
        say(target>=app.page_count?"页码超出全书范围，保留原位置":"章节加载失败，保留原位置");
        return false;
    }
    app.jump_pending=false;app.menu=app.jump=false;
    app.message[0]=0;app.dirty=true;save_state();
    ESP_LOGI("ThingsOS_reader","Jump ready target=%u pages=%u chapter=%u offset=%u elapsed=%u ms",
        (unsigned)target+1,(unsigned)app.page_count,app.chapter_index,(unsigned)app.start,
        (unsigned)(now()-app.jump_started_at));
    return true;
}
static bool reader_jump_request(bool percent,size_t target) {
    if(app.epub.weidu_online) {
        if(!percent){say("在线阅读请按目录或阅读进度跳转");return false;}
        unsigned index=(unsigned)((target>100?100:target)*(app.epub.spine_count-1)/100);
        if(load_chapter(index)){app.menu=app.jump=false;app.dirty=true;save_state();return true;}
        return false;
    }
    if(app.jump_pending)return true;
    if(!app.opened || !pagination_prepare() || app.pagination_failed) {
        say("全书分页不可用，保留原位置；可返回继续阅读");return false;
    }
    app.jump_by_percent=percent;app.jump_target=percent && target>100?100:target;
    app.jump_book=app.runtime.location.book_id;app.jump_layout=app.page_cache_key;
    app.jump_started_at=app.jump_progress_at=now();
    app.menu=app.jump=true;
    ESP_LOGI("ThingsOS_reader","Jump request book=%016llx kind=%s value=%u indexed=%d pages=%u",
        (unsigned long long)app.jump_book,percent?"percent":"page",(unsigned)target,
        app.pagination_valid,(unsigned)app.page_count);
    memory_log("jump-request");
    if(app.pagination_valid)return reader_jump_finish();
    /* Rebuildable frames have lower priority than the index and next chapter. */
    reader_cache_drop_frames();
    app.jump_pending=true;app.dirty=true;
    return true;
}
static void reader_jump_poll(void) {
    if(!app.jump_pending || app.dirty)return;
    if(!app.opened || app.asleep || app.page!=THINGSOS_UI_READING ||
       app.runtime.location.book_id!=app.jump_book || !pagination_prepare() ||
       app.page_cache_key!=app.jump_layout) {
        reader_jump_cancel();return;
    }
    /* Bound the overall operation as well as each layout slice. A timeout
       leaves the readable book/position and reusable partial index intact. */
    if(now()-app.jump_started_at>=300000) {
        reader_jump_cancel();say("分页耗时过长，已取消跳转；可继续阅读");return;
    }
    if(!pagination_step_impl(true)) {
        reader_jump_cancel();
        say(app.pagination_error && !strcmp(app.pagination_error,"memory")
            ?"分页内存不足，已取消跳转；可继续阅读"
            :"分页失败，已取消跳转；可继续阅读");
        return;
    }
    if(app.pagination_valid){reader_jump_finish();return;}
    if(now()-app.jump_progress_at>=2000) {
        app.jump_progress_at=now();app.dirty=true;
    }
}
