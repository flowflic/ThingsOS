/* SPDX-License-Identifier: Apache-2.0
 * Main-task-only scheduler: bounded chapter windows per cancellable worker job. Foreground
 * targets supersede speculative prefetch; all completion checks bind the book.
 */
#ifndef ONLINE_ROOT
#define ONLINE_ROOT "/sdcard/.thingsos/weidu"
#endif
#define online (app.online_state)
static void online_modal_begin(bool);
static void online_modal_progress(void);
static void online_close(void) {
    if(online.running)thingsos_weidu_cancel();
    online.waiting=online.opening=false;
    online.check_current_images=online.repairing_current_images=online.refresh_current_images=false;
    if(online.painted || online.error)app.dirty=true;
    online.cleanup|=online.painted;
    online.error=online.painted=false;online.animate_at=0;
    free(online.note);online.note=NULL;online.cloud=false;
}
static bool online_pending(void) {return online.opening || online.waiting || online.error;}
static bool online_modal_visible(void) {
    return online_pending() && app.page==online.page && !app.asleep &&
        (online.initial || (app.opened && app.epub.weidu_online && app.runtime.location.book_id==online.book_id));
}
static bool online_covers(unsigned index,const char* id) {
    return online.running && !strcmp(online.active_book,id) && index>=online.active_index &&
        index-online.active_index<online.active_count;
}
static bool online_window_ready(const char* id,unsigned first,unsigned amount,bool images) {
    unsigned count=thingsos_weidu_stream_count(ONLINE_ROOT,id);
    if(!count || first>=count)return false;
    for(unsigned i=0;i<amount && first+i<count;++i)
        if(!(images?thingsos_weidu_stream_images_ready(ONLINE_ROOT,id,first+i,true):
                    thingsos_weidu_stream_ready(ONLINE_ROOT,id,first+i)))return false;
    return true;
}
static unsigned online_saved(const thingsos_weidu_book_t* book) {
    char identity_path[640];snprintf(identity_path,sizeof(identity_path),"/sdcard/books/微读-%s.epub",book->id);
    uint64_t id=identity(identity_path);unsigned chapter=0,offset=0;char file[128];
    snprintf(file,sizeof(file),"/sdcard/.thingsos/%016llx.pos",(unsigned long long)id);
    FILE* f=fopen(file,"rb");if(f){if(fscanf(f,"THINGSOS1 %u %u",&chapter,&offset)!=2)chapter=0;fclose(f);}
    return chapter;
}
static bool online_open(const thingsos_weidu_book_t* book) {
    char path[640];unsigned chapter=online_saved(book);
    unsigned count=thingsos_weidu_stream_count(ONLINE_ROOT,book->id);if(count && chapter>=count)chapter=0;
    if(!thingsos_weidu_stream_path(ONLINE_ROOT,book->id,path,sizeof(path)))return false;
    bool connected=thingsos_network_connected();
    if(online_window_ready(book->id,chapter,connected?3:1,connected) &&
       (!connected || (thingsos_weidu_stream_images_ready(ONLINE_ROOT,book->id,chapter,false) &&
                      !thingsos_weidu_stream_needs_cover(ONLINE_ROOT,book->id))) &&
       weidu_open_path(book,path))return true;
    if(!thingsos_network_connected()){say("本章尚未加载，请先连接 WiFi；已加载章节仍可离线阅读");return false;}
    if(online.running && !online_covers(chapter,book->id))thingsos_weidu_cancel();
    online.book=*book;online.target=chapter;online.opening=true;online.waiting=false;
    online.upgrading_images=online_window_ready(book->id,chapter,3,false);
    online.page=app.page;online.retry_at=0;online_modal_begin(true);return true;
}
static void online_need(unsigned index) {
    if(!app.epub.weidu_online || index>=app.epub.spine_count)return;
    if(!thingsos_weidu_stream_book(app.read_path,&online.book))return;
    if(!thingsos_network_connected()){say("本章尚未加载，请连接 WiFi 后重试；当前位置已保留");return;}
    free(online.note);online.note=NULL;online.cloud=false;
    if(online.running && !online_covers(index,online.book.id))thingsos_weidu_cancel();
    online.target=index;online.waiting=true;online.opening=false;
    online.book_id=app.runtime.location.book_id;online.page=app.page;online.retry_at=0;
    online_modal_begin(false);
}
static void online_loaded(unsigned index) {
    online.retry_at=0;
    online.check_current_images=true;
    online.refresh_current_images=false;
    if(index!=online.active_index)online.repairing_current_images=false;
    if((online.waiting || online.error) && index!=online.target) {
        online.cleanup|=online.painted;
        online.waiting=online.error=false;online.painted=false;app.dirty=true;
        free(online.note);online.note=NULL;online.cloud=false;
    }
}
static bool online_cloud_target(uint32_t offset) {
    if(!online.waiting)return false;
    online.cloud=true;online.cloud_offset=offset;return true;
}
static bool online_note_target(const thingsos_wd_note_t* note) {
    if(!online.waiting)return false;
    free(online.note);online.note=thingsos_psram_alloc(sizeof(*note));
    if(!online.note){online.waiting=false;return false;}
    *online.note=*note;return true;
}
static void online_apply_position(bool cloud,uint32_t cloud_offset,const thingsos_wd_note_t* note) {
    if(!cloud && !note)return;
    wd_prepare();size_t a,b;
    if(note && thingsos_wd_map_range(&app.wd_map,&app.chapter,note,&a,&b)) {
        app.start=a;
        size_t slot=app.wd_line_count<WD_DRAW_LIMIT?app.wd_line_count++:WD_DRAW_LIMIT-1;
        app.wd_lines[slot]=(thingsos_text_underline_t){a,b,app.wd_kind==1};
        app.chapter.underlines=app.wd_lines;app.chapter.underline_count=app.wd_line_count;
    } else if(cloud && thingsos_wd_local(&app.wd_map,cloud_offset,&a))app.start=a;
    else say("无法精确定位，已到对应章首");
    if(cloud)wd_accept_cloud_position();
    app.wd_notes_open=app.wd_long_open=false;
    thingsos_runtime_overlay(&app.runtime,false,now());
}
static void online_poll(void) {
    if(online_pending() && (app.page!=online.page || (!online.initial &&
       (!app.opened || !app.epub.weidu_online || app.runtime.location.book_id!=online.book_id))))online_close();
    if(online.running) {
        thingsos_weidu_status_t status;thingsos_weidu_state(&status);
        if(status.serial!=online.progress_serial){online.progress_serial=status.serial;online_modal_progress();}
    }
    if(online.running && !thingsos_weidu_busy()) {
        thingsos_weidu_status_t status;thingsos_weidu_state(&status);online.running=false;
        if(status.phase==WD_READY && app.opened && app.epub.weidu_online &&
           !strcmp(app.wd_id,online.active_book)) {
            app.toc_pending=true;
            app.toc_invalidate_pending=true;
        }
        if(online.repairing_current_images) {
            online.repairing_current_images=false;
            online.refresh_current_images=status.phase==WD_READY;
        }
        if(online.opening && status.phase==WD_READY) {
            unsigned count=thingsos_weidu_stream_count(ONLINE_ROOT,online.book.id);
            if(count && online.target>=count)online.target=0;
        }
        if(online.opening && !strcmp(online.active_book,online.book.id) &&
           online.target>=online.active_index && online.target-online.active_index<online.active_count)
            online.upgrading_images=false;
        if(status.phase!=WD_READY && !strcmp(online.active_book,online.book.id) &&
           online.target>=online.active_index && online.target-online.active_index<online.active_count) {
            if(online.opening || online.waiting) {
                if(status.phase==WD_CANCELLED)online_close();
                else if(!online.opening || !online_window_ready(online.book.id,online.target,3,false)) {
                    online.error=true;online.opening=online.waiting=false;
                    strlcpy(online.error_text,status.message[0]?status.message:"请检查网络后重试",sizeof(online.error_text));
                    app.dirty=true;
                }
            }
            online.retry_at=status.phase==WD_CANCELLED?0:now()+30000;
        }
    }
    if(online.refresh_current_images && !online_pending() && app.page==THINGSOS_UI_READING &&
       !app.asleep && !reader_ui_active()) {
        online.refresh_current_images=false;
        /* Inserting image anchors shifts byte offsets. Reload a visible chapter
           head in place and use the upgraded cache on later entries elsewhere. */
        if(!app.start && load_chapter_content(app.chapter_index,true)) {
            ESP_LOGI("ThingsOS_reader","Online images reloaded chapter=%u count=%u",
                app.chapter_index,(unsigned)app.chapter.image_count);
            reader_cache_clear();app.dirty=true;
        }
        online.check_current_images=false;
    }
    if(online.error)return;
    if(online.opening && online.upgrading_images && !online.running && !thingsos_network_connected())
        online.upgrading_images=false;
    if((online.opening || online.waiting) && !online.running && !thingsos_weidu_busy()) {
        char complete[512];
        if(thingsos_weidu_device_cached(online.book.id,complete,sizeof(complete))) {
            thingsos_weidu_book_t book=online.book;bool opening=online.opening;unsigned index=online.target;
            bool cloud=online.cloud;uint32_t offset=online.cloud_offset;thingsos_wd_note_t* note=online.note;online.note=NULL;
            online.opening=online.waiting=false;
            if(!weidu_open_path(&book,complete) || (!opening && !load_chapter(index)))say("图书打开失败，缓存保留");
            else if(!opening){online_apply_position(cloud,offset,note);app.runtime.location.byte_offset=(uint32_t)app.start;save_state();}
            free(note);online.cloud=false;
            return;
        }
    }
    if((online.waiting || (online.opening && !online.upgrading_images && !online.running && !thingsos_weidu_busy() &&
       online_window_ready(online.book.id,online.target,3,false))) &&
       thingsos_weidu_stream_ready(ONLINE_ROOT,online.book.id,online.target)) {
        thingsos_weidu_book_t book=online.book;unsigned index=online.target;bool opening=online.opening;
        bool cloud=online.cloud;uint32_t offset=online.cloud_offset;thingsos_wd_note_t* note=online.note;online.note=NULL;
        online.opening=online.waiting=false;app.message[0]=0;
        if(opening) {char path[640];thingsos_weidu_stream_path(ONLINE_ROOT,book.id,path,sizeof(path));if(!weidu_open_path(&book,path))say("章节打开失败，已加载内容保留");}
        else if(load_chapter(index)) {online_apply_position(cloud,offset,note);app.menu=app.jump=false;app.runtime.location.chapter=index;app.runtime.location.byte_offset=(uint32_t)app.start;navigate(THINGSOS_UI_READING);save_state();}
        free(note);online.cloud=false;
        return;
    }
    if(!online.waiting && !online.opening){free(online.note);online.note=NULL;online.cloud=false;}
    if(online.running || thingsos_weidu_busy() || !thingsos_network_connected() || now()<online.retry_at)return;
    unsigned amount=3;
    if(!online.opening && !online.waiting) {
        if(!app.opened || !app.epub.weidu_online || app.page!=THINGSOS_UI_READING || app.asleep)return;
        if(!thingsos_weidu_background_ready())return;
        if(!thingsos_weidu_stream_book(app.read_path,&online.book)){online.retry_at=now()+30000;return;}
        char complete[512];if(thingsos_weidu_device_cached(online.book.id,complete,sizeof(complete))){online.retry_at=UINT64_MAX;return;}
        bool repair=online.check_current_images &&
            !thingsos_weidu_stream_images_ready(ONLINE_ROOT,online.book.id,app.chapter_index,false);
        online.check_current_images=false;
        if(repair) {
            online.target=app.chapter_index;amount=1;
        } else {
            unsigned next=app.chapter_index+1;
            /* A ready window needs no SD polling on every UI iteration. Loading
               another chapter or an explicit target re-arms the scheduler. */
            if(next>=app.epub.spine_count || online_window_ready(online.book.id,next,2,true)){online.retry_at=UINT64_MAX;return;}
            online.target=next;amount=2;
        }
    }
#ifndef THINGSOS_HOST_UI
    if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<1536u*1024u ||
       heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)<512u*1024u)reader_cache_drop_frames();
#endif
    if(thingsos_weidu_device_chapters(&online.book,online.target,amount,online.opening)) {
        online.running=true;online.active_index=online.target;online.active_count=amount;
        online.repairing_current_images=amount==1;
        strlcpy(online.active_book,online.book.id,sizeof(online.active_book));
        if(online.repairing_current_images)
            ESP_LOGI("ThingsOS_weidu","Online image repair book=%s chapter=%u",online.active_book,online.active_index);
    } else if(online.opening && online.upgrading_images) {
        /* The old chapter window is still usable if the optional image pass
           could not start. The next poll can open it directly. */
        online.upgrading_images=false;
    } else if(online.opening || online.waiting) {
        online.error=true;online.opening=online.waiting=false;
        strlcpy(online.error_text,"章节加载暂不可用，请稍后重试",sizeof(online.error_text));app.dirty=true;
    } else if(amount==1) {
        online.check_current_images=true;online.retry_at=now()+30000;
    }
}
#include "weidu_loading_card.inc"
