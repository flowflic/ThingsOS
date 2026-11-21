/* SPDX-License-Identifier: Apache-2.0
 * Independently implemented adjacent text-page cache. The UI owns all state;
 * a worker borrows the font/chapter only across a synchronous display wait.
 * Two reusable 8-bit canvases retain exact glyph coverage; no internal-RAM fallback.
 */
#define READER_CACHE_BYTES (684u * 1216u)
#include <ft2build.h>
#include FT_CONFIG_OPTIONS_H
/* The patched gray workspace is per-raster PSRAM. Keep the existing 32 KiB
   PSRAM worker stack budget for interpreter/layout headroom; the UI internal
   stack is independently reduced after target frame verification. */
#define READER_PREP_STACK_BYTES (FT_RENDER_POOL_SIZE + 16u * 1024u)
typedef struct {
    unsigned char* pixels;
    thingsos_text_hits_t* hits;
    uint64_t book, layout, font;
    unsigned chapter, px, line, margin, weight;
    size_t start, end;
    uint64_t retry_after;
    bool attempted, valid;
    thingsos_text_cursor_t cursor;
} reader_frame_t;
static reader_frame_t reader_frames[2];
static uint64_t reader_cache_after;
static unsigned reader_cache_hits, reader_cache_misses;
static bool reader_cache_restored_hits;
#ifndef THINGSOS_HOST_UI
static TaskHandle_t reader_prep_task;
static SemaphoreHandle_t reader_prep_done;
static atomic_bool reader_prep_stop;
static bool reader_prep_active;
static thingsos_page_position_t reader_prep_position;
static bool reader_cache_continue(void* unused) {
    (void)unused;
    return !atomic_load_explicit(&reader_prep_stop,memory_order_acquire);
}
#endif
#ifdef THINGSOS_HOST_UI
static bool reader_cache_fail_allocate;
static HANDLE reader_cache_test_thread,reader_cache_test_started,reader_cache_test_progress;
static volatile LONG reader_cache_test_stop,reader_cache_test_rows;
static bool reader_cache_test_slow;
static bool reader_cache_continue(void* unused) {
    (void)unused;
    SetEvent(reader_cache_test_started);
    /* Test-only handshake: on the second row boundary, one full row has
       advanced. Wait for the real cancellation instead of a 10ms wall delay. */
    if(reader_cache_test_slow&&InterlockedIncrement(&reader_cache_test_rows)>=2){
        SetEvent(reader_cache_test_progress);
        while(!InterlockedCompareExchange(&reader_cache_test_stop,0,0))Sleep(1);
    }
    return !InterlockedCompareExchange(&reader_cache_test_stop,0,0);
}
#endif
/* Coverage changes invalidate pixels, but need not repeatedly free and allocate
   the PSRAM canvases, hit regions and worker stack. UI owns this handoff. */
static void reader_cache_invalidate(void) {
    for(unsigned i=0;i<2;++i) {
        reader_frames[i].valid=reader_frames[i].attempted=false;
        reader_frames[i].retry_after=0;
        memset(&reader_frames[i].cursor,0,sizeof(reader_frames[i].cursor));
    }
    reader_cache_after=0;
}
/* Release only the rebuildable page frames; the worker keeps running. */
static void reader_cache_drop_frames(void) {
    for (unsigned i=0;i<2;++i) {
        free(reader_frames[i].pixels);
        free(reader_frames[i].hits);reader_frames[i].hits=NULL;
        reader_frames[i].pixels=NULL;
        reader_frames[i].valid=false;
        reader_frames[i].attempted=false;reader_frames[i].retry_after=0;
    }
}
static void reader_cache_clear(void) {
#ifndef THINGSOS_HOST_UI
    /* Only the UI calls clear, after the output-overlap handoff. */
    if(reader_prep_task){vTaskDeleteWithCaps(reader_prep_task);reader_prep_task=NULL;}
    if(reader_prep_done){vSemaphoreDelete(reader_prep_done);reader_prep_done=NULL;}
#endif
    for (unsigned i=0;i<2;++i) {
        free(reader_frames[i].pixels);
        free(reader_frames[i].hits);
        memset(&reader_frames[i],0,sizeof(reader_frames[i]));
    }
    reader_cache_after=0;
}
static bool reader_cache_key(const reader_frame_t* f, unsigned chapter, size_t start) {
    return f->attempted && (!f->retry_after || now()<f->retry_after) && f->book==app.runtime.location.book_id &&
        f->layout==app.page_cache_key && f->font==app.font.signature &&
        f->chapter==chapter && f->start==start && f->px==app.px &&
        f->line==app.line_choice && f->margin==app.margin_choice && f->weight==app.weight_choice;
}
static bool reader_cache_build(unsigned slot,thingsos_page_position_t position);
static bool reader_cache_restore(void) {
    reader_cache_restored_hits=false;
    for(unsigned i=0;i<2;++i) {
        reader_frame_t* f=&reader_frames[i];
        if(f->cursor.active && reader_cache_key(f,app.chapter_index,app.start))
            reader_cache_build(i,(thingsos_page_position_t){app.chapter_index,(uint32_t)app.start});
        if(f->valid && reader_cache_key(f,app.chapter_index,app.start)) {
            /* Status, progress and overlays are always drawn from live state. */
            memcpy(app.canvas.pixels+(size_t)reader_body_top()*reader_width(),f->pixels+(size_t)reader_body_top()*reader_width(),
                (size_t)reader_body_height()*reader_width());
            app.end=f->end;
            if(f->hits) {
                app.text_hits.count=f->hits->count;app.text_hits.line_count=f->hits->line_count;
                app.text_hits.glyph_count=f->hits->glyph_count;
                memcpy(app.text_hits.items,f->hits->items,f->hits->count*sizeof(*f->hits->items));
                memcpy(app.text_hits.lines,f->hits->lines,f->hits->line_count*sizeof(*f->hits->lines));
                memcpy(app.text_hits.glyphs,f->hits->glyphs,f->hits->glyph_count*sizeof(*f->hits->glyphs));
                reader_cache_restored_hits=true;
            }
            ++reader_cache_hits;
            ESP_LOGI("ThingsOS_cache","Hit chapter=%u offset=%u hits=%u",
                app.chapter_index,(unsigned)app.start,reader_cache_hits);
            return true;
        }
    }
    ++reader_cache_misses;
    return false;
}
static bool reader_cache_picture(void* context,const thingsos_inline_t* image,
    thingsos_canvas_t* canvas,int x,int y,int w,int h) {
    (void)image;(void)canvas;(void)x;(void)y;(void)w;(void)h;
    *(bool*)context=true; /* Image pages retain the normal decoder/hit regions. */
    return false;
}
static bool reader_cache_build(unsigned slot,thingsos_page_position_t position) {
    reader_frame_t* f=&reader_frames[slot];
    bool matching=reader_cache_key(f,position.chapter,position.offset);
    bool resume=matching && f->cursor.active;
    if(matching && !resume) return false;
    if(!resume)memset(&f->cursor,0,sizeof(f->cursor));
    f->valid=false;f->attempted=true;f->retry_after=0;
    f->book=app.runtime.location.book_id;f->layout=app.page_cache_key;
    f->font=app.font.signature;f->chapter=position.chapter;f->start=position.offset;
    f->px=app.px;f->line=app.line_choice;f->margin=app.margin_choice;
    f->weight=app.weight_choice;
    if(!f->pixels) {
#ifdef THINGSOS_HOST_UI
        if(!reader_cache_fail_allocate) f->pixels=malloc(READER_CACHE_BYTES);
#else
        /* One immediately useful text frame has priority over a second frame.
           The image decoder already drops both rebuildable frames before use. */
        size_t reserve=slot?2u*1024u*1024u:1024u*1024u;
        if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)>READER_CACHE_BYTES+sizeof(*f->hits)+reserve)
            f->pixels=heap_caps_malloc(READER_CACHE_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#endif
        if(!f->pixels) {
            f->retry_after=now()+1000;
            ESP_LOGI("ThingsOS_cache","Frame deferred slot=%u: insufficient optional memory",slot);
            return true;
        }
    }
    thingsos_chapter_t* temporary=NULL;
    const thingsos_chapter_t* chapter=&app.chapter;
    if(position.chapter!=app.chapter_index) {
        if(!app.epub_open || !media()) return true;
        temporary=calloc(1,sizeof(*temporary));
        if(!temporary) return true;
        if(!reader_chapter_load(position.chapter,temporary)) {
            thingsos_chapter_close(temporary);free(temporary);return true;
        }
        chapter=temporary;
    }
    uint64_t started=now();
    thingsos_canvas_t canvas={.pixels=f->pixels,.width=reader_width(),.height=reader_height()};
    if(!resume)memset(f->pixels,255,READER_CACHE_BYTES);
    bool image=false;
    bool annotations=true; /* Text selection also needs the original glyph geometry. */
    if(annotations && !f->hits && !resume) {
#ifdef THINGSOS_HOST_UI
        f->hits=malloc(sizeof(*f->hits));
#else
        f->hits=heap_caps_malloc(sizeof(*f->hits),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#endif
    }
    if(!annotations) {free(f->hits);f->hits=NULL;}
    if(f->hits && !resume)memset(f->hits,0,sizeof(*f->hits));
    unsigned font_errors=app.font.errors;
    thingsos_font_size(&app.font,app.px);
    thingsos_font_weight(&app.font,app.weight_choice);
    bool complete=true;
    f->end=thingsos_text_page_resume_flow(&app.font,&canvas,chapter,f->start,
        reader_content_left(chapter),reader_body_top(),reader_content_width(chapter),reader_body_height(),reader_lines[app.line_choice],
        thingsos_reader_indent_px(app.px,app.indent_chars),
        reader_epub_paragraphs(),reader_cache_picture,&image,f->hits,
#ifdef THINGSOS_HOST_UI
        reader_cache_test_thread?reader_cache_continue:NULL,NULL,&complete,&f->cursor,app.vertical_enabled);
#else
        reader_prep_active?reader_cache_continue:NULL,NULL,&complete,&f->cursor,app.vertical_enabled);
#endif
    bool font_ok=app.font.errors==font_errors;
    f->valid=complete && font_ok && !image && f->end>f->start;
    if(!font_ok) {f->attempted=false;memset(&f->cursor,0,sizeof(f->cursor));}
    if(!complete && !f->cursor.active)f->attempted=false;
    if(image)memset(&f->cursor,0,sizeof(f->cursor));
    if(temporary) {thingsos_chapter_close(temporary);free(temporary);}
    thingsos_font_size(&app.font,app.px);
    thingsos_font_weight(&app.font,0);
    ESP_LOGI("ThingsOS_cache","Build slot=%u chapter=%u offset=%u valid=%d time=%u ms resume=%d paused=%d cursor=%u",
        slot,f->chapter,(unsigned)f->start,f->valid,(unsigned)(now()-started),resume,f->cursor.active,(unsigned)f->cursor.offset);
    return true;
}
/* The main task lends its font only while it synchronously waits for output.
   No worker may open another chapter, touch the UI canvas, or present pixels. */
#ifndef THINGSOS_HOST_UI
static void reader_cache_worker(void* unused) {
    (void)unused;
    for(;;) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        reader_cache_build(0,reader_prep_position);
        ESP_LOGI("ThingsOS_cache","Prefetch stack budget=%u remaining=%u bytes",
            (unsigned)READER_PREP_STACK_BYTES,(unsigned)uxTaskGetStackHighWaterMark(NULL));
        xSemaphoreGive(reader_prep_done);
    }
}
#else
static thingsos_page_position_t reader_cache_test_position;
static DWORD WINAPI reader_cache_worker(LPVOID unused) {
    (void)unused;
    reader_cache_build(0,reader_cache_test_position);
    return 0;
}
#endif
static bool reader_cache_next(thingsos_page_position_t* next) {
    if(app.pagination_valid) {
        size_t page=current_page();
        if(page+1>=app.page_count)return false;
        *next=app.pages[page+1];
    }else {
        if(app.end<=app.start || app.end>=app.chapter.bytes)return false;
        *next=(thingsos_page_position_t){app.chapter_index,(uint32_t)app.end};
    }
    return next->chapter==app.chapter_index;
}
static bool reader_cache_overlap_begin(void) {
    if(alarm_active())return false;
    /* Cached WeRead pages repeatedly cancel this work at the scan barrier.
       Keep their idle prefetch, without competing with framebuffer packing. */
    if(app.wd_id[0])return false;
    if(!app.opened || app.page!=THINGSOS_UI_READING || app.controls_open || app.menu || app.jump || app.annotation_open || app.annotation_selecting ||
       app.image_view.open || app.image_hit_count || app.asleep || app.wd_notes_open || app.wd_consent || app.wd_cloud_prompt ||
       thingsos_network_busy())return false;
    thingsos_page_position_t next;
    if(!reader_cache_next(&next) || app.chapter.image_count ||
       (reader_cache_key(&reader_frames[0],next.chapter,next.offset) && !reader_frames[0].cursor.active))return false;
#ifdef THINGSOS_HOST_UI
    reader_cache_test_position=next;
    InterlockedExchange(&reader_cache_test_stop,0);
    InterlockedExchange(&reader_cache_test_rows,0);
    reader_cache_test_started=CreateEvent(NULL,TRUE,FALSE,NULL);
    if(!reader_cache_test_started)return false;
    reader_cache_test_progress=CreateEvent(NULL,TRUE,FALSE,NULL);
    if(!reader_cache_test_progress){CloseHandle(reader_cache_test_started);reader_cache_test_started=NULL;return false;}
    reader_cache_test_thread=CreateThread(NULL,0,reader_cache_worker,NULL,CREATE_SUSPENDED,NULL);
    if(!reader_cache_test_thread){CloseHandle(reader_cache_test_started);reader_cache_test_started=NULL;CloseHandle(reader_cache_test_progress);reader_cache_test_progress=NULL;return false;}
    ResumeThread(reader_cache_test_thread);
    return true;
#else
    if(!reader_prep_done)reader_prep_done=xSemaphoreCreateBinary();
    if(!reader_prep_done)return false;
    /* Never take this large stack from the internal SD/LCD DMA heap. A failed
       optional allocation keeps ordinary rendering, not an undersized task. */
    if(!reader_prep_task && xTaskCreatePinnedToCoreWithCaps(reader_cache_worker,"reader_prep",
        READER_PREP_STACK_BYTES,NULL,2,&reader_prep_task,1,
        MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS) {
        ESP_LOGW("ThingsOS_cache","Prefetch stack allocation failed; using ordinary rendering");
        return false;
    }
    xSemaphoreTake(reader_prep_done,0);
    reader_prep_position=next;
    atomic_store_explicit(&reader_prep_stop,false,memory_order_release);
    reader_prep_active=true;
    xTaskNotifyGive(reader_prep_task);
    return true;
#endif
}
static void reader_cache_overlap_end(bool started) {
#ifndef THINGSOS_HOST_UI
    if(started) {
        uint64_t joined=now();
        atomic_store_explicit(&reader_prep_stop,true,memory_order_release);
        xSemaphoreTake(reader_prep_done,portMAX_DELAY);
        reader_prep_active=false;
        ESP_LOGI("ThingsOS_cache","Scan-barrier handoff=%u ms valid=%d",
            (unsigned)(now()-joined),reader_frames[0].valid);
    }
#else
    if(started) {
        InterlockedExchange(&reader_cache_test_stop,1);
        WaitForSingleObject(reader_cache_test_thread,INFINITE);
        CloseHandle(reader_cache_test_thread);reader_cache_test_thread=NULL;
        CloseHandle(reader_cache_test_started);reader_cache_test_started=NULL;
        CloseHandle(reader_cache_test_progress);reader_cache_test_progress=NULL;
    }
#endif
}
static bool reader_scan_preparing;
static void reader_cache_before_scan(void) {
    if(reader_scan_preparing) {
        reader_cache_overlap_end(true);reader_scan_preparing=false;
    }
}
static void reader_cache_output_begin(void) {
    reader_scan_preparing=reader_cache_overlap_begin();
    thingsos_board_scan_barrier(reader_scan_preparing?reader_cache_before_scan:NULL);
}
static void reader_cache_output_end(void) {
    thingsos_board_scan_barrier(NULL);
    reader_cache_before_scan();
}
static void reader_cache_idle(void) {
    if(alarm_active())return;
    if(!app.opened || app.page!=THINGSOS_UI_READING || app.controls_open || app.menu || app.jump || app.annotation_open || app.annotation_selecting ||
       app.image_view.open || app.asleep || app.dirty || app.wd_notes_open || app.wd_consent || app.wd_cloud_prompt || thingsos_network_busy() ||
       now()<reader_cache_after) return;
    thingsos_page_position_t next;
    if(reader_cache_next(&next) && reader_cache_build(0,next))return;
    if(!app.pagination_valid) {
        if(app.history_count)reader_cache_build(1,(thingsos_page_position_t){app.chapter_index,(uint32_t)app.history[app.history_count-1]});
        return;
    }
    size_t page=current_page();
    /* At most one page per loop; input is polled again before the other slot. */
    if(page && reader_cache_build(1,app.pages[page-1])) return;
}
