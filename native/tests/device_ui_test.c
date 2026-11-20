// SPDX-License-Identifier: Apache-2.0
#define THINGSOS_HOST_UI
#define ANNOTATION_ROOT "annotation-ui-test"
#include "../firmware/main/main.c"
#include "font_fault.h"
#include "thingsos/touch_queue.h"
#define CHECK(x)                                                    \
    do {                                                            \
        if (!(x)) {                                                 \
            fprintf(stderr, "UI test line %d: %s\n", __LINE__, #x); \
            exit(1);                                                \
        }                                                           \
    } while (0)
static const char* fixture_root;
static void canvas_snapshot(const char* name);
static void online_comic_layout_test(void) {
    thingsos_chapter_t* chapter=calloc(1,sizeof(*chapter));CHECK(chapter);
    char text[]="Chapter\n\n";
    chapter->text=text;chapter->bytes=strlen(text);
    chapter->heading=true;chapter->heading_end=7;
    chapter->image_count=1;chapter->images[0].offset=8;
    chapter->images[0].width=1200;chapter->images[0].height=1680;
    app.epub.weidu_online=true;
    CHECK(reader_content_left(chapter)==0 && reader_content_width(chapter)==reader_width());
    text[8]='A'; /* Mixed chapters keep the text column. */
    CHECK(reader_content_left(chapter)==reader_body_left() && reader_content_width(chapter)==reader_body_width());
    text[8]='\n';app.epub.weidu_online=false;
    CHECK(reader_content_width(chapter)==reader_body_width());
    free(chapter);

    unsigned char pixels[40*32];memset(pixels,255,sizeof(pixels));
    for(unsigned y=6;y<24;++y)memset(pixels+y*40+9,32,24);
    unsigned w=40,h=32;
    reader_comic_trim(pixels,&w,&h);
    CHECK(w==32 && h==26 && pixels[4*w+3]==255 && pixels[4*w+4]==32);
    CHECK(pixels[21*w+27]==32 && pixels[22*w+27]==255);
}
static void online_arrive(unsigned index) {
    char src[800],dst[800];
    snprintf(src,sizeof(src),"%s/online-window-source/book-1234567890/online-%u-%u-img.xhtml",fixture_root,index,index+1);
    snprintf(dst,sizeof(dst),"%s/book-1234567890/online-%u-%u-img.xhtml",host_online_root,index,index+1);
    FILE* in=fopen(src,"rb"),*out=fopen(dst,"wb");CHECK(in && out);
    int ch;while((ch=fgetc(in))!=EOF)CHECK(fputc(ch,out)!=EOF);
    CHECK(!fclose(in) && !fclose(out));
}
static void online_reader_test(void) {
    app.wd_active=false;release_book();host_sd=true;host_network_connected=true;
    online_comic_layout_test();
    host_weidu.busy=false;host_weidu_epub[0]=0;
    memset(&online,0,sizeof(online));
    snprintf(host_online_root,sizeof(host_online_root),"%s/online-source",fixture_root);
    char path[700];CHECK(thingsos_weidu_stream_path(host_online_root,"1234567890",path,sizeof(path)));
    thingsos_weidu_book_t book;CHECK(thingsos_weidu_stream_book(path,&book));
    CHECK(weidu_open_path(&book,path) && app.epub.weidu_online && app.chapter_index==0);
    app.wd_active=false;host_weidu.busy=false;
    CHECK(pagination_prepare() && !app.pagination_valid && !pagination_step() && !app.pages);
    host_clock_us+=2000000;app.activity_at=now();online_poll();
    CHECK(online.running && host_weidu_action==WD_DEVICE_CHAPTER && host_weidu_chapter==1 && host_weidu_chapters==2);
    online_need(5);online_poll();
    CHECK(online.waiting && online.running && host_weidu_chapter==5);
    CHECK(online_modal_visible() && !app.message[0]);
    app.dirty=false;host_clock_us+=4000000;message_expire_poll();CHECK(!app.dirty && online_modal_visible());
    bool immersive=app.immersive_enabled;app.immersive_enabled=true;
    CHECK(reader_body_top()==12 && reader_body_height()==reader_height()-24);
    app.immersive_enabled=immersive;
    CHECK(online_cloud_target(3) && online.cloud);
    thingsos_wd_note_t note={0};strcpy(note.uid,"6");strcpy(note.text,"Hello");
    CHECK(online_note_target(&note) && online.note!=&note && !strcmp(online.note->text,"Hello"));
    online_need(4);online_poll();CHECK(online.waiting && host_weidu_chapter==4);
    CHECK(!online.note && !online.cloud);
    CHECK(load_chapter(3) && !online.waiting); /* A cached manual turn wins. */
    CHECK(!online_modal_visible() && !app.message[0]);
    app.start=2;host_network_connected=false;host_weidu.busy=false;host_weidu.phase=WD_READY;
    online_poll();CHECK(app.chapter_index==3 && app.start==2);
    unsigned before=host_weidu_calls;online_need(5);online_poll();
    CHECK(app.chapter_index==3 && app.start==2 && host_weidu_calls==before);
    app.wd_active=false;release_book();host_network_connected=true;host_weidu.busy=false;
    memset(&online,0,sizeof(online));strcpy(book.id,"999");app.page=THINGSOS_UI_WEIDU;
    host_weidu_chapter_start=WD_START_BUSY;
    CHECK(online_open(&book));online_poll();
    CHECK(online.opening && !online.error && !online.running && online.retry_at==now()+250);
    host_weidu_chapter_start=WD_START_OK;
    online_poll();CHECK(!online.running);
    host_clock_us+=250000;
    online_poll();CHECK(online.running && host_weidu_chapter==0);
    online_close();online_poll();
    host_weidu_chapter_start=WD_START_BUSY;
    CHECK(online_open(&book));online_poll();
    host_clock_us+=3000000;
    online_poll();CHECK(online.error && !online.running && strstr(online.error_text,"后台任务"));
    online_close();
    host_weidu_chapter_start=WD_START_MEMORY;
    CHECK(online_open(&book));online_poll();
    CHECK(online.error && !strcmp(online.error_text,thingsos_weidu_error_text(WD_MEMORY)));
    online_close();
    host_weidu_chapter_start=WD_START_OK;
    CHECK(online_open(&book));host_network_connected=false;online_poll();
    CHECK(online.error && strstr(online.error_text,"WiFi"));
    online_close();host_network_connected=true;
    CHECK(online_open(&book));online_poll();CHECK(online.running);
    app.page=THINGSOS_UI_LIBRARY;host_weidu.busy=false;host_weidu.phase=WD_READY;
    online_poll();CHECK(!online.opening && app.page==THINGSOS_UI_LIBRARY);
    memset(&online,0,sizeof(online));host_weidu.busy=false;
    strcpy(book.id,"1234567890");app.page=THINGSOS_UI_WEIDU;
    CHECK(online_open(&book) && online.opening && !app.opened);
    CHECK(online_modal_visible() && online.progress==2 && online.total==3);
    draw();canvas_snapshot("online-loading-card-initial");
    unsigned full_refreshes=host_full_refreshes;
    CHECK(online_modal_present_frame(false)==ESP_OK && host_full_refreshes==full_refreshes && host_loading_x==0 && host_loading_y==0 &&
        host_loading_w==684 && host_loading_h==1216);
    online_modal_presented(true);
    CHECK(online.animate_at==now()+3000);
    CHECK(online_modal_present_status()==ESP_OK && host_loading_x==0 && host_loading_y==0 &&
        host_loading_w==684 && host_loading_h==74);
    online_card_rect_t card=online_card_rect(),ring=online_spinner_rect();
    CHECK(card.x==76 && card.y==393 && card.w==532 && card.h==430);
    CHECK(online_modal_present_frame(true)==ESP_OK && host_loading_x==66 && host_loading_y==383 &&
        host_loading_w==552 && host_loading_h==450);
    unsigned char* modal_pixels=malloc(READER_CACHE_BYTES);CHECK(modal_pixels);memcpy(modal_pixels,app.canvas.pixels,READER_CACHE_BYTES);
    unsigned presents=host_loading_presents;uint64_t at=host_clock_us;
    host_clock_us=(online.animate_at-1)*1000;CHECK(online_modal_animate() && host_loading_presents==presents);
    host_clock_us=online.animate_at*1000;CHECK(online_modal_animate() && host_loading_presents==presents+1);
    CHECK(online.animate_at==now()+3000);
    CHECK(host_loading_x==ring.x && host_loading_y==ring.y && host_loading_w==64 && host_loading_h==66);
    unsigned changes=0;
    for(int y=0;y<app.canvas.height;++y)for(int x=0;x<app.canvas.width;++x) {
        size_t offset=(size_t)y*app.canvas.width+x;
        if(modal_pixels[offset]!=app.canvas.pixels[offset]){++changes;CHECK(x>=ring.x && x<ring.x+ring.w && y>=ring.y && y<ring.y+ring.h);}
    }
    CHECK(changes);free(modal_pixels);host_clock_us=at+2000000;
    tap(10,10);CHECK(online.opening && app.page==THINGSOS_UI_WEIDU);
    online_poll();CHECK(online.running && host_weidu_chapter==0 && host_weidu_chapters==3);
    CHECK(!app.opened); /* Current chapter alone must not bypass warm-up. */
    host_weidu.busy=false;host_weidu.phase=WD_ERROR;++host_weidu.serial;
    online_poll();CHECK(online.error && online_modal_visible() && !app.opened);
    draw();canvas_snapshot("online-loading-card-error");
    host_network_connected=false;tap(card.x+100,card.y+350);CHECK(online.error && !host_weidu.busy);
    host_network_connected=true;tap(card.x+100,card.y+350);CHECK(!online.error && online.opening);
    online_poll();CHECK(online.running);
    online_arrive(2);host_weidu.busy=false;host_weidu.phase=WD_READY;
    app.force_refresh=false;
    online_poll();CHECK(app.opened && app.chapter_index==0 && !online_pending() && !app.force_refresh && online.cleanup);
    host_loading_result=1;
    CHECK(online_modal_present_frame(false)!=ESP_OK && host_full_refreshes==full_refreshes && host_loading_w==684 && host_loading_h==1216);
    online_modal_presented(false);CHECK(online.cleanup);
    /* A failed exit must request normalization again, including if the menu
       opens before retry. Successful presentation consumes the request. */
    app.menu=true;draw();host_loading_result=ESP_OK;
    CHECK(online_modal_present_frame(false)==ESP_OK && host_full_refreshes==full_refreshes);
    online_modal_presented(true);CHECK(!online.cleanup);
    app.menu=false;full_refreshes=host_full_refreshes;
    app.force_refresh=false;
    CHECK(load_chapter(1) && load_chapter(2));
    app.activity_at=now();online_poll();CHECK(online.running && host_weidu_chapter==3 && host_weidu_chapters==2);
    unsigned calls=host_weidu_calls;
    online_need(4);online_poll();CHECK(online.waiting && host_weidu_calls==calls && host_weidu.busy);
    online_need(5);online_poll();CHECK(online.waiting && host_weidu_chapter==5 && host_weidu_chapters==3);
    draw();online_modal_presented(true);
    app.force_refresh=false;
    online_arrive(5);online_poll();CHECK(app.chapter_index==5 && !online.waiting && host_weidu.busy && !app.force_refresh && online.cleanup);
    app.force_refresh=false;
    host_weidu.busy=false;host_weidu.phase=WD_READY;online_poll();
    CHECK(online.retry_at==UINT64_MAX); /* Last chapter has no future work. */
    CHECK(load_chapter(3));app.start=2;online_need(4);CHECK(online_modal_visible());
    draw();canvas_snapshot("online-loading-card-chapter");
    app.landscape_enabled=true;draw();canvas_snapshot("online-loading-card-landscape");
    card=online_card_rect();CHECK(app.canvas.width==1216 && app.canvas.height==684 && card.x==342 && card.y==127);
    online_modal_presented(true);host_clock_us=online.animate_at*1000;
    CHECK(online_modal_present_frame(false)==ESP_OK && host_full_refreshes==full_refreshes && host_loading_w==1216 && host_loading_h==684);
    CHECK(online_modal_animate() && host_loading_x==576 && host_loading_y==164 && host_loading_w==64 && host_loading_h==66);
    app.landscape_enabled=false;draw();
    unsigned index=app.chapter_index;capacitive_key(THINGSOS_KEY_NEXT);CHECK(app.chapter_index==index && online.waiting);
    app.force_refresh=false;
    card=online_card_rect();tap(card.x+250,card.y+350);CHECK(!online_pending() && app.chapter_index==index && app.start==2 && online.cleanup && !app.force_refresh);
    app.landscape_enabled=true;draw();
    CHECK(online_modal_present_frame(false)==ESP_OK && host_full_refreshes==full_refreshes && host_loading_w==1216 && host_loading_h==684);
    online_modal_presented(true);CHECK(!online.cleanup);
    app.landscape_enabled=false;draw();
    app.wd_active=false;release_book();host_network_connected=false;
    app.page=THINGSOS_UI_WEIDU;CHECK(online_open(&book) && app.opened);
    app.wd_active=false;release_book();host_network_connected=true;
    /* Restore the independent fixture for repeatable runs. */
    snprintf(path,sizeof(path),"%s/book-1234567890/online-2-3-img.xhtml",host_online_root);remove(path);
    snprintf(path,sizeof(path),"%s/book-1234567890/online-5-6-img.xhtml",host_online_root);remove(path);
    /* An illustration decoded from the online SD cache uses the same reader
       body rectangle in both orientations, with no network available. */
    snprintf(host_online_root,sizeof(host_online_root),"%s/online-window-source",fixture_root);
    CHECK(thingsos_weidu_stream_path(host_online_root,book.id,path,sizeof(path)));
    char upgraded[700],legacy[700];
    snprintf(upgraded,sizeof(upgraded),"%s/book-1234567890/online-0-1-img.xhtml",host_online_root);
    snprintf(legacy,sizeof(legacy),"%s/book-1234567890/online-0-1.xhtml",host_online_root);
    CHECK(!rename(upgraded,legacy));
    memset(&online,0,sizeof(online));host_weidu.busy=false;app.page=THINGSOS_UI_WEIDU;
    CHECK(online_open(&book) && online.upgrading_images && !app.opened);
    host_network_connected=false;online_poll();CHECK(app.opened && !online_pending());
    app.wd_active=false;release_book();host_network_connected=true;app.page=THINGSOS_UI_WEIDU;
    CHECK(online_open(&book) && online.upgrading_images && !app.opened);
    online_poll();CHECK(online.running && !app.opened);
    CHECK(!rename(legacy,upgraded));
    host_weidu.busy=false;host_weidu.phase=WD_READY;online_poll();
    CHECK(app.opened && !online_pending());
    app.wd_active=false;release_book();
    /* Existing chapter data can be complete while the cover file is missing. */
    char cover_toc[700];snprintf(cover_toc,sizeof(cover_toc),"%s/book-1234567890/toc.dat",host_online_root);
    FILE* toc=fopen(cover_toc,"rb+");thingsos_weidu_chapter_t first;
    CHECK(toc && fread(&first,sizeof(first),1,toc)==1);
    thingsos_weidu_chapter_t cover=first;strcpy(cover.title,"封面");
    CHECK(!fseek(toc,0,SEEK_SET) && fwrite(&cover,sizeof(cover),1,toc)==1 && !fclose(toc));
    CHECK(thingsos_weidu_stream_needs_cover(host_online_root,book.id));
    app.page=THINGSOS_UI_WEIDU;
    CHECK(online_open(&book) && online.opening && !app.opened);
    online_poll();CHECK(online.running && host_weidu_prepare_cover);
    host_weidu.busy=false;host_weidu.phase=WD_READY;online_poll();
    CHECK(app.opened && !online_pending()); /* Optional cover failure leaves reading available. */
    app.wd_active=false;release_book();
    toc=fopen(cover_toc,"rb+");CHECK(toc && fwrite(&first,sizeof(first),1,toc)==1 && !fclose(toc));
    /* Enter a previously cached partial chapter through normal navigation.
       Even with a complete next chapter, retry the current chapter first. */
    char held[710];
    snprintf(upgraded,sizeof(upgraded),"%s/book-1234567890/online-4-5-img.xhtml",host_online_root);
    snprintf(held,sizeof(held),"%s.hold",upgraded);
    CHECK(!rename(upgraded,held));
    CHECK(weidu_open_path(&book,path) && load_chapter(4) && app.chapter.image_count==2);
    host_weidu.busy=false;online_poll();
    CHECK(online.running && !online_pending() && host_weidu_chapter==4 && host_weidu_chapters==1);
    unsigned repair_calls=host_weidu_calls;
    host_weidu.busy=false;host_weidu.phase=WD_READY;online_poll();online_poll();
    CHECK(app.chapter.image_count==2 && host_weidu_calls==repair_calls); /* No tight retry loop. */
    CHECK(load_chapter(3) && load_chapter(4));online_poll();
    CHECK(host_weidu_calls==repair_calls+1 && host_weidu_chapter==4);
    CHECK(!rename(held,upgraded));
    host_weidu.busy=false;host_weidu.phase=WD_READY;app.start=2;online_poll();
    CHECK(app.chapter.image_count==2 && app.start==2); /* Preserve an in-chapter position. */
    CHECK(!rename(upgraded,held));
    CHECK(load_chapter(3) && load_chapter(4));online_poll();
    CHECK(host_weidu_calls==repair_calls+2 && host_weidu_chapter==4);
    CHECK(!rename(held,upgraded));
    host_weidu.busy=false;host_weidu.phase=WD_READY;app.dirty=false;online_poll();
    CHECK(app.chapter_index==4 && app.start==0 && app.chapter.image_count==3 && app.dirty);
    app.wd_active=false;release_book();
    host_network_connected=false;
    CHECK(weidu_open_path(&book,path) && load_chapter(4) && app.chapter.image_count==3);
    for(unsigned orientation=0;orientation<2;++orientation) {
        app.landscape_enabled=orientation!=0;app.start=app.chapter.images[0].offset;draw();
        CHECK(app.image_hit_count && app.image_hits[0].h*2==app.image_hits[0].w);
        CHECK(app.image_hits[0].x==reader_body_left()+(reader_body_width()-app.image_hits[0].w)/2);
        CHECK(app.image_hits[0].y==reader_body_top() && app.image_hits[0].h<=reader_body_height());
        if(orientation)CHECK(app.image_hits[0].w<reader_body_width());
        else CHECK(app.image_hits[0].w==reader_body_width());
        unsigned char* pixels=NULL;unsigned w=0,h=0;
        CHECK(image_load(app.chapter.images,&pixels,&w,&h) && w==4 && h==2 && pixels[0]==0 && pixels[3]==255);
        free(pixels);
    }
    app.landscape_enabled=false;app.wd_active=false;release_book();host_network_connected=true;
    memset(&online,0,sizeof(online));host_weidu.busy=false;
}
static void canvas_snapshot(const char* name);
static bool resume_rows(void* context) {
    unsigned* remaining=context;
    if(!*remaining)return false;
    --*remaining;return true;
}
static void reader_resume_equivalence(void) {
    unsigned char* full=malloc(READER_CACHE_BYTES),*sliced=malloc(READER_CACHE_BYTES);
    thingsos_text_hits_t* a=calloc(1,sizeof(*a)),*b=calloc(1,sizeof(*b));
    CHECK(full && sliced && a && b);
    for(unsigned mode=0;mode<3;++mode)for(unsigned wide=0;wide<2;++wide) {
        memset(full,255,READER_CACHE_BYTES);memset(sliced,255,READER_CACHE_BYTES);
        memset(a,0,sizeof(*a));memset(b,0,sizeof(*b));
        thingsos_canvas_t ca={full,wide?1216:684,wide?684:1216},cb={sliced,ca.width,ca.height};
        app.font.monochrome=mode==1;app.font.dither=mode==2;thingsos_font_size(&app.font,app.px);
        size_t end=thingsos_text_page_layout(&app.font,&ca,&app.chapter,app.start,24,90,ca.width-48,ca.height-176,145,app.px*2,app.epub_open,NULL,NULL,a);
        thingsos_text_cursor_t cursor={0};bool completed=false;size_t got=app.start;
        unsigned slices=0;
        while(!completed) {
            size_t before=got;unsigned rows=2;
            got=thingsos_text_page_resume(&app.font,&cb,&app.chapter,app.start,24,90,cb.width-48,cb.height-176,145,app.px*2,app.epub_open,NULL,NULL,b,resume_rows,&rows,&completed,&cursor);
            CHECK(++slices<100 && (completed || (cursor.active && got>before)));
        }
        CHECK(slices>1 && got==end && !cursor.active);
        CHECK(!memcmp(full,sliced,READER_CACHE_BYTES));
        CHECK(!memcmp(a,b,sizeof(*a)));
    }
    app.font.monochrome=false;app.font.dither=app.refresh_mode==1;
    free(full);free(sliced);free(a);free(b);
}
static void font_recovery_test(void) {
    thingsos_font_t original=app.font;char original_path[320];
    memcpy(original_path,app.font_path,sizeof(original_path));
    memset(&app.font,0,sizeof(app.font));CHECK(thingsos_board_font(&app.font));
    unsigned before=app.font.errors;
    font_fault_begin(&app.font);
    thingsos_glyph(&app.font,&app.canvas,0x4e2d,80,150);
    font_fault_end(&app.font);
    CHECK(app.font.errors!=before);
    void* failed_face=app.font.face;
    CHECK(!font_frame_ready(before));
    CHECK(app.font.face && app.font.face!=failed_face && app.font_retries==1);
    strcpy(app.font_path,"/sdcard/fonts/not-present.ttf");
    app.font_retries=0;before=app.font.errors;++app.font.errors;
    CHECK(!font_frame_ready(before) && !app.font_path[0]);
    before=app.font.errors;++app.font.errors;
    CHECK(!font_frame_ready(before) && app.font_retries==2);
    /* Persistent failures still return control to presentation/input, rather
       than invoking fallback() and permanently leaving the main task. */
    for(unsigned i=0;i<4;++i) {
        before=app.font.errors;++app.font.errors;
        CHECK(font_frame_ready(before) && app.font_retries==2);
    }
    CHECK(font_frame_ready(app.font.errors) && !app.font_retries);
    thingsos_font_close(&app.font);app.font=original;
    memcpy(app.font_path,original_path,sizeof(original_path));
    thingsos_font_t original_ui=app.ui_font;memset(&app.ui_font,0,sizeof(app.ui_font));
    CHECK(thingsos_board_font(&app.ui_font));void* reader_face=app.font.face;
    before=app.ui_font.errors;unsigned body_errors=app.font.errors;
    font_fault_begin(&app.ui_font);label("中",80,150,30,80);font_fault_end(&app.ui_font);
    CHECK(app.font.errors==body_errors); /* UI drawing never borrows the reading face. */
    CHECK(app.ui_font.errors!=before&&!ui_font_frame_ready(before));
    CHECK(app.font.face==reader_face&&app.ui_font_retries==1);
    CHECK(ui_font_frame_ready(app.ui_font.errors)&&!app.ui_font_retries);
    thingsos_font_close(&app.ui_font);app.ui_font=original_ui;
    app.dirty=app.force_refresh=false;
}
static void jump_job_test(void) {
    /* Reuse the long TXT's known complete index as an independent oracle. */
    size_t count=app.page_count,position=app.start;
    thingsos_page_position_t* expected=malloc(count*sizeof(*expected));CHECK(expected);
    memcpy(expected,app.pages,count*sizeof(*expected));
    bool sd=host_sd;host_sd=false;app.asleep=false;app.page=THINGSOS_UI_READING;
    app.menu=app.jump=true;app.pagination_started=false;
    CHECK(reader_jump_request(true,90) && app.jump_pending && !app.page_count && app.start==position);
    CHECK(!go_page(0)); /* An incomplete index can no longer start a full-book loop. */
    app.dirty=false;reader_jump_poll();
    CHECK(app.jump_pending && app.pagination_cursor.active && !app.page_count && app.start==position);
    tap(500,1150);CHECK(app.jump_pending && app.start==position); /* Repeated confirm is inert. */
    tap(100,1150);CHECK(!app.jump_pending && app.start==position);
    CHECK(reader_jump_request(true,90));
    unsigned polls=0;
    while(app.jump_pending && polls++<20000){app.dirty=false;reader_jump_poll();}
    CHECK(!app.jump_pending && app.pagination_valid && polls>count && app.page_count==count);
    CHECK(!memcmp(expected,app.pages,count*sizeof(*expected)));
    CHECK(current_page()==thingsos_page_percent(90,count) && !app.menu && !app.jump);
    CHECK(reader_jump_request(true,0) && current_page()==0 && !app.jump_pending);
    CHECK(reader_jump_request(true,100) && current_page()==count-1);
    CHECK(go_page(1));position=app.start;
    /* Absolute pages are range-checked after indexing, not against the prefix. */
    app.pagination_started=false;CHECK(pagination_prepare());
    feature_editor(4);CHECK(thingsos_input_insert(&app.input,"65536"));tap(500,1040);
    CHECK(app.jump_pending && !app.jump_by_percent && app.jump_target==65535 && app.start==position);
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(!app.jump_pending && app.start==position);
    CHECK(reader_jump_request(false,65535));polls=0;
    while(app.jump_pending && polls++<20000){app.dirty=false;reader_jump_poll();}
    CHECK(!app.jump_pending && app.start==position && strstr(app.message,"超出"));
    /* Allocation failure and malformed index state leave a readable position. */
    app.pagination_started=false;CHECK(reader_jump_request(true,50));
    pagination_fail_allocate=true;app.dirty=false;reader_jump_poll();pagination_fail_allocate=false;
    CHECK(!app.jump_pending && app.pagination_failed && !strcmp(app.pagination_error,"memory") && app.start==position);
    CHECK(strstr(app.message,"内存不足"));
    thingsos_font_t retained_font=app.font;memset(&app.font,0,sizeof(app.font));
    CHECK(thingsos_font_file(&app.font,host_font));
    app.pagination_started=false;CHECK(reader_jump_request(true,50));
    font_fault_begin(&app.font);app.dirty=false;reader_jump_poll();font_fault_end(&app.font);
    CHECK(!app.jump_pending && app.pagination_failed && !strcmp(app.pagination_error,"layout") && app.start==position);
    thingsos_font_close(&app.font);app.font=retained_font;
    app.pagination_started=false;CHECK(reader_jump_request(true,50));
    app.pagination_chapter=UINT_MAX;app.dirty=false;reader_jump_poll();
    CHECK(!app.jump_pending && app.pagination_failed && app.start==position);
    app.pagination_started=false;CHECK(reader_jump_request(true,50));
    uint64_t clock=host_clock_us;host_clock_us+=301000000;app.dirty=false;reader_jump_poll();host_clock_us=clock;
    CHECK(!app.jump_pending && app.start==position && strstr(app.message,"耗时过长"));
    CHECK(reader_jump_request(true,50));++app.px;app.dirty=false;reader_jump_poll();--app.px;
    CHECK(!app.jump_pending && app.start==position); /* Layout identity changed. */
    app.pagination_started=false;CHECK(reader_jump_request(true,50));
    ++app.runtime.location.book_id;app.dirty=false;reader_jump_poll();--app.runtime.location.book_id;
    CHECK(!app.jump_pending && app.start==position);
    app.pagination_started=false;CHECK(reader_jump_request(true,50));
    power_action(1);CHECK(!app.jump_pending && app.asleep && app.start==position);power_action(1);
    CHECK(!app.asleep && app.start==position);
    app.page=THINGSOS_UI_READING;app.pagination_started=false;CHECK(reader_jump_request(true,50));
    navigate(THINGSOS_UI_SETTINGS);CHECK(!app.jump_pending && app.start==position);
    navigate(THINGSOS_UI_READING);app.pagination_started=false;CHECK(paginate());
    CHECK(app.page_count==count && !memcmp(expected,app.pages,count*sizeof(*expected)));
    app.menu=app.jump=false;app.message[0]=0;host_sd=sd;free(expected);
}
static void reader_contact_test(void) {
    app.asleep=false;app.page=THINGSOS_UI_READING;app.menu=true;app.jump=false;
    app.landscape_enabled=false;app.immersive_enabled=false;reader_canvas_geometry();
    host_nvs_available=true;host_nvs_commit_fail=false;
    CHECK(buffered_touch_page(THINGSOS_UI_READER_MORE));
    CHECK(buffered_touch_page(THINGSOS_UI_READING));
    unsigned px=app.px;
    CHECK(reading_menu_press(580,560) && app.px==thingsos_reader_size_step(px,true));
    CHECK(reading_menu_press(400,720) && app.line_choice==1);
    CHECK(reading_menu_press(550,800) && app.margin_choice==2);
    CHECK(reading_menu_press(250,880) && app.weight_choice==0);
    CHECK(!reading_menu_press(355,720)); /* Button gap remains inert. */
    CHECK(reading_menu_press(550,1070) && app.jump);
    CHECK(!reading_menu_press(550,1070));app.jump=false;
    CHECK(reading_menu_press(517,1210) && app.page==THINGSOS_UI_READER_MORE);
    unsigned old=app.refresh_mode;
    CHECK(reader_more_press(490,250) && app.refresh_mode==1);
    CHECK(reader_more_press(600,250) && app.refresh_mode==2);
    CHECK(reader_more_press(350,250) && app.refresh_mode==0);
    CHECK(reader_more_press(580,450) && app.tilt_enabled);
    CHECK(reader_more_press(500,450) && !app.tilt_enabled);
    app.confirm_action=1;CHECK(!reader_more_press(580,450));app.confirm_action=0;
    app.refresh_mode=old;app.px=px;app.line_choice=app.margin_choice=0;
    reader_more_back();app.menu=false;
}
static void reader_surface_refresh_test(void) {
    CHECK(app.opened && app.page==THINGSOS_UI_READING);
    size_t position=app.start;unsigned chapter=app.chapter_index;
    const thingsos_ui_page_t pages[]={THINGSOS_UI_READING,THINGSOS_UI_CHAPTERS,
        THINGSOS_UI_BOOKMARKS,THINGSOS_UI_FONTS,THINGSOS_UI_READER_MORE};
    for(unsigned from=0;from<5;++from)for(unsigned to=0;to<5;++to) {
        app.page=pages[from];app.force_refresh=app.transition_refresh=false;
        navigate(pages[to]);
        CHECK(!app.transition_refresh && !app.force_refresh && reader_surface(app.page));
        CHECK(app.start==position && app.chapter_index==chapter);
        CHECK(reader_ui_active()==(pages[to]!=THINGSOS_UI_READING));
    }
    app.page=THINGSOS_UI_READING;
    bool* overlays[]={&app.menu,&app.jump,&app.image_view.open,&app.footnote_open,
        &app.annotation_open,&app.annotation_selecting,&app.wd_notes_open,
        &app.wd_thought_open,&app.wd_consent,&app.wd_cloud_prompt,&app.wd_sync_panel};
    for(unsigned i=0;i<sizeof(overlays)/sizeof(overlays[0]);++i) {
        *overlays[i]=true;CHECK(reader_ui_active());*overlays[i]=false;CHECK(!reader_ui_active());
    }
    app.input_return=THINGSOS_UI_READING;
    app.editing=4;CHECK(reader_surface(THINGSOS_UI_PASSWORD));
    app.editing=6;CHECK(reader_surface(THINGSOS_UI_PASSWORD));
    app.editing=0;CHECK(!reader_surface(THINGSOS_UI_PASSWORD));
    app.force_refresh=app.transition_refresh=false;app.menu=true;
    tap(550,1070);CHECK(app.jump && !app.transition_refresh && !app.force_refresh);
    tap(100,1150);CHECK(!app.jump && !app.transition_refresh && !app.force_refresh);
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(!app.menu && !app.transition_refresh && !app.force_refresh);
    app.menu=true;app.jump=true;capacitive_key(THINGSOS_KEY_PREVIOUS);
    CHECK(!app.jump && app.menu && !app.transition_refresh);
    capacitive_key(THINGSOS_KEY_MENU);CHECK(!app.menu && !app.transition_refresh);
    CHECK(app.start==position && app.chapter_index==chapter);
    navigate(THINGSOS_UI_LIBRARY);CHECK(app.transition_refresh); /* Real scene exit still cleans. */
    navigate(THINGSOS_UI_READING);app.transition_refresh=false;app.dirty=false;
}
static void reader_key_priority_test(void) {
    thingsos_keys_t keys={0};thingsos_touch_queue_t q={0};thingsos_touch_sample_t s;
    CHECK(buffered_touch_page(THINGSOS_UI_CHAPTERS) && buffered_touch_page(THINGSOS_UI_BOOKMARKS));
    app.page=THINGSOS_UI_READING;app.asleep=false;app.menu=app.jump=false;app.dirty=true;
    size_t position=app.start;
    /* A complete real 55 ms Home touch can arrive while a background redraw
       is pending. Consume its release before spending that redraw's scan. */
    thingsos_touch_queue_push(&q,(thingsos_touch_sample_t){100,240,1500,1,true});
    thingsos_touch_queue_push(&q,(thingsos_touch_sample_t){155,240,1500,1,false});
    CHECK(thingsos_touch_queue_pop(&q,&s));
    CHECK(thingsos_keys_step(&keys,s.down,s.x,s.y,s.at_ms)==THINGSOS_KEY_NONE);
    CHECK(reader_input_awaiting_release(&keys,false,false,false));
    CHECK(!reader_input_can_idle(&keys,true,false,true));
    CHECK(thingsos_touch_queue_pop(&q,&s));
    thingsos_key_action_t action=thingsos_keys_step(&keys,s.down,s.x,s.y,s.at_ms);
    CHECK(action==THINGSOS_KEY_MENU && !reader_input_awaiting_release(&keys,false,false,false));
    capacitive_key(action);CHECK(app.menu && app.ui_action_at && app.start==position);
    CHECK(thingsos_keys_step(&keys,false,240,1500,155)==THINGSOS_KEY_NONE);
    CHECK(reader_input_can_idle(&keys,false,false,false));
    CHECK(!reader_input_can_idle(&keys,false,false,true));
    CHECK(!reader_input_can_idle(&keys,false,true,false));
    CHECK(reader_input_awaiting_release(&keys,true,false,false));
    CHECK(!reader_input_awaiting_release(&keys,true,true,true)); /* A contact button can draw immediately. */
    CHECK(thingsos_keys_step(&keys,true,240,1500,200)==THINGSOS_KEY_NONE);
    CHECK(thingsos_keys_step(&keys,false,240,1500,219)==THINGSOS_KEY_NONE);
    CHECK(thingsos_keys_step(&keys,true,240,1500,300)==THINGSOS_KEY_NONE);
    action=thingsos_keys_step(&keys,false,240,1500,320);CHECK(action==THINGSOS_KEY_MENU);
    capacitive_key(action);CHECK(!app.menu && app.start==position);
    CHECK(thingsos_keys_step(&keys,true,240,1500,1000)==THINGSOS_KEY_NONE);
    CHECK(thingsos_keys_step(&keys,true,240,1500,2000)==THINGSOS_KEY_HOME);
    CHECK(!reader_input_awaiting_release(&keys,false,false,false));
    CHECK(thingsos_keys_step(&keys,false,240,1500,2100)==THINGSOS_KEY_NONE);
    app.ui_action_at=0;app.dirty=false;
}
static void reader_font_parent_test(void) {
    CHECK(app.opened);app.page=THINGSOS_UI_READING;app.asleep=false;app.menu=true;app.jump=false;
    bool sd=host_sd;host_sd=false;app.landscape_enabled=false;reader_canvas_geometry();
    size_t position=app.start;unsigned chapter=app.chapter_index;
    void* active_font=app.font.face;
    CHECK(reading_menu_press(104,1205) && app.page==THINGSOS_UI_FONTS && app.font_from_reader);
    draw();
    for(int y=1100;y<1216;++y)for(int x=0;x<684;++x)CHECK(app.canvas.pixels[(size_t)y*684+x]==255);
    canvas_snapshot("reader-font-parent");
    for(unsigned tab=0;tab<4;++tab){tap((int)tab*171+85,1150);CHECK(app.page==THINGSOS_UI_FONTS);}
    CHECK(reader_font_press(60,150) && app.page==THINGSOS_UI_READING && app.menu && !app.jump);
    CHECK(app.start==position && app.chapter_index==chapter && app.font.face==active_font);
    CHECK(reading_menu_press(103,1210) && app.font_from_reader);
    CHECK(reader_font_press(150,1040) && app.page==THINGSOS_UI_READING && app.menu);
    CHECK(reading_menu_press(100,1160) && app.font_from_reader);
    capacitive_key(THINGSOS_KEY_MENU);CHECK(app.page==THINGSOS_UI_READING && app.menu);
    CHECK(reading_menu_press(100,1160));
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(app.page==THINGSOS_UI_READING && app.menu);
    CHECK(reading_menu_press(100,1160));
    font_preview_current();tap(500,1040);CHECK(app.page==THINGSOS_UI_READING && app.menu);
    navigate(THINGSOS_UI_SETTINGS);font_preview_current();navigate(THINGSOS_UI_FONTS);
    CHECK(!app.font_from_reader && !reader_surface(app.page));
    draw();unsigned nav_ink=0;
    for(int y=1100;y<1216;++y)for(int x=0;x<684;++x)nav_ink+=app.canvas.pixels[(size_t)y*684+x]<128;
    CHECK(nav_ink>0);tap(100,1040);CHECK(app.page==THINGSOS_UI_SETTINGS);
    navigate(THINGSOS_UI_READING);app.menu=false;host_sd=sd;app.ui_action_at=0;app.dirty=false;
}
static void canvas_snapshot(const char* name) {
    char path[700];
    snprintf(path, sizeof(path), "%s/ui-%s.pgm", fixture_root, name);
    FILE* file = fopen(path, "wb");
    CHECK(file);
    fprintf(file, "P5\n%d %d\n255\n",app.canvas.width,app.canvas.height);
    if(app.canvas.inverted) {
        unsigned char row[1216];
        CHECK(app.canvas.width>0 && app.canvas.width<=(int)sizeof(row));
        for(int y=0;y<app.canvas.height;++y) {
            for(int x=0;x<app.canvas.width;++x)row[x]=255-app.canvas.pixels[(size_t)y*app.canvas.width+x];
            CHECK(fwrite(row,1,app.canvas.width,file)==(size_t)app.canvas.width);
        }
    } else CHECK(fwrite(app.canvas.pixels, 1, 684u * 1216u, file) == 684u * 1216u);
    fclose(file);
}
static void snapshot(const char* name) {
    app.message[0] = 0;
    draw();
    canvas_snapshot(name);
}
/* A notice raised from a handled tap must still expire on its own. */
static void transient_message_test(void) {
    bool sd=host_sd;host_sd=true;
    thingsos_ui_page_t page=app.page;bool dirty=app.dirty;uint64_t until=app.message_until;
    app.page=THINGSOS_UI_MUSIC;app_media.music_count=0;app.audio=host_audio;
    app.message[0]=0;app.message_until=0;app.dirty=false;
    uint64_t started=now();
    tap(340,629); /* Play on the music page with an empty music folder. */
    CHECK(app.message[0] && strstr(app.message,"music"));
    CHECK(app.message_until==started+3000);
    app.dirty=false;host_clock_us+=3001000;message_expire_poll();
    CHECK(!app.message[0] && !app.message_until && app.dirty);
    say("hidden before expiry");app.message[0]=0;app.dirty=false;
    host_clock_us+=3001000;message_expire_poll();CHECK(!app.dirty && !app.message_until);
    app.page=page;app.dirty=dirty;app.message_until=until;host_sd=sd;
}
static unsigned region_ink(int top,int bottom) {
    unsigned count=0;
    for(int y=top;y<bottom;++y) for(int x=36;x<648;++x)
        count+=app.canvas.pixels[(size_t)y*684+x]<128;
    return count;
}
static void launcher_audio_transfer_test(void){
    host_sd=true;host_network_connected=true;app.wifi_enabled=true;
    app.controls_open=false;app_media.playlist=false;
    host_audio=(thingsos_audio_status_t){.state=THINGSOS_AUDIO_PLAYING,.serial=50};
    strlcpy(host_audio.path,"/sdcard/music/test.mp3",sizeof(host_audio.path));
    app.audio=host_audio;navigate(THINGSOS_UI_APPS);draw();
    CHECK(region_ink(950,1075)>0);
    unsigned char* icons=malloc(684u*200u);CHECK(icons);
    memcpy(icons,app.canvas.pixels+150u*684,684u*200u);
    tap(82,180);CHECK(app.page==THINGSOS_UI_APPS&&host_audio.state==THINGSOS_AUDIO_PLAYING);
    CHECK(strstr(app.message,"暂停"));draw();canvas_snapshot("apps-audio-transfer-notice");
    CHECK(app.canvas.pixels[82u*684+24]==235);
    CHECK(!memcmp(icons,app.canvas.pixels+150u*684,684u*200u));free(icons);
    /* A pause removes both the footer and its invisible tap target. */
    CHECK(thingsos_audio_pause(true));app_poll();
    host_clock_us+=3001000;message_expire_poll();CHECK(!app.message[0]);draw();
    CHECK(!app_launcher_audio_visible()&&region_ink(950,1075)==0);
    canvas_snapshot("apps-paused-no-audio-footer");
    tap(300,1000);CHECK(app.page==THINGSOS_UI_APPS);
    /* Query the worker, even if the cached UI state still says Playing. */
    app.audio.state=THINGSOS_AUDIO_PLAYING;tap(82,180);
    CHECK(app.page==THINGSOS_UI_TRANSFER&&!thingsos_audio_active());
    CHECK(app.audio.state==THINGSOS_AUDIO_IDLE&&!host_audio.ended);
    navigate(THINGSOS_UI_APPS);app_poll();draw();CHECK(region_ink(950,1075)==0);
    /* Failed or unfinished cleanup must not enter Transfer. */
    host_audio.state=THINGSOS_AUDIO_PLAY_PAUSED;++host_audio.serial;app_poll();
    host_audio_stop_reject=true;tap(82,180);
    CHECK(app.page==THINGSOS_UI_APPS&&host_audio.state==THINGSOS_AUDIO_PLAY_PAUSED);
    CHECK(strstr(app.message,"失败"));host_audio_stop_reject=false;
    host_audio_deferred=true;tap(82,180);
    CHECK(app.page==THINGSOS_UI_APPS&&host_audio.state==THINGSOS_AUDIO_STOPPING);
    host_audio_deferred=false;
    /* Recording, including a pause, retains its save protection and footer. */
    const thingsos_audio_state_t records[]={THINGSOS_AUDIO_RECORDING,THINGSOS_AUDIO_RECORD_PAUSED};
    for(unsigned i=0;i<2;++i){
        host_audio.state=records[i];++host_audio.serial;app_poll();tap(82,180);
        CHECK(app.page==THINGSOS_UI_APPS&&host_audio.state==records[i]);
        CHECK(strstr(app.message,"保存录音"));draw();CHECK(region_ink(950,1075)>0);
    }
    CHECK(thingsos_audio_stop());app_poll();draw();CHECK(region_ink(950,1075)==0);
    app.message[0]=0;app.message_until=0;
}
/* Exercise worker-confirmed recording feedback, rather than treating command
   submission as a completed SD write. These run on the actual firmware UI. */
static void recording_feedback_test(void) {
    host_sd=true;app.page=THINGSOS_UI_LIBRARY;app.controls_open=false;
    app.menu=app.jump=app.immersive_enabled=app.landscape_enabled=false;
    app.record_notice=RECORD_NOTICE_NONE;app.record_notice_until=0;
    app.record_start_pending=app.record_save_pending=false;
    host_audio=(thingsos_audio_status_t){.volume=50,.serial=100};app.audio=host_audio;
    host_audio_deferred=true;app.message[0]=0;
    CHECK(app_ai_record_toggle()&&host_audio.state==THINGSOS_AUDIO_STARTING);
    CHECK(app.record_start_pending&&app.record_notice==RECORD_NOTICE_PREPARING&&!app.record_notice_until);
    host_clock_us+=10000000;app_poll();message_expire_poll();
    CHECK(app.record_notice==RECORD_NOTICE_PREPARING&&!app_ai_record_toggle());
    host_audio.state=THINGSOS_AUDIO_RECORDING;++host_audio.serial;app_poll();
    CHECK(app.record_notice==RECORD_NOTICE_STARTED&&app.record_notice_until==now()+3000&&!app.message[0]);
    draw();canvas_snapshot("recording-feedback-start");
    CHECK(!feedback_mic_ink(15,1)&&feedback_mic_ink(15,3)&&feedback_mic_ink(15,24));
    CHECK(!feedback_mic_ink(15,26)&&feedback_mic_ink(15,30));
    thingsos_rect(&app.canvas,0,0,684,1216,255);feedback_mic_draw(0,0,30,36);
    canvas_snapshot("recording-mic-v3");
    unsigned gray=0;for(unsigned i=0;i<36*684;++i)gray+=app.canvas.pixels[i]>0&&app.canvas.pixels[i]<255;
    CHECK(gray>20);
    app.font.ui_size_percent=app.font.ui_weight_override=0;
    draw_status();unsigned char chrome[220*74];
    for(unsigned y=0;y<74;++y)memcpy(chrome+y*220,app.canvas.pixels+y*684,220);
    app.page=THINGSOS_UI_READING;draw_status();
    for(unsigned y=0;y<74;++y)CHECK(!memcmp(chrome+y*220,app.canvas.pixels+y*684,220));
    app.page=THINGSOS_UI_LIBRARY;
    host_clock_us+=2999000;message_expire_poll();CHECK(app.record_notice==RECORD_NOTICE_STARTED);
    host_clock_us+=1000;app.dirty=false;message_expire_poll();
    CHECK(app.record_notice==RECORD_NOTICE_NONE&&app.dirty&&app.feedback_refresh);
    CHECK(app.audio.state==THINGSOS_AUDIO_RECORDING);
    CHECK(app_ai_record_toggle()&&host_audio.state==THINGSOS_AUDIO_STOPPING);
    CHECK(app.record_save_pending&&app.record_notice==RECORD_NOTICE_SAVING&&!app.record_notice_until);
    host_clock_us+=10000000;app_poll();message_expire_poll();
    CHECK(app.record_notice==RECORD_NOTICE_SAVING&&!app_ai_record_toggle());
    host_audio.state=THINGSOS_AUDIO_ERROR;strcpy(host_audio.message,"录音写入失败，请检查 SD 卡");++host_audio.serial;app_poll();
    CHECK(!app.record_save_pending&&app.record_notice==RECORD_NOTICE_SAVE_ERROR&&!app.record_notice_until);
    host_clock_us+=5000000;message_expire_poll();CHECK(app.record_notice==RECORD_NOTICE_SAVE_ERROR);
    draw();canvas_snapshot("recording-feedback-error");
    tap(342,608);CHECK(app.record_notice==RECORD_NOTICE_NONE);
    host_audio_record_reject=true;CHECK(!app_ai_record_toggle());
    CHECK(app.record_notice==RECORD_NOTICE_START_ERROR&&!app.record_start_pending);
    host_audio_record_reject=false;CHECK(app_ai_record_toggle());
    host_audio.state=THINGSOS_AUDIO_ERROR;++host_audio.serial;app_poll();
    CHECK(app.record_notice==RECORD_NOTICE_START_ERROR&&!app.record_start_pending);
    CHECK(app_ai_record_toggle());host_audio.state=THINGSOS_AUDIO_IDLE;++host_audio.serial;app_poll();
    CHECK(app.record_notice==RECORD_NOTICE_START_ERROR&&!app.record_start_pending);
    CHECK(app_ai_record_toggle());host_audio.state=THINGSOS_AUDIO_RECORDING;++host_audio.serial;app_poll();
    host_audio_stop_reject=true;CHECK(!app_ai_record_toggle());
    CHECK(app.record_notice==RECORD_NOTICE_SAVE_ERROR&&!app.record_save_pending);
    host_audio_stop_reject=false;CHECK(app_ai_record_toggle());
    app_poll();CHECK(app.record_notice==RECORD_NOTICE_SAVING);
    host_audio.state=THINGSOS_AUDIO_IDLE;host_audio.message[0]=0;++host_audio.serial;app_poll();
    CHECK(app.record_notice==RECORD_NOTICE_SAVED&&!app.record_save_pending&&app.record_notice_until==now()+3000);
    draw();canvas_snapshot("recording-feedback-saved");
    host_clock_us+=3000000;message_expire_poll();CHECK(app.record_notice==RECORD_NOTICE_NONE);
    app_volume_set(54);draw();unsigned char at54=app.canvas.pixels[(452+142)*684+572+42];
    app_volume_set(55);CHECK(app.volume_overlay_until==now()+3000&&app.volume_overlay_pending);draw();
    CHECK(at54==238&&app.canvas.pixels[(452+142)*684+572+42]==0);
    CHECK(app.canvas.pixels[(452+100)*684+572+42]==238);
    CHECK(app.canvas.pixels[(452+180)*684+572+42]==0);
    CHECK(app.canvas.pixels[(452+10)*684+571]==255); /* No rectangular backdrop. */
    canvas_snapshot("volume-capsule-portrait");
    app_volume_set(0);draw();CHECK(app.canvas.pixels[(452+309)*684+572+42]==238);
    app_volume_set(1);draw();CHECK(app.canvas.pixels[(452+309)*684+572+42]==0);
    app_volume_set(1000);draw();CHECK(app.audio_volume==100&&app.canvas.pixels[(452+50)*684+572+42]==0);
    /* Slow layout/refresh used to consume the whole lifetime before the OSD
       reached the panel. Failed frames must not start the visible interval. */
    host_clock_us+=4000000;message_expire_poll();CHECK(app.volume_overlay_pending&&app.volume_overlay_until);
    draw();CHECK(app.volume_overlay_drawn);volume_overlay_presented(false);CHECK(app.volume_overlay_pending);
    volume_overlay_presented(true);CHECK(!app.volume_overlay_pending&&app.volume_overlay_until==now()+3000);
    /* Each accepted quick +/- press keeps it visible, including clamped ends. */
    for(unsigned i=0;i<8;++i){host_clock_us+=150000;app_volume_set(i%2?100:0);host_clock_us+=650000;
        message_expire_poll();CHECK(app.volume_overlay_pending&&app.volume_overlay_until);draw();volume_overlay_presented(true);
        CHECK(app.volume_overlay_until==now()+3000);}
    host_clock_us+=1200000;app_volume_set(60);draw();volume_overlay_presented(true);
    uint64_t volume_deadline=app.volume_overlay_until;
    host_clock_us+=1200000;draw();volume_overlay_presented(true);message_expire_poll();
    CHECK(app.volume_overlay_until==volume_deadline&&app.volume_overlay_until>now()); /* Other frames do not extend it. */
    host_clock_us+=1799000;message_expire_poll();CHECK(app.volume_overlay_until);
    host_clock_us+=1000;app.dirty=false;message_expire_poll();CHECK(!app.volume_overlay_until&&app.dirty);
    app.canvas.width=1216;app.canvas.height=684;
    thingsos_rect(&app.canvas,0,0,1216,684,255);draw_status();volume_overlay_draw();canvas_snapshot("volume-capsule-landscape");
    thingsos_rect(&app.canvas,0,0,1216,684,255);record_feedback_show(RECORD_NOTICE_STARTED,NULL);record_feedback_draw();canvas_snapshot("recording-feedback-landscape");
    CHECK(app.canvas.pixels[223*1216+608]==0);
    app.canvas.width=684;app.canvas.height=1216;
    app.record_notice=RECORD_NOTICE_NONE;app.record_notice_until=0;
    app.record_start_pending=app.record_save_pending=false;host_audio_deferred=false;
    app.feedback_refresh=false;app.dirty=false;
}
static time_t calendar_time(int year,int month,int day) {
    struct tm value={0};value.tm_year=year-1900;value.tm_mon=month-1;
    value.tm_mday=day;value.tm_hour=12;value.tm_isdst=-1;return mktime(&value);
}
static void statistics_heatmap_test(void) {
    thingsos_records_t* saved=malloc(sizeof(*saved));CHECK(saved);*saved=app.records;
    thingsos_records_init(&app.records);
    host_wall_time=calendar_time(2026,10,16);
    app.records.day_count=1;app.records.days[0]=(thingsos_day_record_t){20261016,60ULL*60000};
    app.records.book_day_count=5;
    app.records.book_days[0]=(thingsos_book_day_t){1,5ULL*60000,20260801};
    app.records.book_days[1]=(thingsos_book_day_t){2,15ULL*60000,20260801};
    app.records.book_days[2]=(thingsos_book_day_t){1,1,20260905};
    app.records.book_days[3]=(thingsos_book_day_t){1,999ULL*60000,20261016};
    app.records.book_days[4]=(thingsos_book_day_t){1,20ULL*60000,20261017};
    stats_calendar_t calendar;CHECK(stats_calendar_build(host_wall_time,&calendar));
    CHECK(calendar.count==92 && calendar.dates[0]==20260801 && calendar.dates[91]==20261031);
    CHECK(calendar.active==3 && calendar.duration[0]==20ULL*60000 && calendar.duration[76]==60ULL*60000 && calendar.duration[77]==0);
    CHECK(calendar.leading==5 && calendar.columns==14);
    static const uint64_t lower[]={0,1,5ULL*60000,10ULL*60000,15ULL*60000,30ULL*60000,45ULL*60000,60ULL*60000};
    for(unsigned i=0;i<8;++i) {
        CHECK(stats_heat_level(lower[i])==i);
        if(i)CHECK(stats_heat_level(lower[i]-1)==i-1);
    }
    navigate(THINGSOS_UI_STATS);app.stats_period=1;snapshot("stats-heatmap-exact");
    static const unsigned char shades[]={238,204,170,136,102,68,34,0};
    for(unsigned i=0;i<calendar.count;++i) {
        unsigned slot=calendar.leading+i;
        int x=STATS_HEAT_X+(slot/7)*STATS_HEAT_PITCH_X+12;
        int y=STATS_HEAT_Y+(slot%7)*STATS_HEAT_PITCH_Y+12;
        CHECK(app.canvas.pixels[(size_t)y*684+x]==(calendar.dates[i]>calendar.today?255:shades[stats_heat_level(calendar.duration[i])]));
        if(calendar.dates[i]>calendar.today)CHECK(app.canvas.pixels[(size_t)(y-12)*684+x]==204);
    }
    CHECK(app.canvas.pixels[(STATS_HEAT_Y+12)*684+STATS_HEAT_X+12]==255); /* Before range. */
    CHECK(!region_ink(590,615)); /* No heatmap grayscale legend remains. */
    CHECK(stats_calendar_build(calendar_time(2024,5,31),&calendar));
    CHECK(calendar.dates[0]==20240301 && calendar.dates[calendar.count-1]==20240531);
    CHECK(stats_calendar_build(calendar_time(2026,1,31),&calendar));
    CHECK(calendar.dates[0]==20251101 && calendar.dates[calendar.count-1]==20260131);
    CHECK(stats_calendar_build(calendar_time(2024,2,10),&calendar));
    CHECK(calendar.count==91 && calendar.dates[0]==20231201 && calendar.dates[90]==20240229);
    CHECK(stats_calendar_build(calendar_time(2026,2,10),&calendar));
    CHECK(calendar.count==90 && calendar.dates[0]==20251201 && calendar.dates[89]==20260228);
    CHECK(!stats_calendar_build(1,&calendar));
    /* Three complete calendar months proves month labels, sparse days and tallest bars. */
    thingsos_records_init(&app.records);
    CHECK(thingsos_records_book(&app.records,77,"/sdcard/books/history.txt","山川与日月"));
    CHECK(stats_calendar_build(host_wall_time,&calendar));
    for(unsigned i=0;i<calendar.count;++i)if(calendar.dates[i]<=calendar.today && i%7!=1 && i%9!=2)
        CHECK(thingsos_records_time(&app.records,77,calendar.dates[i],(uint64_t)(1+(i*13)%90)*60000));
    snapshot("stats-heatmap-three-months");
    app.stats_period=0;snapshot("stats-heatmap-daily");
    app.stats_period=2;snapshot("stats-heatmap-month");
    host_wall_time=1;snapshot("stats-heatmap-no-clock");
    host_wall_time=0;app.records=*saved;free(saved);app.stats_period=0;
}
static void statistics_period_test(void) {
    thingsos_records_t* saved=malloc(sizeof(*saved));CHECK(saved);*saved=app.records;
    time_t old_time=host_wall_time;host_wall_time=calendar_time(2026,10,21);
    thingsos_records_init(&app.records);
    CHECK(thingsos_records_book(&app.records,77,"/sdcard/books/stats.txt","时间与边界"));
    CHECK(thingsos_records_time(&app.records,77,20260930,90ULL*60000));
    CHECK(thingsos_records_time(&app.records,77,20261001,30ULL*60000));
    CHECK(thingsos_records_time(&app.records,77,20261019,20ULL*60000));
    CHECK(thingsos_records_time(&app.records,77,20261021,10ULL*60000));
    CHECK(thingsos_records_hour(&app.records,20261021,8,4ULL*60000));
    CHECK(thingsos_records_hour(&app.records,20261021,11,6ULL*60000));
    stats_period_t data;
    CHECK(stats_period_build(host_wall_time,0,&data));
    CHECK(data.count==24 && data.first==20261021 && data.last==20261021 && data.total==10ULL*60000);
    CHECK(data.values[8]==4ULL*60000 && data.values[11]==6ULL*60000 && !data.unknown);
    CHECK(data.book_ms[0]==data.total);
    CHECK(stats_period_build(host_wall_time,1,&data));
    CHECK(data.count==7 && data.first==20261019 && data.last==20261025 && data.total==30ULL*60000);
    CHECK(data.values[0]==20ULL*60000 && data.values[2]==10ULL*60000 && !data.values[6]);
    CHECK(stats_period_build(host_wall_time,2,&data));
    CHECK(data.count==31 && data.first==20261001 && data.last==20261031 && data.total==60ULL*60000);
    CHECK(data.values[0]==30ULL*60000 && data.values[18]==20ULL*60000 && data.book_ms[0]==data.total);
    CHECK(stats_period_build(calendar_time(2024,2,15),2,&data) && data.count==29 && data.last==20240229);
    CHECK(stats_period_build(calendar_time(2026,2,15),2,&data) && data.count==28 && data.last==20260228);
    CHECK(stats_period_build(calendar_time(2026,4,15),2,&data) && data.count==30);
    CHECK(stats_period_build(calendar_time(2026,1,1),1,&data) && data.first==20251229 && data.last==20260104);
    CHECK(!stats_period_build(1,0,&data) && !data.dated && !data.total);
    navigate(THINGSOS_UI_STATS);app.stats_period=0;snapshot("stats-fixed-day-hours");
    app.records.hour_date=0;CHECK(stats_period_build(host_wall_time,0,&data) && data.unknown==data.total);
    navigate(THINGSOS_UI_STATS);
    for(unsigned p=0;p<3;++p) {
        app.dirty=false;tap(STATS_TABS_X+p*STATS_TAB_WIDTH+STATS_TAB_WIDTH/2,STATS_TABS_Y+30);
        CHECK(app.page==THINGSOS_UI_STATS && app.stats_period==p);
        snapshot(p==0?"stats-fixed-day":p==1?"stats-fixed-week":"stats-fixed-month");
        app.dirty=false;tap(STATS_TABS_X+p*STATS_TAB_WIDTH+10,STATS_TABS_Y+10);
        CHECK(!app.dirty && app.page==THINGSOS_UI_STATS);
    }
    app.dirty=false;CHECK(stats_tab_press(342,170) && app.stats_period==1 && app.dirty);
    app.dirty=false;CHECK(stats_tab_press(342,170) && !app.dirty); /* Contact, once; same choice needs no scan. */
    CHECK(!stats_tab_press(342,600));
    CHECK(stats_tab_hit(35,160)==-1 && stats_tab_hit(648,160)==-1 && stats_tab_hit(100,208)==-1);
    app.dirty=false;tap(340,600);CHECK(!app.dirty);
    tap(40,160);CHECK(app.page==THINGSOS_UI_STATS && app.stats_period==0); /* Left tab never hits Back. */
    tap(40,100);CHECK(app.page==THINGSOS_UI_LIBRARY);
    /* Account a real interval across the hour boundary, retaining daily totals. */
    thingsos_records_init(&app.records);CHECK(thingsos_records_book(&app.records,77,"/sdcard/books/stats.txt","时间与边界"));
    struct tm at={0};at.tm_year=126;at.tm_mon=9;at.tm_mday=21;at.tm_hour=9;at.tm_min=0;at.tm_sec=1;at.tm_isdst=-1;
    host_wall_time=mktime(&at);
    bool opened=app.opened;thingsos_location_t loc=app.runtime.location;
    uint64_t reading=app.runtime.reading_ms,accounted=app.accounted_ms;
    app.opened=true;app.runtime.location.book_id=77;app.runtime.reading_ms=2000;app.accounted_ms=0;
    feature_account();CHECK(app.records.total_ms==2000 && app.records.hour_ms[8]==1000 && app.records.hour_ms[9]==1000);
    at.tm_mday=22;at.tm_hour=0;host_wall_time=mktime(&at);app.runtime.reading_ms+=2000;
    feature_account();CHECK(app.records.hour_date==20261022 && app.records.hour_ms[0]==1000 && stats_day_duration(20261021)==3000);
    app.opened=opened;app.runtime.location=loc;app.runtime.reading_ms=reading;app.accounted_ms=accounted;
    app.records=*saved;free(saved);host_wall_time=old_time;app.stats_period=0;
}
/* Metalio frontlight/haptics page under Display; settings apply and persist at once. */
static void frontlight_controls_test(void) {
    navigate(THINGSOS_UI_DISPLAY);app.light_mode=0;app.light_level=40;app.haptic_enabled=true;
    app.force_refresh=false;tap(100,990);CHECK(app.force_refresh && !app.light_page);
    tap(500,990);CHECK(app.page==THINGSOS_UI_DISPLAY && app.light_page);snapshot("frontlight");
    tap(560,350);CHECK(app.light_mode==3 && host_light_mode==3);
    tap(380,350);CHECK(app.light_mode==1 && host_light_mode==1);
    tap(260,350);CHECK(app.light_mode==0 && host_light_mode==0);
    tap(380,350);
    tap(560,480);CHECK(app.light_level==41 && host_light_level==41);
    for(unsigned i=0;i<40;++i)tap(260,480);
    CHECK(app.light_level==1);
    tap(300,610);CHECK(!app.haptic_enabled);
    tap(600,610);CHECK(app.haptic_enabled);
    tap(300,1060);CHECK(app.page==THINGSOS_UI_DISPLAY && !app.light_page);
    tap(500,990);CHECK(app.light_page);
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(app.page==THINGSOS_UI_DISPLAY && !app.light_page);
    tap(500,990);CHECK(app.light_page);tap(60,150);CHECK(app.page==THINGSOS_UI_DISPLAY && !app.light_page);
    tap(500,990);CHECK(app.light_page);navigate(THINGSOS_UI_SETTINGS);CHECK(!app.light_page);
    /* Volume keys page like bezel keys and pulse the motor when enabled. */
    host_haptic_ms=0;capacitive_key(THINGSOS_KEY_NEXT);CHECK(host_haptic_ms==50);
    app.haptic_enabled=false;host_haptic_ms=0;capacitive_key(THINGSOS_KEY_NEXT);CHECK(!host_haptic_ms);
    app.haptic_enabled=true;app.light_mode=0;app.light_level=40;
}
static void usb_controls_test(void) {
    navigate(THINGSOS_UI_SETTINGS);tap(100,800);
    CHECK(app.page==THINGSOS_UI_USB);snapshot("usb-off");
    tap(300,900);CHECK(usb_begin_requested);usb_begin_requested=false;
    host_usb=(thingsos_usb_status_t){.active=true,.connected=true};
    snapshot("usb-connected");
    /* Windows can remove the disk without reporting SCSI eject. The explicit
       close switch must queue shutdown even with the last connected state. */
    tap(300,900);CHECK(usb_end_requested);usb_end_requested=false;
    tap(60,150);CHECK(app.page==THINGSOS_UI_USB);
    host_usb.ejected=true;snapshot("usb-ejected");
    tap(300,900);CHECK(usb_end_requested);usb_end_requested=false;
    memset(&host_usb,0,sizeof(host_usb));tap(300,1100);
    CHECK(app.page==THINGSOS_UI_SETTINGS);
    tap(100,875);CHECK(app.page==THINGSOS_UI_UPGRADE);
    navigate(THINGSOS_UI_SETTINGS);tap(100,950);CHECK(app.page==THINGSOS_UI_RECOVERY);
    navigate(THINGSOS_UI_SETTINGS);tap(100,1020);CHECK(app.page==THINGSOS_UI_LOGS);
    snapshot("settings-logs");tap(100,1040);CHECK(app.page==THINGSOS_UI_SETTINGS);
    navigate(THINGSOS_UI_RECOVERY);
    navigate(THINGSOS_UI_LIBRARY);app.message[0]=0;
}
static void file_picture_test(void) {
    bool sd=host_sd;host_sd=true;strlcpy(host_picture_root,fixture_root,sizeof(host_picture_root));
    thingsos_browser_t* saved=malloc(sizeof(*saved));CHECK(saved);*saved=app.browser;
    navigate(THINGSOS_UI_FILES);memset(&app.browser,0,sizeof(app.browser));
    strcpy(app.browser.path,"/sdcard/wallpapers");app.browser.count=5;app.list_start=3;
    strcpy(app.browser.entries[3].name,"portrait.png");strcpy(app.browser.entries[4].name,"picture.jpg");
    thingsos_location_t before=app.runtime.location;
    tap(180,425);CHECK(file_picture.open && file_picture.width==684 && file_picture.height==1216);
    CHECK(!strcmp(app.browser.path,"/sdcard/wallpapers") && app.list_start==3);
    snapshot("file-image-portrait");tap(40,140);CHECK(!file_picture.open && !file_picture.pixels && app.list_start==3);
    tap(180,500);CHECK(file_picture.open && file_picture.width==240);snapshot("file-image-jpeg");
    tap(300,1110);CHECK(!file_picture.open && app.page==THINGSOS_UI_FILES && app.list_start==3);
    CHECK(file_picture_open("/sdcard/wallpapers/landscape.png"));snapshot("file-image-landscape");
    navigate(THINGSOS_UI_LIBRARY);CHECK(!file_picture.open && !file_picture.pixels);
    CHECK(!memcmp(&before,&app.runtime.location,sizeof(before)));
    CHECK(!file_picture_open("/sdcard/wallpapers/bad.png") && !file_picture.pixels);
    CHECK(!file_picture_path("/sdcard/../bad.png") && !file_picture_path("/sdcard/fonts/a.ttf"));
    app.browser=*saved;free(saved);app.list_start=0;host_sd=sd;app.message[0]=0;
}
static void transfer_refresh_test(void){
    thingsos_transfer_state_t saved=app.transfer_displayed;
    unsigned serial=app.transfer_serial,progress=app.transfer_progress;
    app.transfer_displayed=(thingsos_transfer_state_t){0};app.transfer_serial=app.transfer_progress=0;
    thingsos_transfer_state_t s={.serial=1,.active=true};
    strcpy(s.name,"speed-probe.mp3");strcpy(s.message,"preparing");
    CHECK(transfer_refresh_due(&s,0,0));transfer_refresh_record(&s);
    CHECK(!transfer_refresh_due(&s,100000,0));
    ++s.serial;strcpy(s.message,"receiving");
    CHECK(transfer_refresh_due(&s,1,0));transfer_refresh_record(&s);
    /* Repeated serial changes and long elapsed time within each 20% band
       must not repaint. Observe exactly 20,40,60,80,100 after the start. */
    unsigned draws=0;uint64_t time=1,refreshed=1;
    for(unsigned percent=0;percent<=100;++percent){
        s.percent=percent;
        for(unsigned repeat=0;repeat<5;++repeat){
            ++s.serial;time+=3000;
            bool due=transfer_refresh_due(&s,time,refreshed);
            CHECK(due==(percent>0&&percent%20==0&&repeat==0));
            if(due){++draws;transfer_refresh_record(&s);refreshed=time;}
        }
    }
    CHECK(draws==5);
    /* Finish, failure and cancellation are not held until a time/percent gate. */
    ++s.serial;s.active=false;strcpy(s.message,"complete");
    CHECK(transfer_refresh_due(&s,time,refreshed));transfer_refresh_record(&s);
    ++s.serial;s.active=true;s.percent=0;strcpy(s.message,"receiving");
    CHECK(transfer_refresh_due(&s,time,refreshed));transfer_refresh_record(&s);
    ++s.serial;s.percent=7;s.active=false;
    CHECK(transfer_refresh_due(&s,time,refreshed));transfer_refresh_record(&s);
    ++s.serial;s.failed=true;strcpy(s.message,"interrupted");
    CHECK(transfer_refresh_due(&s,time,refreshed));transfer_refresh_record(&s);
    ++s.serial;strcpy(s.name,"next.mp3");
    CHECK(transfer_refresh_due(&s,time,refreshed));transfer_refresh_record(&s);
    ++s.serial;s.percent=0;
    CHECK(transfer_refresh_due(&s,time,refreshed));transfer_refresh_record(&s);
    /* OTA progress waits four seconds; stage changes above remain immediate. */
    ++s.serial;s.ota=true;s.active=true;s.failed=false;
    CHECK(transfer_refresh_due(&s,time,refreshed));transfer_refresh_record(&s);
    ++s.serial;s.percent=1;
    CHECK(!transfer_refresh_due(&s,refreshed+4000,refreshed));
    CHECK(transfer_refresh_due(&s,refreshed+4001,refreshed));
    s.serial=0;CHECK(!transfer_refresh_due(&s,refreshed+10000,refreshed));
    app.transfer_displayed=saved;app.transfer_serial=serial;app.transfer_progress=progress;
}
static void online_transfer_test(void) {
    transfer_refresh_test();
    uint64_t saved_activity=app.activity_at;thingsos_runtime_t saved_runtime=app.runtime;
    app.activity_at=100;
    thingsos_transfer_state_t timing={.active=true};transfer_sync_activity(&timing,1000);
    CHECK(app.activity_at==1000);
    timing.active=false;timing.finished_ms=1500;transfer_sync_activity(&timing,90000);
    CHECK(app.activity_at==1500); /* Server finish, not UI observation time. */
    transfer_sync_activity(&timing,100000);CHECK(app.activity_at==1500);
    app.activity_at=2000;transfer_sync_activity(&timing,110000);CHECK(app.activity_at==2000);
    timing.active=true;transfer_sync_activity(&timing,3000);CHECK(app.activity_at==3000);
    timing.active=false;timing.failed=true;timing.finished_ms=3500;
    transfer_sync_activity(&timing,120000);CHECK(app.activity_at==3500);
    CHECK(!lock_due(3500+(uint64_t)app.auto_lock_minutes*60000));
    CHECK(lock_due(3501+(uint64_t)app.auto_lock_minutes*60000));
    app.activity_at=saved_activity;app.runtime=saved_runtime;
    app.wifi_enabled=true; /* Normal saved-preference default. */
    unsigned automatic_checks=host_release_checks;
    navigate(THINGSOS_UI_TRANSFER);CHECK(app.service_on&&host_http_requested);
    CHECK(lock_due(app.activity_at+(uint64_t)app.auto_lock_minutes*60000+1));
    app.service_on=false;CHECK(lock_due(app.activity_at+(uint64_t)app.auto_lock_minutes*60000+1));
    app.service_on=true;
    host_network_connected=false;app.wifi_connected=false;
    app.hotspot=true;app.service_on=true;snapshot("transfer-hotspot-images");
    host_network_connected=true;network_connection_poll();CHECK(!app.hotspot && !host_hotspot && app.wifi_connected);
    char address[80];thingsos_network_address(app.hotspot,address,sizeof(address));CHECK(!strcmp(address,"http://192.168.1.42"));
    snapshot("transfer-wifi-images");
    host_network_connected=false;network_connection_poll();thingsos_network_address(app.hotspot,address,sizeof(address));CHECK(app.hotspot && host_hotspot && !strcmp(address,"http://192.168.4.1"));
    snapshot("transfer-disconnected");
    navigate(THINGSOS_UI_UPGRADE);CHECK(!app.service_on&&!host_http_requested && !app.hotspot && !host_hotspot);
    host_network_connected=true;
    CHECK(host_release_checks==automatic_checks); /* Entering pages and connecting never checks. */
    unsigned checks=host_release_checks,installs=host_release_installs;
    tap(100,350);CHECK(host_release_checks==checks+1);
    host_network_busy=true;host_release_checking=true;CHECK(!transfer_blocks_ui());
    host_release_checking=false;CHECK(transfer_blocks_ui());host_network_busy=false;
    navigate(THINGSOS_UI_LIBRARY);app.dirty=false;++host_release.serial;upgrade_poll();CHECK(!app.dirty);
    navigate(THINGSOS_UI_UPGRADE);app.dirty=false;upgrade_poll();CHECK(app.dirty);
    host_release.checked=host_release.available=true;strcpy(host_release.release.version,"0.4.0");
    strcpy(host_release.release.notes,"Added online updates\nAdded image upload\nImproved USB storage");
    snapshot("upgrade-online-available");
    for(unsigned i=0;i<8;++i)strcat(host_release.release.notes,"\nAdditional firmware changes");
    draw();CHECK(upgrade_notes_pages>1);tap(500,700);CHECK(upgrade_notes_page==1);
    snapshot("upgrade-online-notes-page2");tap(100,700);CHECK(!upgrade_notes_page);
    tap(300,775);CHECK(app.confirm_action==8 && host_release_installs==installs);
    snapshot("upgrade-online-confirm");tap(100,1100);CHECK(!app.confirm_action && host_release_installs==installs);
    tap(300,775);tap(500,1100);CHECK(host_release_installs==installs+1);
    memset(&host_release,0,sizeof(host_release));host_network_connected=false;
    navigate(THINGSOS_UI_LIBRARY);app.message[0]=0;
}
static void shared_qwerty_test(void) {
    thingsos_ui_page_t saved=app.page;
    CHECK(keyboard_typeface() && keyboard_font.monochrome);
    CHECK(keyboard_font.signature!=app.font.signature);
    app.page=THINGSOS_UI_PASSWORD;app.editing=1;app.wifi_symbols_more=false;
    thingsos_input_open(&app.input,"",63,true);snapshot("keyboard-wifi-unified");
    unsigned char* common=malloc(684u*316);CHECK(common);
    memcpy(common,app.canvas.pixels+754u*684,684u*316);
    app.editing=5;thingsos_input_open(&app.input,"",96,false);snapshot("keyboard-search-unified");
    for(unsigned y=754;y<1070;++y) {
        unsigned width=y<1000?684:532; /* Only the action label changes from connect to search. */
        CHECK(!memcmp(common+(y-754)*684,app.canvas.pixels+(size_t)y*684,width));
    }
    /* Repeated alphabet/number/more switches must rebuild the exact target,
       with no old glyphs or keycaps left in the software frame. Hardware DU transitions
       are verified separately on the device. */
    unsigned char* keyboard_target=malloc(684u*316);CHECK(keyboard_target);
    memcpy(keyboard_target,app.canvas.pixels+754u*684,684u*316);
    for(unsigned cycle=0;cycle<12;++cycle) {
        tap(60,1030);CHECK(app.input.mode==THINGSOS_KEYS_SYMBOLS && app.keyboard_refresh);draw();
        tap(160,1030);CHECK(app.keyboard_refresh);draw();
        tap(60,1030);CHECK(app.input.mode==THINGSOS_KEYS_LOWER && app.keyboard_refresh);draw();
        CHECK(!memcmp(keyboard_target,app.canvas.pixels+754u*684,684u*316));
    }
    free(keyboard_target);
    free(common);
    char long_name[97];memset(long_name,'W',96);long_name[96]=0;
    CHECK(thingsos_input_open(&app.input,long_name,96,false));
    memset(app.canvas.pixels,255,684u*1216u);feature_input_layout(true,0,0);
    CHECK(!region_ink(420,508)); /* Long search text remains inside the input box. */
    feature_input_layout(false,50,320);CHECK(app.input.cursor<96);
    thingsos_input_open(&app.input,"",96,false);
    bool printable[128]={0};printable[' ']=printable[',']=true;
    for(unsigned mode=0;mode<4;++mode) {
        app.input.mode=mode==0?THINGSOS_KEYS_LOWER:mode==1?THINGSOS_KEYS_UPPER:THINGSOS_KEYS_SYMBOLS;
        app.wifi_symbols_more=mode==3;
        draw();
        canvas_snapshot(mode==0?"keyboard-lower":mode==1?"keyboard-upper":mode==2?"keyboard-symbols":"keyboard-more");
        for(unsigned y=746;y<1076;++y)for(unsigned x=28;x<656;++x) {
            unsigned char pixel=app.canvas.pixels[(size_t)y*684+x];
            CHECK(pixel==0 || pixel==255);
        }
        input_key_t keys[40];size_t count=input_keyboard_keys(keys);CHECK(count<=40);
        for(size_t i=0;i<count;++i) {
            CHECK(wifi_key_hit(keys[i].x+keys[i].w/2,keys[i].y+34)==keys[i].code);
            /* Replay physical coordinates for every key, in all four layouts.
               The sensor transform belongs before hit testing, once only. */
            int raw_x=((keys[i].x+keys[i].w/2-29)*1000+448)/896;
            int raw_y=keys[i].y+34+18;
            keyboard_touch_coordinates(&raw_x,&raw_y);
            CHECK(wifi_key_hit(raw_x,raw_y)==keys[i].code);
            CHECK(keys[i].x>=28 && keys[i].x+keys[i].w<=656 && keys[i].y+68<=1076);
            if(keys[i].code>=100)printable[keys[i].code-100]=true;
            for(size_t j=0;j<i;++j)CHECK(keys[i].x>=keys[j].x+keys[j].w || keys[j].x>=keys[i].x+keys[i].w || keys[i].y>=keys[j].y+68 || keys[j].y>=keys[i].y+68);
        }
        /* Exhaustive ownership across caps and seams in every layout, rather
           than assuming symbols/uppercase share the lowercase arrangement. */
        for(int y=746;y<1076;++y)for(int x=28;x<656;++x) {
            int hit=wifi_key_hit(x,y);
            CHECK(hit>=0);
            for(size_t i=0;i<count;++i)
                if(x>=keys[i].x && x<keys[i].x+keys[i].w && y>=keys[i].y && y<keys[i].y+68)
                    CHECK(hit==keys[i].code);
        }
    }
    for(unsigned c=32;c<=126;++c)CHECK(printable[c]); /* WiFi passwords retain all printable ASCII. */
    app.input.mode=THINGSOS_KEYS_LOWER;app.wifi_symbols_more=false;
    /* User-labelled centre taps captured on COM5, not generated from the
       calibration formula. Both early O taps used to be interpreted as P. */
    static const struct {int x,y,key;} measured[]={
        {582,803,100+'o'},{590,791,100+'o'},{576,806,100+'o'},
        {655,791,100+'p'},{632,819,100+'p'},{647,810,100+'p'},
        {41,804,100+'q'},{315,812,100+'t'},{659,795,100+'p'},
        {61,901,100+'a'},{354,891,100+'g'},{615,888,100+'l'},
        {126,969,100+'z'},{550,961,100+'m'},{335,1055,42},{620,971,43}
    };
    CHECK(wifi_key_hit(582,803)==100+'p');
    for(size_t i=0;i<sizeof(measured)/sizeof(*measured);++i) {
        int x=measured[i].x,y=measured[i].y;
        keyboard_touch_coordinates(&x,&y);
        CHECK(wifi_key_hit(x,y)==measured[i].key);
    }
    for(unsigned surface=0;surface<3;++surface) {
        app.page=surface==0?THINGSOS_UI_READING:THINGSOS_UI_PASSWORD;
        app.editing=surface==1?4:5;
        int x=582,y=surface==2?350:803;
        keyboard_touch_coordinates(&x,&y);
        CHECK(x==582 && y==(surface==2?350:803));
    }
    app.page=THINGSOS_UI_PASSWORD;app.editing=5;
    /* Every painted keycap is 68 px tall on all four rows. The hit box must be
       that same rectangle: a tap in the blank strip under the bottom row (or in
       the 20 px margin left of "q") types nothing, instead of typing a key that
       is not painted under the finger. */
    CHECK(wifi_key_hit(60,1085)==-1);          /* below the bottom keycaps */
    CHECK(wifi_key_hit(60,1075)==41);          /* last drawn row of "123" */
    CHECK(wifi_key_hit(60,1105)==-1);          /* label row below the keyboard */
    CHECK(wifi_key_hit(300,735)==-1);          /* candidate row above it */
    CHECK(wifi_key_hit(300,745)==-1);          /* the seam above row one */
    CHECK(wifi_key_hit(300,746)==100+'t');     /* first drawn row of row one */
    CHECK(wifi_key_hit(20,780)==-1);           /* inside the keyboard margin */
    CHECK(wifi_key_hit(660,780)==-1);          /* right of the right-hand key */
    CHECK(wifi_key_hit(360,1085)==-1);         /* no key under the space gap */
    /* Painted, not nearest-centre: a tap 1 px inside an edge belongs to that
       key even when a wider neighbour's centre is nearer. Before this a tap on
       the last 8 px of the space bar typed "中 / 英", and a tap on the first
       pixel of "中 / 英" typed a space, which is what "the wrong adjacent key
       fires" looked like on the device. */
    CHECK(wifi_key_hit(439,1030)==42 && wifi_key_hit(440,1030)==42);
    CHECK(wifi_key_hit(443,1030)==42 && wifi_key_hit(444,1030)==49);
    CHECK(wifi_key_hit(447,1030)==49 && wifi_key_hit(448,1030)==49);
    CHECK(wifi_key_hit(523,1030)==49 && wifi_key_hit(527,1030)==49);
    CHECK(wifi_key_hit(528,1030)==48 && wifi_key_hit(532,1030)==48);
    CHECK(wifi_key_hit(125,1030)==41 && wifi_key_hit(134,1030)==50);
    /* The 6 px column seams and 14 px row seams stay live and resolve to the
       nearer painted key, so no tap is swallowed. */
    CHECK(wifi_key_hit(94,780)==100+'w');      /* column seam between q and w */
    CHECK(wifi_key_hit(88,780)==100+'q' && wifi_key_hit(93,780)==100+'q');
    CHECK(wifi_key_hit(130,1030)==50);         /* between "123" and "," */
    CHECK(wifi_key_hit(66,780)==100+'q');      /* left inset of row one */
    CHECK(wifi_key_hit(44,895)==100+'a' && wifi_key_hit(35,895)==100+'a');
    CHECK(wifi_key_hit(100,940)==40); /* Shift keeps its own target. */
    /* Row seams resolve inside one row, so a tap just above the action row does
       not fire the action key and one just below a letter row does not either. */
    CHECK(wifi_key_hit(60,911)==40 && wifi_key_hit(60,923)==40);
    CHECK(wifi_key_hit(200,992)==100+'x');     /* nearest row is the letter row */
    CHECK(wifi_key_hit(200,1000)==42);         /* first drawn row of the space bar */
    CHECK(wifi_key_hit(118,940)==100+'z');     /* right of Shift belongs to z */
    CHECK(wifi_key_hit(569,940)==43);          /* right of m belongs to delete */
    for(int y=746;y<1076;++y)for(int x=28;x<656;++x) {
        int key=wifi_key_hit(x,y);
        if(!(key>=100 || key==40 || key==41 || key==42 || key==43 || key==48 || key==49 || key==50))
            fprintf(stderr,"keyboard gap at (%d,%d) key=%d mode=%d\n",x,y,key,(int)app.input.mode);
        CHECK(key>=100 || key==40 || key==41 || key==42 || key==43 || key==48 || key==49 || key==50);
        /* A tap that lands on a painted keycap always selects that keycap: the
           seam fallback may never override the key the user visibly touched. */
        input_key_t inside[40];size_t inside_count=input_keyboard_keys(inside);int painted=-1;
        for(size_t i=0;i<inside_count;++i) {
            int top=input_key_row_top(input_key_row(&inside[i]));
            if(x>=inside[i].x && x<inside[i].x+inside[i].w && y>=top && y<top+INPUT_KEY_HEIGHT)
                painted=inside[i].code;
        }
        CHECK(painted<0 || painted==key);
    }
    /* Layout switches must ask for a full-pixel refresh: a DU frame leaves the
       previous key outlines between the new keys. */
    app.input.mode=THINGSOS_KEYS_LOWER;app.wifi_symbols_more=false;
    app.transition_refresh=false;tap(80,1030);
    CHECK(app.input.mode==THINGSOS_KEYS_SYMBOLS && app.transition_refresh);
    app.transition_refresh=false;tap(160,1030);
    CHECK(app.wifi_symbols_more && app.transition_refresh);
    app.transition_refresh=false;tap(80,1030);
    CHECK(app.input.mode==THINGSOS_KEYS_LOWER && app.transition_refresh);
    tap(100,650);CHECK(!app.input.chinese); /* Former language control is inert. */
    app.transition_refresh=false;
    CHECK(keyboard_mode_press(480,1030) && app.input.chinese && app.keyboard_refresh &&
          app.transition_refresh);
    tap(145,950);tap(395,860);tap(550,780);tap(450,950);tap(330,860);
    CHECK(!strcmp(app.input.composition,"zhong") && !app.input.length);
    tap(480,1030);CHECK(app.input.chinese && !strcmp(app.input.composition,"zhong"));
    snapshot("keyboard-search-pinyin");
    /* Input-only rendering must match a complete frame, preserve static
       controls and include the enlarged candidates and paging arrows. */
    app.keyboard_refresh=app.input_refresh=app.force_refresh=app.transition_refresh=false;
    draw();
    CHECK(keyboard_mode_press(60,780));CHECK(!strcmp(app.input.composition,"zhongq"));
    draw();
    unsigned char* partial=malloc(684u*1216u);CHECK(partial);
    memcpy(partial,app.canvas.pixels,684u*1216u);
    app.keyboard_refresh=app.input_refresh=false;draw();
    if(memcmp(partial,app.canvas.pixels,684u*1216u)) {
        unsigned first=0,last=0,count=0;for(unsigned n=0;n<684u*1216u;++n)if(partial[n]!=app.canvas.pixels[n]){if(!count)first=n;last=n;++count;}
        fprintf(stderr,"Keyboard diff %u pixels first=(%u,%u) last=(%u,%u)\n",count,first%684,first/684,last%684,last/684);
    }
    CHECK(!memcmp(partial,app.canvas.pixels,684u*1216u));free(partial);
    CHECK(keyboard_mode_press(610,950));CHECK(!strcmp(app.input.composition,"zhong"));
    CHECK(keyboard_mode_press(615,686));
    CHECK(keyboard_mode_press(545,686));
    tap(70,730);
    CHECK(!app.input.composition[0] && app.input.length);
    size_t chinese_bytes=app.input.length;
    tap(160,1030);CHECK(app.input.length==chinese_bytes+3);
    tap(610,950);CHECK(app.input.length==chinese_bytes);
    tap(480,1030);CHECK(!app.input.chinese && app.input.length==chinese_bytes);
    tap(60,950);CHECK(app.input.mode==THINGSOS_KEYS_UPPER);
    tap(60,780);CHECK(app.input.text[app.input.length-1]=='Q');
    tap(60,1030);CHECK(app.input.mode==THINGSOS_KEYS_SYMBOLS);
    tap(160,1030);CHECK(app.wifi_symbols_more);snapshot("keyboard-search-symbols");
    CHECK(thingsos_input_open(&app.input,"",96,false));app.input.chinese=true;
    const char* word_input="zhongguo";for(const char* p=word_input;*p;++p)CHECK(thingsos_ime_letter(&app.input,*p));
    snapshot("keyboard-word-candidates");tap(70,730);CHECK(!strcmp(app.input.text,"中国") && !app.input.composition[0]);
    char associated[THINGSOS_IME_CANDIDATE_BYTES];CHECK(thingsos_ime_candidate(&app.input,0,associated) && !strncmp(associated,"中国",6) && strlen(associated)>6);
    snapshot("keyboard-word-continuations");tap(70,730);CHECK(!strcmp(app.input.text,associated));
    CHECK(thingsos_input_open(&app.input,"中国阅读",96,false));app.input.chinese=true;draw();
    thingsos_input_cursor(&app.input,1);app.input_refresh=true;draw();
    partial=malloc(684u*1216u);CHECK(partial);memcpy(partial,app.canvas.pixels,684u*1216u);
    app.input_refresh=false;draw();CHECK(!memcmp(partial,app.canvas.pixels,684u*1216u));free(partial);
    /* The candidate band must keep clear air between its last painted row and
       the white rectangle that repaints the keyboard at y=746. At the largest
       UI scale the row above used to finish 9 px above that edge, so a taller
       glyph or a one-pixel baseline drift put the bottom of 好像/好吃/好好 on
       the keyboard's erase edge and the device showed the top half only. */
    unsigned saved_ui=app.ui_px;
    for(unsigned scale=26;scale<=50;scale+=2) {
        app.ui_px=scale;
        app.font.ui_size_percent=(app.ui_px?app.ui_px:30)*100/30;
        CHECK(thingsos_input_open(&app.input,"",96,false));app.input.chinese=true;
        for(const char* p="zhongguo";*p;++p)CHECK(thingsos_ime_letter(&app.input,*p));
        draw();
        unsigned last_candidate_row=0,first_key_row=0;
        for(unsigned y=638;y<746;++y)for(unsigned x=36;x<648;++x)
            if(app.canvas.pixels[(size_t)y*684+x]<128)last_candidate_row=y;
        for(unsigned y=746;y<1076;++y)for(unsigned x=28;x<656;++x)
            if(app.canvas.pixels[(size_t)y*684+x]<128){if(!first_key_row)first_key_row=y;}
        /* Ink stays inside the candidate band and above the keyboard. */
        CHECK(last_candidate_row>0 && last_candidate_row<INPUT_KEY_TOP-8);
        CHECK(first_key_row>=INPUT_KEY_TOP);
        /* Every candidate on the page is painted at one type size chosen so that
           the whole row fits: no column silently drops its last characters, and
           a long word no longer shrinks alone. */
        /* Full drawing finishes with the key labels, whose scale can be larger
           than the bounded candidate band. Measure the actual candidate choice. */
        char measured_words[THINGSOS_IME_PAGE_SIZE][THINGSOS_IME_CANDIDATE_BYTES];size_t measured_count=0;
        for(size_t i=0;i<THINGSOS_IME_PAGE_SIZE;++i)if(thingsos_ime_candidate(&app.input,i,measured_words[measured_count]))++measured_count;
        unsigned row_size=input_candidate_size(measured_words,measured_count);
        CHECK(row_size>=18 && row_size<=28);
        for(unsigned i=0;i<3;++i) {
            char word[THINGSOS_IME_CANDIDATE_BYTES];
            if(!thingsos_ime_candidate(&app.input,i,word))continue;
            int advance=0;
            for(const char* p=word;*p;)advance+=thingsos_glyph(&keyboard_font,NULL,input_utf8_next(&p),0,0);
            CHECK(advance<=INPUT_CANDIDATE_WIDTH);
            int column_ink=0;
            for(unsigned y=638;y<746;++y)for(unsigned x=36+i*204;x<36+i*204+196;++x)
                column_ink+=app.canvas.pixels[(size_t)y*684+x]<128;
            CHECK(column_ink>0);
        }
    }
    app.ui_px=saved_ui;app.font.ui_size_percent=(app.ui_px?app.ui_px:30)*100/30;
    /* Exact reported sequence: empty Chinese hint -> h -> ha -> hao. Each
       incremental canvas must equal a fresh frame, including the hint's bottom
       rows; panel erase strength is tested separately from this software check. */
    CHECK(thingsos_input_open(&app.input,"",96,false));app.input.chinese=true;
    snapshot("keyboard-empty-pinyin-hint");
    partial=malloc(684u*1216u);CHECK(partial);
    static const int hao_raw[3][2]={{427,883},{67,897},{576,806}};
    for(unsigned i=0;i<3;++i) {
        int x=hao_raw[i][0],y=hao_raw[i][1];
        keyboard_touch_coordinates(&x,&y);CHECK(keyboard_mode_press(x,y));
        CHECK(strlen(app.input.composition)==i+1 && !strncmp(app.input.composition,"hao",i+1));
        draw();
        memcpy(partial,app.canvas.pixels,684u*1216u);
        app.input_refresh=app.keyboard_refresh=false;draw();
        CHECK(!memcmp(partial,app.canvas.pixels,684u*1216u));
    }
    canvas_snapshot("keyboard-hao-no-hint");free(partial);
    /* Candidate paging and commit share one rectangle with the drawing code, so
       a candidate that is painted is always the candidate a tap commits, and no
       row of candidate glyphs sits outside its own hit band. */
    CHECK(input_hit_candidate(36,706) && input_hit_candidate(35,706)==false);
    CHECK(input_hit_candidate(36+3*204-1,731) && input_hit_candidate(36+3*204,731)==false);
    CHECK(input_hit_candidate(70,699)==false);     /* above the hit band */
    CHECK(input_hit_candidate(70,700));            /* first row of the band */
    CHECK(input_hit_candidate(70,746)==false);      /* the keyboard's first row */
    CHECK(input_candidate_index(70)==0 && input_candidate_index(274)==1 && input_candidate_index(478)==2);
    CHECK(input_hit_page(520,680) && !input_hit_page(580,680) && input_hit_page(600,680));
    CHECK(input_hit_page(520,666) && input_hit_page(520,703));
    CHECK(!input_hit_page(520,665) && !input_hit_page(520,704));
    app.input_return=saved;app.page=saved;app.editing=0;
    memset(&app.input,0,sizeof(app.input));app.message[0]=0;
    app.keyboard_refresh=app.input_refresh=false;
}

static void reader_annotation_test(void) {
    unsigned px=app.px,scale=app.font.ui_size_percent;
    static const unsigned sizes[]={20,30,48};
    for(unsigned i=0;i<3;++i) {
        app.px=sizes[i];app.font.ui_size_percent=170;
        reader_annotation_label("Annotation",36,100,612);
        CHECK(app.font.size==app.px-2 && app.font.ui_size_percent==170);
        size_t measured=reader_annotation_page(NULL,"Text wraps in this window",25,0,36,0,200,120,145);
        CHECK(measured>0 && app.font.size==app.px-2 && app.font.ui_size_percent==170);
    }
    app.px=px;app.font.ui_size_percent=scale;
}
static void reader_background_test(void) {
    app.page=THINGSOS_UI_READING;app.dirty=false;app.reader_background_refresh=false;
    app.wifi_connected=false;host_network_connected=true;
    network_connection_poll();
    CHECK(app.dirty && app.reader_background_refresh && app.wifi_connected);
    /* A queued foreground frame is never downgraded by another completion. */
    app.reader_background_refresh=false;
    host_network_connected=false;network_connection_poll();
    CHECK(app.dirty && !app.reader_background_refresh);
    app.dirty=false;background_redraw();
    CHECK(app.reader_background_refresh);
    capacitive_key(THINGSOS_KEY_MENU);CHECK(!app.reader_background_refresh);
    app.menu=false;app.dirty=false;background_redraw();
    next_page(true);CHECK(!app.reader_background_refresh);
    app.dirty=false;background_redraw();navigate(THINGSOS_UI_LIBRARY);
    CHECK(!app.reader_background_refresh);
    app.dirty=false;background_redraw();CHECK(app.dirty && !app.reader_background_refresh);
}
static void weread_reading_ui_test(void){
    release_book();host_network_connected=false;host_sd=true;host_nvs_available=true;
    app.wd_pull_on=app.wd_push_on=app.wd_time_on=true;app.wd_time_confirmed=false;
    app.page=THINGSOS_UI_READING;app.opened=true;strcpy(app.read_path,"/sdcard/books/微读-55.epub");
    app.chapter.text=_strdup("Text");app.chapter.bytes=4;app.start=0;
    wd_opened();CHECK(app.wd_active&&app.wd_consent);snapshot("weread-time-consent");
    unsigned requests=host_weidu_calls;size_t dialog_position=app.start;next_page(true);CHECK(app.start==dialog_position);
    capacitive_key(THINGSOS_KEY_MENU);CHECK(!app.wd_time_on&&!app.wd_time_confirmed&&!app.wd_consent);
    CHECK(host_weidu_calls==requests);
    app.wd_status.duration_seconds=169;app.wd_status.duration_at=1792281600;
    app.wd_sync_panel=true;app.page=THINGSOS_UI_WEIDU;snapshot("weread-sync-panel");
    thingsos_wd_diagnostic_t saved_diag=app.wd_status.diagnostic;
    thingsos_wd_diagnostic_t* diag=&app.wd_status.diagnostic;
    *diag=(thingsos_wd_diagnostic_t){.error=WD_NETWORK,.action=WD_DEVICE_UPLOAD,.phase=4,.http_status=503};
    strcpy(diag->stage,"POST");char diagnostic_code[128];
    CHECK(wd_sync_diagnostic_text(diagnostic_code,sizeof(diagnostic_code)) && strstr(diagnostic_code,"/POST P4") && strstr(diagnostic_code,"H503"));
    CHECK(strstr(wd_sync_failure_hint(diag),"服务端"));snapshot("weread-sync-http-failure");
    diag->http_status=0;diag->phase=2;CHECK(strstr(wd_sync_failure_hint(diag),"连接服务器"));
    diag->error=WD_DNS;CHECK(strstr(wd_sync_failure_hint(diag),"域名"));snapshot("weread-sync-dns-failure");
    diag->error=WD_TLS;diag->tls_error=0x2700;diag->tls_flags=8;
    CHECK(strstr(wd_sync_failure_hint(diag),"证书"));snapshot("weread-sync-tls-failure");
    diag->error=WD_OK;CHECK(!wd_sync_diagnostic_text(diagnostic_code,sizeof(diagnostic_code)) && !diagnostic_code[0]);
    *diag=saved_diag;
    thingsos_weidu_status_t before_sync=host_weidu;
    thingsos_weidu_action_t previous_action=app.weidu_action;
    bool previous_sd=host_sd,previous_network=host_network_connected;
    host_sd=host_network_connected=true;host_weidu.busy=app.weidu.busy=false;
    tap(424,278);CHECK(host_weidu_calls==requests+1 && weidu_sync_running());
    tap(399,274);CHECK(host_weidu_calls==requests+1 && weidu_sync_running() && !app.weidu_deferred);
    host_weidu.busy=false;host_weidu.phase=WD_READY;++host_weidu.serial;weidu_poll();
    CHECK(!weidu_sync_running());
    tap(424,278);CHECK(host_weidu_calls==requests+2 && weidu_sync_running());
    host_weidu=app.weidu=before_sync;app.weidu_action=previous_action;host_weidu_calls=requests;
    host_sd=previous_sd;host_network_connected=previous_network;
    tap(300,845);CHECK(app.wd_consent);tap(500,875);CHECK(app.wd_time_on&&app.wd_time_confirmed);
    bool saved_pull=app.wd_pull_on,saved_push=app.wd_push_on;
    app.wd_pull_on=false;app.wd_push_on=false;CHECK(preferences_save());
    app.wd_pull_on=app.wd_push_on=true;app.wd_time_on=app.wd_time_confirmed=false;host_nvs_read=true;preferences_load();
    CHECK(!app.wd_pull_on&&!app.wd_push_on&&app.wd_time_on&&app.wd_time_confirmed);
    app.wd_pull_on=saved_pull;app.wd_push_on=saved_push;app.wd_sync_panel=false;
    app.page=THINGSOS_UI_READING;app.dirty=false;app.reader_ready_at=0;
    wd_poll();CHECK(host_weidu_calls==requests); /* No automatic work before presentation. */
    app.reader_ready_at=now();app.activity_at=now();
    host_clock_us+=1900000;wd_poll();CHECK(host_weidu_calls==requests);
    host_clock_us+=200000;wd_poll();CHECK(!app.wd_pull_pending&&host_weidu_calls==requests);
    app.wd_sync_panel=true;app.dirty=false;host_wd_reading.serial=app.wd_serial+1;
    wd_poll();CHECK(!app.dirty);app.wd_sync_panel=false;
    host_wd_notes_available=false;wd_notes_enter("55",true);snapshot("weread-notes-empty");CHECK(app.wd_notes_open);
    tap(100,140);CHECK(!app.wd_notes_open);
    app.page=THINGSOS_UI_WEIDU;app.wd_notes_open=app.wd_long_open=true;
    thingsos_wd_note_t private_test_note={0};private_test_note.range=true;private_test_note.begin=3;private_test_note.length=4;
    strcpy(private_test_note.uid,"1");strcpy(private_test_note.text,"Text");
    wd_own_thought_enter("66",&private_test_note);CHECK(app.annotation_open && app.annotation_remote && !strcmp(app.annotation_remote_id,"66"));
    snapshot("weread-own-thought-library");tap(80,145);
    CHECK(!app.annotation_open && app.wd_notes_open && app.wd_long_open);
    app.wd_notes_open=app.wd_long_open=false;app.page=THINGSOS_UI_READING;
    host_wd_notes_available=true;wd_notes_enter("55",true);snapshot("weread-notes-list");
    CHECK(app.wd_info.count[0]==3);tap(100,400);CHECK(app.wd_long_open);snapshot("weread-notes-detail");
    tap(300,945);CHECK(app.wd_notes_open); /* no TOC: never guess a chapter */
    tap(100,140);CHECK(!app.wd_notes_open);
    /* Cached highlights and a later notes completion must not insert an
       unsolicited body scan between opening/turning pages. They are included
       in the next requested frame, with the page cache invalidated. */
    bool saved_epub_open=app.epub_open;app.epub_open=true;host_wd_mapped_lines=true;
    for(unsigned completion=0;completion<2;++completion) {
        wd_forget_map();app.dirty=false;app.reader_background_refresh=false;
        wd_prepare();
        CHECK(!app.dirty && !app.reader_background_refresh);
        CHECK(app.chapter.underline_count==1 && app.chapter.underlines==app.wd_lines);
        snapshot("weread-deferred-highlights");CHECK(app.text_hits.line_count==1);
    }
    wd_forget_map();app.dirty=true;app.reader_background_refresh=false;wd_prepare();
    CHECK(app.dirty && !app.reader_background_refresh); /* Preserve a queued user frame. */
    wd_forget_map();host_wd_mapped_lines=false;app.epub_open=saved_epub_open;
    /* Visible popular highlights have row-local hit boxes, and own the tap. */
    app.wd_lines[0]=(thingsos_text_underline_t){0,4,false};app.wd_line_count=1;
    app.chapter.underlines=app.wd_lines;app.chapter.underline_count=1;reader_cache_clear();
    unsigned saved_thought_px=app.px;app.px=30;
    host_thought_cache=true;host_thought_line=true;app.menu=false;
    snapshot("weread-clickable-highlight");CHECK(app.text_hits.line_count==1);
    /* Model a full-width highlighted row at the bottom of the reading area:
       central text opens thoughts; both lower side zones retain page turning. */
    thingsos_text_hit_t line_hit=app.text_hits.lines[0];line_hit.x=36;line_hit.w=612;line_hit.y=1000;line_hit.h=45;
    app.text_hits.lines[0]=line_hit;size_t original_position=app.start;
    unsigned thought_requests=host_thought_requests;
    tap(100,1020);CHECK(!app.wd_thought_open && host_thought_requests==thought_requests);
    app.start=original_position;app.text_hits_start=app.start;
    tap(600,1020);CHECK(!app.wd_thought_open && host_thought_requests==thought_requests);
    app.start=original_position;app.text_hits_start=app.start;
    snapshot("weread-before-thought-modal");
    unsigned char* reading_background=malloc(684u*1216u);CHECK(reading_background);
    memcpy(reading_background,app.canvas.pixels,684u*1216u);
    app.text_hits.lines[0]=line_hit;app.text_hits.line_count=1;app.text_hits_start=app.start;app.text_hits_chapter=app.chapter_index;
    tap(342,1020);
    CHECK(app.wd_thought_open && app.wd_thought_cached && app.start==original_position && host_thought_requests==thought_requests);
    next_page(true);CHECK(app.start==original_position);snapshot("weread-highlight-thoughts");
    CHECK(!memcmp(reading_background,app.canvas.pixels,684u*170u));
    CHECK(!memcmp(reading_background+684u*1086u,app.canvas.pixels+684u*1086u,684u*130u));
    free(reading_background);
    tap(300,610);CHECK(app.wd_thought_full);snapshot("weread-highlight-thought-full");
    tap(100,230);CHECK(!app.wd_thought_full);tap(500,1020);CHECK(app.wd_thought_row==2);
    tap(500,1040);CHECK(app.wd_thought_page==1 && app.wd_thought_query.max_idx==10);
    tap(100,1040);CHECK(app.wd_thought_page==0 && app.wd_thought_row==2);
    tap(600,230);CHECK(!app.wd_thought_open && app.start==original_position);
    app.wd_thought_open=true;app.wd_thought_row=0;app.px=48;
    snapshot("weread-highlight-thoughts-large");CHECK(ui_font()->size==24);tap(500,1020);CHECK(app.wd_thought_row==2);
    host_thought_long=true;tap(300,610);CHECK(app.wd_thought_full);
    snapshot("weread-highlight-thought-full-large");CHECK(app.wd_long_end>0 && app.wd_long_end<strlen(app.wd_note.text));
    size_t second_thought_page=app.wd_long_end;tap(500,1020);snapshot("weread-highlight-thought-full-next");
    CHECK(app.wd_long_page==second_thought_page);tap(100,1020);CHECK(app.wd_long_page==0);
    tap(100,230);CHECK(!app.wd_thought_full && app.wd_thought_row==2);
    tap(10,600);CHECK(!app.wd_thought_open && app.start==original_position);
    host_thought_long=false;app.px=saved_thought_px;
    host_thought_cache=false;host_network_connected=false;
    app.text_hits.lines[0]=line_hit;app.text_hits.line_count=1;app.text_hits_start=app.start;app.text_hits_chapter=app.chapter_index;
    tap(342,1020);CHECK(app.wd_thought_open && !app.wd_thought_cached && !app.wd_thought_loading);
    app.wd_thought_error=WD_AUTH;CHECK(strstr(wd_thought_empty_hint(),"登录") && !strstr(wd_thought_empty_hint(),"联网加载"));
    snapshot("weread-highlight-thoughts-auth");app.wd_thought_error=WD_NETWORK;
    snapshot("weread-highlight-thoughts-offline");capacitive_key(THINGSOS_KEY_MENU);CHECK(!app.wd_thought_open);
    app.chapter.underlines=NULL;app.chapter.underline_count=app.wd_line_count=0;host_thought_line=false;
    /* Exit reports floor(seconds), then no second upload for duplicate exit. */
    app.chapter.cover_image=1;app.start=0;thingsos_wd_upload_t cover_position;
    CHECK(wd_position(&cover_position) && cover_position.offset==0);app.chapter.cover_image=0;
    app.wd_map.source=calloc(4,sizeof(uint32_t));CHECK(app.wd_map.source);
    app.wd_map.source[0]=0;app.wd_map.source[1]=3;app.wd_map.source[2]=4;app.wd_map.source[3]=7;
    app.wd_map.count=app.wd_map.capacity=2;app.wd_map.bytes=4;app.wd_map.text=app.chapter.text;app.wd_map_attempted=true;
    app.wd_started=app.wd_accounted=100;app.runtime.reading_ms=18099;app.wd_active=true;host_network_connected=true;host_weidu.busy=false;
    app.dirty=false;app.wd_enter_pending=true;app.wd_init_pending=false;app.wd_pull_pending=false;app.wd_was_online=true;
    app.wd_enter_attempts=0;app.wd_enter_retry_at=0;
    app.wd_mapped_chapter=app.chapter_index;host_wd_reading.serial=app.wd_serial;
    app.reader_ready_at=now();app.activity_at=now();unsigned quiet_requests=host_weidu_calls;
    wd_poll();CHECK(app.wd_enter_pending && host_weidu_calls==quiet_requests);
    host_clock_us+=2100000;
    wd_poll();CHECK(host_weidu_action==WD_DEVICE_ENTER && !host_wd_upload.report && !host_wd_upload.seconds);
    CHECK(app.wd_enter_attempts==1 && !app.wd_enter_pending);
    strcpy(host_wd_reading.book,app.wd_id);strcpy(host_wd_reading.owner,app.wd_owner);
    host_wd_reading.action=WD_DEVICE_ENTER;host_wd_reading.session_ready=false;
    host_wd_reading.progress=WD_NETWORK;host_weidu.busy=false;++host_wd_reading.serial;
    quiet_requests=host_weidu_calls;wd_poll();
    CHECK(app.wd_enter_pending && host_weidu_calls==quiet_requests);
    host_clock_us+=4999000;wd_poll();CHECK(host_weidu_calls==quiet_requests);
    host_clock_us+=1000;wd_poll();CHECK(host_weidu_calls==quiet_requests+1 && app.wd_enter_attempts==2);
    CHECK(!host_wd_upload.report && !host_wd_upload.seconds);
    host_weidu.busy=false;host_wd_reading.progress=WD_DNS;++host_wd_reading.serial;wd_poll();
    quiet_requests=host_weidu_calls;host_clock_us+=15000000;wd_poll();
    CHECK(host_weidu_calls==quiet_requests+1 && app.wd_enter_attempts==3);
    host_weidu.busy=false;host_wd_reading.progress=WD_NETWORK;++host_wd_reading.serial;wd_poll();
    quiet_requests=host_weidu_calls;host_clock_us+=60000000;wd_poll();
    CHECK(!app.wd_enter_pending && host_weidu_calls==quiet_requests);
    host_weidu.busy=false;
    app.wd_status.session_ready=true;strcpy(app.wd_status.book,app.wd_id);
    app.wd_accounted=app.runtime.reading_ms;app.runtime.reading_ms+=31000;app.wd_report_after=0;app.dirty=false;
    wd_poll();CHECK(host_weidu_action==WD_DEVICE_REPORT && host_wd_upload.report && host_wd_upload.seconds==31);
    CHECK(app.wd_accounted==app.runtime.reading_ms);host_weidu.busy=false;app.runtime.reading_ms+=17000;
    unsigned before_exit=host_weidu_calls;
    wd_finish();CHECK(host_wd_scheduled && host_weidu_calls==before_exit);
    thingsos_wd_device_poll(false);CHECK(host_weidu_calls==before_exit);
    thingsos_wd_device_poll(true);
    CHECK(host_weidu_action==WD_DEVICE_UPLOAD&&host_wd_upload.seconds==17&&host_wd_upload.report);
    unsigned after_exit=host_weidu_calls;wd_finish();CHECK(host_weidu_calls==after_exit);
    /* A normal power-key exit must upload once before WiFi is suspended. */
    unsigned saved_suspend=host_suspend,saved_resume=host_resume,saved_off=host_off;
    app.wd_active=true;app.asleep=false;host_weidu.busy=false;
    power_action(1);CHECK(!app.asleep && app.wd_power_action==1 && host_suspend==saved_suspend);
    reading_power_poll();CHECK(host_weidu.busy && app.wd_power_stage==2 && host_wd_upload.seconds==17 && host_wd_upload.report);
    unsigned power_requests=host_weidu_calls;reading_power_poll();CHECK(host_weidu_calls==power_requests && !app.asleep);
    host_weidu.busy=false;reading_power_poll();CHECK(app.asleep && !app.wd_power_action && host_suspend==saved_suspend+1);
    power_action(1);CHECK(!app.asleep && app.wd_active);
    app.runtime.reading_ms+=3000;host_weidu.busy=false;power_action(2);reading_power_poll();
    CHECK(host_weidu.busy && host_wd_upload.seconds==3 && app.wd_power_action==2);
    app.wd_power_deadline=0;reading_power_poll();CHECK(app.asleep && !app.wd_power_action && host_off==saved_off+1);
    CHECK(host_weidu_calls==power_requests+1); /* timeout never replays duration */
    app.asleep=false;app.wd_active=false;host_weidu.busy=false;
    app.wd_active=true;host_weidu.busy=true;
    unsigned busy_requests=host_weidu_calls;power_action(1);reading_power_poll();
    CHECK(app.wd_power_stage==1 && host_weidu_calls==busy_requests && !app.asleep);
    power_action(1);CHECK(!app.wd_power_action && app.wd_active && !app.asleep);
    app.wd_active=false;host_weidu.busy=false;
    host_suspend=saved_suspend;host_resume=saved_resume;host_off=saved_off;
    app.wd_active=true;app.wd_pull_pending=true;app.wd_cloud_prompt=false;app.dirty=true;
    host_wd_reading.action=WD_DEVICE_PULL;host_wd_reading.pull=WD_OK;host_wd_reading.cloud.valid=true;
    strcpy(host_wd_reading.book,app.wd_id);strcpy(host_wd_reading.owner,app.wd_owner);
    strcpy(host_wd_reading.cloud.uid,"different-chapter");host_wd_reading.cloud.offset=999;
    host_wd_reading.serial=app.wd_serial+1;size_t local_anchor=app.start;
    wd_poll();CHECK(!app.wd_cloud_prompt&&!app.wd_pull_pending&&app.start==local_anchor);
    /* Multi-device sync: offline->online pull re-arm, prompt & ENTER gating, Confirm/Cancel, queued conflict. */
    {
        time_t saved_sync_time=host_wall_time;
        bool saved_sync_wifi=host_network_connected;
        bool saved_time_conf=app.wd_time_confirmed;
        bool saved_rt_on=app.wd_rt_on;
        thingsos_wd_device_status_t saved_reading=host_wd_reading;
        host_wd_sync_reset();
        strcpy(host_wd_reading.owner,"0123456789abcdef0123456789abcdef");
        strcpy(app.wd_owner,host_wd_reading.owner);
        host_wall_time=1792282000;
        app.wd_time_confirmed=true;app.wd_consent=false;app.wd_rt_on=false;
        app.page=THINGSOS_UI_READING;app.opened=true;app.dirty=false;
        app.start=1;app.chapter_index=0;
        strcpy(app.read_path,"/sdcard/books/微读-55.epub");
        wd_opened();
        app.reader_ready_at=now();app.activity_at=now();
        host_clock_us+=2100000;
        /* Offline poll does not issue network PULL; turning Wi-Fi on re-arms and pulls. */
        host_network_connected=false;unsigned c_before=host_weidu_calls;wd_poll();
        CHECK(!app.wd_pull_pending && host_weidu_calls==c_before);
        host_network_connected=true;host_weidu.busy=false;
        wd_poll();
        CHECK(host_weidu_calls==c_before+1 && host_weidu_action==WD_DEVICE_PULL && app.wd_enter_pending);
        /* Pull returns newer phone position -> raises prompt & holds ENTER until user responds. */
        host_wd_cloud_chapter=0;host_wd_cloud_at=2;
        host_weidu.busy=false;
        strcpy(host_wd_reading.book,"55");
        host_wd_reading.action=WD_DEVICE_PULL;host_wd_reading.pull=WD_OK;host_wd_reading.cloud.valid=true;
        strcpy(host_wd_reading.cloud.uid,"ch2");host_wd_reading.cloud.offset=220;
        host_wd_reading.cloud.percent=38;host_wd_reading.cloud.updated=1792282500;
        ++host_wd_reading.serial;
        wd_poll();
        CHECK(app.wd_cloud_prompt && app.wd_enter_pending);
        draw(); /* Verify cloud prompt draws cleanly without error. */
        host_clock_us+=2100000;unsigned held=host_weidu_calls;wd_poll();
        CHECK(host_weidu_calls==held && app.wd_enter_pending); /* Still gated while prompt is open. */
        tap(500,875); /* Confirm: jumps to cloud target & releases ENTER. */
        CHECK(!app.wd_cloud_prompt && app.start==2);
        thingsos_wd_sync_t sync_check={0};
        CHECK(thingsos_wd_device_sync_load("55",app.wd_owner,&sync_check) && sync_check.local_updated>=1792282500);
        draw();app.dirty=false;host_clock_us+=2100000;
        wd_poll();CHECK(host_weidu_action==WD_DEVICE_ENTER && !app.wd_enter_pending);
        /* Cancel keeps local position, dismisses timestamp, and suppresses immediate stale exit upload. */
        host_weidu.busy=false;host_wd_reading.cloud.updated=1792282600;host_wd_reading.cloud.offset=350;
        app.wd_pull_pending=true;++host_wd_reading.serial;host_wd_reading.action=WD_DEVICE_PULL;
        wd_poll();CHECK(app.wd_cloud_prompt);
        tap(150,875); /* Cancel */
        CHECK(!app.wd_cloud_prompt && app.start==2);
        host_wd_scheduled=false;wd_finish();CHECK(!host_wd_scheduled);
        /* Reading further offline creates newer local update and schedules upload on exit. */
        host_wall_time=1792282700;host_weidu.busy=false;app.dirty=false;
        wd_opened();app.start=3;app.activity_at=now();app.reader_ready_at=now();host_clock_us+=2100000;
        wd_poll();CHECK(app.wd_session_moved);
        wd_finish();CHECK(host_wd_scheduled);
        CHECK(thingsos_wd_device_sync_load("55",app.wd_owner,&sync_check) && sync_check.local_updated==1792282700);
        /* Queue drain protects even newer phone progress instead of overwriting cloud. */
        host_weidu.busy=false;
        host_wd_queue_conflict=true;
        host_wd_queue_cloud.valid=true;strcpy(host_wd_queue_cloud.uid,"ch8");
        host_wd_queue_cloud.offset=900;host_wd_queue_cloud.percent=72;host_wd_queue_cloud.updated=1792283000;
        unsigned before_drain=host_weidu_calls;
        thingsos_wd_device_poll(true);
        CHECK(!host_wd_scheduled && host_weidu_calls==before_drain && host_wd_reading.conflict);
        /* Library/WeRead shelf and other books never pop progress sync prompts. */
        app.page=THINGSOS_UI_LIBRARY;app.opened=false;app.wd_active=false;
        wd_poll();CHECK(!app.wd_cloud_prompt);
        app.page=THINGSOS_UI_WEIDU;
        wd_poll();CHECK(!app.wd_cloud_prompt);
        app.page=THINGSOS_UI_READING;app.opened=true;strcpy(app.read_path,"/sdcard/books/微读-66.epub");
        wd_opened();wd_poll();CHECK(!app.wd_cloud_prompt);
        /* Opening the matching book pops the saved cloud progress prompt cleanly. */
        strcpy(app.read_path,"/sdcard/books/微读-55.epub");app.start=1;app.chapter_index=0;
        wd_opened();
        CHECK(app.wd_cloud_prompt && app.wd_cloud_progress.updated==1792283000);
        app.wd_cloud_prompt=false;
        host_wd_sync_reset();host_wd_cloud_chapter=-1;host_wd_cloud_at=0;
        host_wall_time=saved_sync_time;host_network_connected=saved_sync_wifi;
        app.wd_time_confirmed=saved_time_conf;app.wd_rt_on=saved_rt_on;host_wd_reading=saved_reading;
    }
    app.wd_active=true;app.wd_last_valid=true;strcpy(app.wd_owner,"old");
    host_wd_reading.serial=++app.wd_serial;host_wd_reading.serial++;strcpy(host_wd_reading.owner,"new");
    app.wd_notes_open=true;wd_poll();CHECK(!app.wd_last_valid&&!app.wd_notes_open&&!app.wd_map.source);
    app.wd_active=false;release_book();host_wd_notes_available=false;host_network_connected=false;host_weidu.busy=false;
    app.wd_sync_panel=app.wd_consent=app.wd_cloud_prompt=false;host_nvs_available=host_nvs_read=false;
}
static void fast_font_comparison(void) {
    thingsos_font_t font={0};CHECK(thingsos_font_file(&font,host_font));
    thingsos_canvas_t original=app.canvas;
    thingsos_canvas_t canvas={malloc(684u*1216u),684,1216};CHECK(canvas.pixels);
    static const char* const titles[]={
        "Gray glyph + threshold 128","Native mono hinting",
        "Glyph-local coverage diffusion","Gray coverage reference"};
    static const char* const names[]={"fast-font-threshold","fast-font-native-mono",
        "fast-font-dither","fast-font-gray-reference"};
    for(unsigned mode=0;mode<4;++mode) {
        memset(canvas.pixels,255,684u*1216u);
        font.monochrome=mode==1;font.dither=mode==2;
        thingsos_font_size(&font,24);thingsos_font_weight(&font,0);
        thingsos_label(&font,&canvas,titles[mode],28,48,628);
        for(unsigned size=0;size<3;++size)for(unsigned weight=0;weight<3;++weight) {
            int y=110+(int)(size*3+weight)*120;char info[64];
            snprintf(info,sizeof(info),"%u px / weight %u",24+size*4,weight);
            thingsos_font_size(&font,18);thingsos_font_weight(&font,0);thingsos_label(&font,&canvas,info,28,y-26,628);
            thingsos_font_size(&font,24+size*4);thingsos_font_weight(&font,weight);
            thingsos_label(&font,&canvas,"人入八水永然，细笔画、斜线与标点。",28,y,628);
            thingsos_label(&font,&canvas,"ABC abc 123 / \\ () ,.;: 混排边缘",28,y+38,628);
        }
        CHECK(!font.errors);
        if(mode==0)for(size_t i=0;i<684u*1216u;++i)canvas.pixels[i]=canvas.pixels[i]<128?0:255;
        else if(mode<3)for(size_t i=0;i<684u*1216u;++i)CHECK(canvas.pixels[i]==0 || canvas.pixels[i]==255);
        app.canvas=canvas;canvas_snapshot(names[mode]);app.canvas=original;
    }
    font.dither=false;
    free(canvas.pixels);thingsos_font_close(&font);
}
static void physical_back_test(void) {
    size_t position=app.start;
    CHECK(app.page==THINGSOS_UI_DISPLAY);tap(100,1060);CHECK(app.page==THINGSOS_UI_DISPLAY);
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(app.page==THINGSOS_UI_SETTINGS);
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(app.page==THINGSOS_UI_SETTINGS);
    navigate(THINGSOS_UI_READER_MORE);tap(100,225);CHECK(app.page==THINGSOS_UI_READER_MORE);
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(app.page==THINGSOS_UI_READING && app.menu);
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(app.page==THINGSOS_UI_READING && !app.menu && app.start==position);
    app.footnote_open=true;capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(!app.footnote_open && app.start==position);
    app.runtime.location.byte_offset=(uint32_t)position;thingsos_image_open(&app.image_view,app.runtime.location);
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(!app.image_view.open && app.start==position);
    app.annotation_selecting=true;capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(!app.annotation_selecting && app.start==position);
    app.input_return=THINGSOS_UI_READER_MORE;thingsos_input_open(&app.input,"discard",32,false);navigate(THINGSOS_UI_PASSWORD);
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(app.page==THINGSOS_UI_READER_MORE && !app.input.text[0]);
    navigate(THINGSOS_UI_MANAGE);app.manage_return=THINGSOS_UI_LIBRARY;app.delete_confirm=true;app.manage_single_delete=false;
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(app.page==THINGSOS_UI_MANAGE && !app.delete_confirm);
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(app.page==THINGSOS_UI_LIBRARY);
    navigate(THINGSOS_UI_FILES);app.file_more=true;
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(app.page==THINGSOS_UI_FILES && !app.file_more);
    navigate(THINGSOS_UI_READING);app.menu=false;
}
static void reader_refresh_config_test(void) {
    reader_annotations_dismiss();app.landscape_enabled=false;reader_canvas_geometry();
    app.ui_refresh_mode=2;app.ui_refresh_pages=20;app.refresh_mode=0;app.refresh_pages=15;
    host_nvs_available=true;host_nvs_commit_fail=false;CHECK(preferences_save());
    app.pagination_started=false;CHECK(pagination_prepare());uint64_t gray_key=app.page_cache_key;
    size_t position=app.start;
    bool wd_active=app.wd_active;app.wd_active=true;
    navigate(THINGSOS_UI_READER_MORE);tap(100,225);
    CHECK(app.page==THINGSOS_UI_READER_MORE && app.wd_active);app.wd_active=wd_active;
    snapshot("reader-refresh-balanced");
    host_nvs_commit_fail=true;tap(480,260);CHECK(!app.refresh_mode && app.ui_refresh_mode==2);
    host_nvs_commit_fail=false;tap(480,260);
    CHECK(app.refresh_mode==1 && app.ui_refresh_mode==2 && app.refresh_pages==15 && app.ui_refresh_pages==20);
    CHECK(app.start==position && !app.pagination_started);CHECK(pagination_prepare() && app.page_cache_key!=gray_key);
    snapshot("reader-refresh-fast");CHECK(app.font.dither);
    tap(360,380);CHECK(app.refresh_pages==5 && app.ui_refresh_pages==20);
    tap(520,380);CHECK(app.refresh_pages==15);
    app.landscape_enabled=true;reader_canvas_geometry();snapshot("reader-refresh-landscape");
    tap(550,250);CHECK(app.refresh_mode==2 && app.ui_refresh_mode==2);
    tap(1150,250);CHECK(app.refresh_pages==20 && app.ui_refresh_pages==20);
    tap(300,250);CHECK(!app.refresh_mode);
    app.landscape_enabled=false;reader_canvas_geometry();CHECK(pagination_prepare() && app.page_cache_key==gray_key);
    tap(480,260);tap(520,380);CHECK(app.refresh_mode==1 && app.refresh_pages==15);
    app.ui_refresh_mode=app.refresh_mode=0;app.ui_refresh_pages=app.refresh_pages=5;
    host_nvs_read=true;preferences_load();host_nvs_read=false;
    CHECK(app.ui_refresh_mode==2 && app.refresh_mode==1 && app.ui_refresh_pages==20 && app.refresh_pages==15);
    tap(100,225);CHECK(app.page==THINGSOS_UI_READER_MORE);
    capacitive_key(THINGSOS_KEY_MENU);CHECK(app.page==THINGSOS_UI_READING && app.menu && app.start==position);
    app.menu=app.jump=app.image_view.open=false;app.image_hit_count=0;host_network_busy=false;
    app.force_refresh=app.transition_refresh=false;
    app.ripple_enabled=true;app.ripple_pending=true;CHECK(!reader_wave_requested());
    app.refresh_mode=0;app.ripple_pending=true;CHECK(reader_wave_requested());app.refresh_mode=1;
    snapshot("reader-fast-body");
    navigate(THINGSOS_UI_DISPLAY);tap(438,610);snapshot("display-fast-native");
    for(unsigned y=322;y<356;++y)for(unsigned x=48;x<180;++x) {
        unsigned char p=app.canvas.pixels[(size_t)y*684+x];CHECK(p==0 || p==255);
    }
    navigate(THINGSOS_UI_DISPLAY);tap(298,610);
    CHECK(!app.ui_refresh_mode && app.refresh_mode==1);snapshot("display-independent-refresh");
}
#include "vertical_ui_test.inc"
#include "lock_pin_ui_test.inc"
#include "toc_current_ui_test.inc"
#include "txt_source_ui_test.inc"
#include "reader_swipe_ui_test.inc"
#include "reader_image_hold_ui_test.inc"
static void reader_immersive_test(void) {
    release_book();host_sd=true;app.book_count=1;memset(&app.books[0],0,sizeof(app.books[0]));
    app.books[0].id=0x1aa1cc55;strcpy(app.books[0].name,"沉浸模式测试");
    snprintf(app.books[0].path,sizeof(app.books[0].path),"%s/immersive.txt",fixture_root);
    FILE* text=fopen(app.books[0].path,"wb");CHECK(text);
    for(unsigned i=0;i<4000;++i)fputs("山川与日月，文字与时光。",text);
    CHECK(!fclose(text));CHECK(open_book(0));
    reader_annotations_dismiss();app.menu=app.jump=app.footnote_open=app.image_view.open=false;
    app.wd_id[0]=0;app.immersive_enabled=false;app.landscape_enabled=false;
    host_nvs_available=true;host_nvs_commit_fail=false;host_nvs_read=true;
    host_immersive_present=false;preferences_load();CHECK(!app.immersive_enabled);
    host_immersive_present=true;host_saved_immersive=99;preferences_load();CHECK(!app.immersive_enabled);
    host_saved_immersive=0;host_nvs_read=false;
    for(unsigned wide=0;wide<2;++wide) {
        app.landscape_enabled=wide!=0;app.immersive_enabled=false;reader_canvas_geometry();
        CHECK(reader_body_top()==90 && reader_body_height()==reader_height()-176);
        navigate(THINGSOS_UI_READING);CHECK(pagination_prepare());uint64_t regular_key=app.page_cache_key;
        size_t position=app.start;
        app.menu=true;app.controls_refresh=app.force_refresh=app.transition_refresh=false;
        tap(wide?1150:620,wide?110:490);CHECK(!app.menu&&!app.controls_refresh&&!app.force_refresh&&!app.transition_refresh);
        app.menu=true;app.controls_refresh=false;capacitive_key(THINGSOS_KEY_MENU);CHECK(!app.menu&&!app.controls_refresh&&!app.transition_refresh);
        navigate(THINGSOS_UI_READER_MORE);
        int x=reader_more_row_left(3)+reader_more_row_width()-40,y=reader_more_row_top(3)+30;
        host_nvs_commit_fail=true;tap(x,y);CHECK(!app.immersive_enabled);host_nvs_commit_fail=false;
        tap(x,y);CHECK(app.immersive_enabled && host_saved_immersive==1 && app.start==position);
        CHECK(!app.pagination_started && !app.page_cache_key);
        CHECK(reader_body_top()==12 && reader_body_height()==reader_height()-24);
        CHECK(reader_more_toggle_hit(x,y)==3);
        snapshot(wide?"reader-more-immersive-landscape":"reader-more-immersive");
        host_nvs_read=true;app.immersive_enabled=false;preferences_load();host_nvs_read=false;
        CHECK(app.immersive_enabled);
        capacitive_key(THINGSOS_KEY_MENU);app.menu=false;draw();
        CHECK(pagination_prepare() && app.page_cache_key!=regular_key && app.start==position);
        CHECK(app.text_hits.glyph_count && app.text_hits.glyphs[0].y<74);
        snapshot(wide?"reading-immersive-landscape":"reading-immersive");
        reader_cache_invalidate();CHECK(reader_cache_build(0,(thingsos_page_position_t){app.chapter_index,app.start}));
        CHECK(reader_frames[0].valid);
        memset(app.canvas.pixels,37,READER_CACHE_BYTES);CHECK(reader_cache_restore());
        size_t begin=(size_t)reader_body_top()*reader_width(),bytes=(size_t)reader_body_height()*reader_width();
        CHECK(!memcmp(app.canvas.pixels+begin,reader_frames[0].pixels+begin,bytes));
        CHECK(app.canvas.pixels[begin-1]==37 && app.canvas.pixels[begin+bytes]==37);
        draw_status();CHECK(app.canvas.pixels[0]==37); /* Hidden chrome must not overwrite expanded text. */
        draw();size_t before=app.start;tap(reader_width()-10,reader_height()-40);
        CHECK(app.start>before);
        draw();before=app.start;tap(reader_width()-10,35);
        CHECK(app.start>before);
        navigate(THINGSOS_UI_READER_MORE);x=reader_more_row_left(3)+reader_more_row_width()-140;y=reader_more_row_top(3)+30;
        before=app.start;tap(x,y);CHECK(!app.immersive_enabled && app.start==before && !host_saved_immersive);
        CHECK(pagination_prepare() && app.page_cache_key==regular_key);
        capacitive_key(THINGSOS_KEY_MENU);app.menu=false;
        snapshot(wide?"reading-chrome-restored-landscape":"reading-chrome-restored");
    }
    strcpy(app.wd_id,"immersive-cloud-layout");
    app.landscape_enabled=false;reader_canvas_geometry();navigate(THINGSOS_UI_READER_MORE);
    snapshot("reader-more-immersive-cloud");CHECK(reader_more_note_top()+66<reader_height());
    CHECK(reader_more_back_left()>36 && reader_more_back_top()==reader_more_note_top());
    app.landscape_enabled=true;reader_canvas_geometry();snapshot("reader-more-immersive-cloud-landscape");
    CHECK(reader_more_row_top(3)+94<=reader_more_note_top());
    app.wd_id[0]=0;app.landscape_enabled=false;reader_canvas_geometry();release_book();
}

#include "alarm_ui_test.inc"
static void charging_status_test(void) {
    unsigned saved_battery=app.battery;
    bool saved_charging=app.charging;
    uint64_t saved_checked=app.charge_checked,saved_clock=app.clock_refreshed;
    thingsos_ui_page_t saved_page=app.page;
    app.page=THINGSOS_UI_LIBRARY;
    app.charging=host_charging=false;app.charge_checked=app.clock_refreshed=0;
    CHECK(!status_refresh_due(4999));
    host_charging=true;
    CHECK(status_refresh_due(5000) && app.charging && app.clock_refreshed==5000);
    CHECK(!status_refresh_due(10000)); /* Stable charging does not flash every poll. */
    host_charging=false;
    CHECK(status_refresh_due(15000) && !app.charging && app.clock_refreshed==15000);
    CHECK(!status_refresh_due(74999));
    CHECK(status_refresh_due(75000)); /* Normal minute cadence remains. */
    unsigned char* plain=malloc((size_t)684*74);CHECK(plain);
    static const unsigned levels[]={0,15,58,90,100};
    for(unsigned i=0;i<sizeof(levels)/sizeof(levels[0]);++i) {
        app.battery=levels[i];app.charging=false;draw_status();
        memcpy(plain,app.canvas.pixels,(size_t)684*74);
        app.charging=true;draw_status();
        unsigned changes=0,lighter=0,darker=0;
        for(int y=0;y<74;++y)for(int x=0;x<684;++x) {
            unsigned a=plain[(size_t)y*684+x],b=app.canvas.pixels[(size_t)y*684+x];
            if(a==b)continue;
            CHECK(x>=638 && x<652 && y>=27 && y<44);
            ++changes;if(b>a)++lighter;else ++darker;
        }
        CHECK(changes>60);
        if(levels[i]==0)CHECK(darker>60 && lighter==0);
        if(levels[i]==58)CHECK(lighter>0 && darker>0);
        if(levels[i]==100)CHECK(lighter>60);
        char name[80];snprintf(name,sizeof(name),"charging-internal-%u",levels[i]);canvas_snapshot(name);
    }
    /* Landscape is exactly the same battery translated to the right edge. */
    unsigned char portrait[54*23];
    for(int y=0;y<23;++y)memcpy(portrait+y*54,app.canvas.pixels+(size_t)(24+y)*684+620,54);
    app.canvas.width=1216;app.canvas.height=684;draw_status();
    for(int y=0;y<23;++y)
        CHECK(!memcmp(portrait+y*54,app.canvas.pixels+(size_t)(24+y)*1216+1152,54));
    canvas_snapshot("charging-internal-landscape");
    app.canvas.width=684;app.canvas.height=1216;
    app.charging=false;draw_status();canvas_snapshot("charging-full-idle");
    free(plain);
    app.battery=saved_battery;app.charging=saved_charging;
    app.charge_checked=saved_checked;app.clock_refreshed=saved_clock;app.page=saved_page;
}
#include "readtime_ui_test.inc"
#include "system_monitor_ui_test.inc"
static void audio_refresh_ack_test(void) {
    app_media_ui_t saved_media=app_media;
    thingsos_audio_status_t saved_host=host_audio,saved_audio=app.audio;
    thingsos_ui_page_t saved_page=app.page;
    bool saved_dirty=app.dirty,saved_controls=app.controls_open;
    bool saved_force=app.force_refresh,saved_transition=app.transition_refresh;
    bool saved_feedback=app.feedback_refresh,saved_control_refresh=app.controls_refresh,saved_wave=app.lock_wave_pending;
    char saved_message[sizeof(app.message)];memcpy(saved_message,app.message,sizeof(app.message));
    uint64_t saved_clock=host_clock_us;
    app.page=THINGSOS_UI_MUSIC;app.controls_open=false;
    app.force_refresh=app.transition_refresh=app.feedback_refresh=app.controls_refresh=app.lock_wave_pending=false;
    app.message[0]=0;app_media.painted=false;
    CHECK(!app_audio_region());app_audio_presented(true);CHECK(app_audio_region());
    app.force_refresh=true;CHECK(!app_audio_region());app.force_refresh=false;
    app.transition_refresh=true;CHECK(!app_audio_region());app.transition_refresh=false;
    app_audio_presented(false);CHECK(!app_audio_region());app_audio_presented(true);
    host_audio=(thingsos_audio_status_t){.state=THINGSOS_AUDIO_IDLE,.serial=10000};
    app.audio=host_audio;app_media.serial=host_audio.serial;
    app_media.painted_audio=THINGSOS_AUDIO_IDLE;app_media.painted_controls=false;
    app_media.preview=app_media.painted_preview=app_media.play_start_pending=false;app.dirty=false;
    host_audio_deferred=true;
    app_audio_play_file("/sdcard/music/refresh-test.mp3",false);
    CHECK(!app.dirty && app_media.play_start_pending && ui_static_panel_page());
    app_poll();CHECK(!app.dirty); /* No unchanged old-state frame or quick STARTING frame. */
    ++host_audio.serial;app_poll();CHECK(!app.dirty); /* Worker may publish STARTING twice. */
    host_clock_us+=100000;host_audio.state=THINGSOS_AUDIO_PLAYING;++host_audio.serial;
    app_poll();CHECK(app.dirty && !app_media.play_start_pending && app_audio_region());
    unsigned clean_frames=host_audio_clean_presents;CHECK(app_audio_present()==ESP_OK);
    CHECK(host_audio_clean_presents==clean_frames+1);
    CHECK(host_audio_clean_x==286 && host_audio_clean_y==603 && host_audio_clean_w==112 && host_audio_clean_h==56);
    app_audio_presented(true);
    app.dirty=false;app_poll();CHECK(!app.dirty);
    host_audio_pause_deferred=true;
    app_audio_toggle();CHECK(!app.dirty && app.audio.state==THINGSOS_AUDIO_PLAYING);
    app_poll();CHECK(!app.dirty);
    host_audio.state=THINGSOS_AUDIO_PLAY_PAUSED;++host_audio.serial;
    app_poll();CHECK(app.dirty);
    clean_frames=host_audio_clean_presents;CHECK(app_audio_present()==ESP_OK);
    CHECK(host_audio_clean_presents==clean_frames+1 && host_audio_clean_y==603);
    app_audio_presented(true);app.dirty=false;app_poll();CHECK(!app.dirty);
    /* Recorder preview uses the same acknowledged pause/resume path. */
    app.page=THINGSOS_UI_RECORDER;app_media.preview=true;
    CHECK(!app_audio_region());app_audio_presented(true);CHECK(app_audio_region());
    CHECK(app_tap(300,650) && !app.dirty && ui_static_panel_page());
    host_audio.state=THINGSOS_AUDIO_PLAYING;++host_audio.serial;
    app_poll();CHECK(app.dirty);
    clean_frames=host_audio_clean_presents;CHECK(app_audio_present()==ESP_OK);
    CHECK(host_audio_clean_presents==clean_frames+1 && host_audio_clean_x==286 && host_audio_clean_y==628);
    app_audio_presented(true);app.dirty=false;app_poll();CHECK(!app.dirty);
    /* Music timer-only differences never recondition the stable label. */
    app.page=THINGSOS_UI_MUSIC;app_media.preview=false;app_audio_presented(true);
    /* Playback progress refreshes every 2 s (each scan competes with decode). */
    host_clock_us+=1000000;host_audio.position_ms+=1000;app_poll();CHECK(!app.dirty);
    host_clock_us+=1000000;host_audio.position_ms+=1000;app_poll();CHECK(app.dirty);
    clean_frames=host_audio_clean_presents;unsigned diff_frames=host_audio_diff_presents;
    CHECK(app_audio_present()==ESP_OK && host_audio_clean_presents==clean_frames && host_audio_diff_presents==diff_frames+1);
    app_audio_presented(true);app.dirty=app.audio_progress=false;
    /* Normal SD/I2S startup may exceed a second; avoid an intermediate scan. */
    app_audio_play_file("/sdcard/audio/refresh-test.wav",true);app_poll();CHECK(!app.dirty);
    host_clock_us+=1400000;app_poll();CHECK(!app.dirty);
    host_clock_us+=601000;app_poll();CHECK(app.dirty);
    app.dirty=false;app_poll();CHECK(!app.dirty);
    host_audio.state=THINGSOS_AUDIO_PLAYING;++host_audio.serial;
    app_poll();CHECK(app.dirty);app.dirty=false;
    /* An inactive/paused clock mismatch is not a reason to refresh the panel. */
    app_media.preview=false;host_audio.state=THINGSOS_AUDIO_IDLE;++host_audio.serial;
    app_poll();app.dirty=false;host_audio.position_ms+=1000;
    for(unsigned i=0;i<60;++i){host_clock_us+=1000000;app_poll();CHECK(!app.dirty);}
    app.page=THINGSOS_UI_LIBRARY;CHECK(!ui_static_panel_page());
    app_audio_presented(true);CHECK(!app_audio_region());
    app_media.preview=true;CHECK(ui_static_panel_page() && app_audio_region());
    app_audio_presented(true);app_media.preview=false;CHECK(app_audio_region());
    app_audio_presented(true);CHECK(!app_audio_region());
    host_audio_deferred=host_audio_pause_deferred=false;
    app_media=saved_media;host_audio=saved_host;app.audio=saved_audio;
    app.page=saved_page;app.dirty=saved_dirty;app.controls_open=saved_controls;host_clock_us=saved_clock;
    app.force_refresh=saved_force;app.transition_refresh=saved_transition;
    app.feedback_refresh=saved_feedback;app.controls_refresh=saved_control_refresh;app.lock_wave_pending=saved_wave;
    memcpy(app.message,saved_message,sizeof(app.message));
}
static void cubox_controller_test(const char* fixture_root) {
    bool prev_sd=host_sd;host_sd=true;
    memset(&host_cubox,0,sizeof(host_cubox));
    strlcpy(host_cubox.server,"cubox.pro",sizeof(host_cubox.server));
    strlcpy(host_cubox.folder_name,"全部文件夹",sizeof(host_cubox.folder_name));
    host_cubox_folder_count=0;host_cubox_card_count=0;host_cubox_detail_ok=false;host_cubox_article[0]=0;
    if(cubox_ui){free(cubox_ui);cubox_ui=NULL;}

    /* Recorder / Music enter and exit use transition_refresh (no full flash). */
    navigate(THINGSOS_UI_APPS);
    app.transition_refresh=app.force_refresh=false;
    navigate(THINGSOS_UI_RECORDER);
    const bool recorder_enter_trans=app.transition_refresh, recorder_enter_force=app.force_refresh;
    app.transition_refresh=app.force_refresh=false;
    navigate(THINGSOS_UI_APPS);
    const bool recorder_exit_trans=app.transition_refresh, recorder_exit_force=app.force_refresh;
    CHECK(recorder_enter_trans && !recorder_enter_force && recorder_exit_trans && !recorder_exit_force);

    /* 9th launcher tile opens Cubox welcome screen; enter/exit matches Recorder & Music refresh policy. */
    navigate(THINGSOS_UI_APPS);
    app.transition_refresh=app.force_refresh=false;
    tap(492,670);
    CHECK(app.page==THINGSOS_UI_CUBOX && cubox_ui && cubox_ui->view==CUBOX_VIEW_LIST &&
          app.transition_refresh==recorder_enter_trans && app.force_refresh==recorder_enter_force);
    snapshot("cubox-welcome");
    /* Tapping top-left back arrow on Cubox root exits to Apps, never Settings. */
    tap(50,140);
    CHECK(app.page==THINGSOS_UI_APPS && app.transition_refresh==recorder_exit_trans && app.force_refresh==recorder_exit_force);
    tap(492,670);
    CHECK(app.page==THINGSOS_UI_CUBOX && cubox_ui && cubox_ui->view==CUBOX_VIEW_LIST);
    CHECK(!app_cubox_region());
    app.transition_refresh=app.force_refresh=false;app.message[0]=0;
    app_cubox_presented(true);
    CHECK(app_cubox_region());
    unsigned cubox_metrics=host_metrics_presents;
    CHECK(app_cubox_present()==ESP_OK && host_metrics_presents==cubox_metrics+1);
    app_cubox_presented(true);

    /* Guard fallbacks: toast, force_refresh and controls pause differential mode cleanly. */
    strlcpy(app.message,"notice",sizeof(app.message));CHECK(!app_cubox_region());app.message[0]=0;
    app.force_refresh=true;CHECK(!app_cubox_region());app.force_refresh=false;
    app.controls_open=true;app_cubox_presented(true);CHECK(!app_cubox_region());
    app.controls_open=false;CHECK(!app_cubox_region());
    app_cubox_presented(true);CHECK(app_cubox_region());

    /* Phone pairing shows QR code + pairing code when WiFi is connected. */
    bool prev_wifi=host_network_connected;host_network_connected=true;
    tap(300,560);
    CHECK(cubox_ui->view==CUBOX_VIEW_PAIR && host_cubox.pairing && !app.transition_refresh && app_cubox_region());
    cubox_poll();draw();
    CHECK(app_cubox_present()==ESP_OK && host_metrics_presents==cubox_metrics+2);
    app_cubox_presented(true);
    {
        char link[128];cubox_pair_link(link,sizeof(link));
        CHECK(!strcmp(link,"http://192.168.1.42:8082/"));
        unsigned qr=0,code=0,close_top=0,close_bot=0;
        for(int yy=490;yy<520;++yy)for(int xx=190;xx<500;++xx)qr+=app.canvas.pixels[(size_t)yy*684+xx]<128;
        for(int yy=735;yy<780;++yy)for(int xx=36;xx<648;++xx)code+=app.canvas.pixels[(size_t)yy*684+xx]<128;
        /* Verify "关闭手机配对" button (y=974..1041) draws complete top & bottom borders. */
        for(int xx=40;xx<644;++xx){
            close_top+=app.canvas.pixels[974u*684+xx]<128;
            close_bot+=app.canvas.pixels[1040u*684+xx]<128;
        }
        CHECK(qr>100 && code>0 && close_top>=600 && close_bot>=600);
    }
    /* Header back arrow in pairing subview closes pairing back to list (not Settings), then re-open pairing. */
    tap(50,90);
    CHECK(app.page==THINGSOS_UI_CUBOX && cubox_ui->view==CUBOX_VIEW_LIST && !host_cubox.pairing);
    tap(300,560);
    CHECK(app.page==THINGSOS_UI_CUBOX && cubox_ui->view==CUBOX_VIEW_PAIR && host_cubox.pairing);
    draw();
    /* Idle sleep must not fire while pairing QR is open. */
    {
        bool asleep=app.asleep;uint64_t activity=app.activity_at;app.asleep=false;app.activity_at=0;
        auto_sleep_poll(900000);CHECK(!app.asleep);
        app.asleep=asleep;app.activity_at=activity;
    }

    /* Submitting credentials moves to VERIFYING without restarting on accidental top-button tap. */
    host_cubox.pairing=false;host_cubox.config_phase=THINGSOS_CUBOX_CONFIG_VERIFYING;++host_cubox.serial;
    cubox_poll();draw();
    CHECK(cubox_ui->view==CUBOX_VIEW_PAIR && cubox_ui->status.config_phase==THINGSOS_CUBOX_CONFIG_VERIFYING);
    tap(300,900);
    CHECK(cubox_ui->view==CUBOX_VIEW_PAIR && !host_cubox.pairing && cubox_ui->status.config_phase==THINGSOS_CUBOX_CONFIG_VERIFYING);

    /* Populate folders & cards when pairing succeeds and verify auto-transition to list. */
    host_cubox_folder_count=2;
    strcpy(host_cubox_folders[0].id,"fld_inbox");strcpy(host_cubox_folders[0].name,"Inbox");host_cubox_folders[0].uncategorized=true;
    strcpy(host_cubox_folders[1].id,"fld_tech");strcpy(host_cubox_folders[1].name,"Tech");strcpy(host_cubox_folders[1].nested_name,"Reading / Tech");
    host_cubox_card_count=2;
    strcpy(host_cubox_cards[0].id,"card_1");strcpy(host_cubox_cards[0].title,"First Cubox Article");
    strcpy(host_cubox_cards[0].domain,"example.com");strcpy(host_cubox_cards[0].folder_name,"Reading / Tech");
    strcpy(host_cubox_cards[1].id,"card_2");strcpy(host_cubox_cards[1].title,"Second Card");host_cubox_cards[1].starred=true;
    host_cubox.pairing=false;host_cubox.configured=true;host_cubox.config_phase=THINGSOS_CUBOX_CONFIG_READY;
    ++host_cubox.connection_serial;++host_cubox.serial;
    cubox_poll();draw();
    CHECK(cubox_ui->view==CUBOX_VIEW_LIST && !host_cubox.pairing && !app.transition_refresh && app_cubox_region());
    snapshot("cubox-list");

    /* Filter tabs: All, Starred, Unread stay differential. */
    tap(240,250);CHECK(host_cubox.filter==THINGSOS_CUBOX_FILTER_ALL && !app.transition_refresh && app_cubox_region());
    tap(380,250);CHECK(host_cubox.filter==THINGSOS_CUBOX_FILTER_STARRED && !app.transition_refresh && app_cubox_region());
    tap(100,250);CHECK(host_cubox.filter==THINGSOS_CUBOX_FILTER_UNREAD && !app.transition_refresh && app_cubox_region());

    /* Folder picker selects Reading / Tech then returns to list without transition flash. */
    tap(200,320);CHECK(cubox_ui->view==CUBOX_VIEW_FOLDERS && !app.transition_refresh && app_cubox_region());draw();
    tap(200,312+2*130+40);
    CHECK(cubox_ui->view==CUBOX_VIEW_LIST && !strcmp(host_cubox.folder_id,"fld_tech") && !app.transition_refresh && app_cubox_region());

    /* Tap card 0 -> detail view -> back arrow must return to list (never Settings), then re-open detail. */
    tap(300,400);
    CHECK(cubox_ui->view==CUBOX_VIEW_DETAIL && !strcmp(cubox_ui->active_id,"card_1") && !app.transition_refresh && app_cubox_region());
    snapshot("cubox-detail");
    tap(50,140);
    CHECK(app.page==THINGSOS_UI_CUBOX && cubox_ui->view==CUBOX_VIEW_LIST);
    tap(300,400);
    CHECK(cubox_ui->view==CUBOX_VIEW_DETAIL && !strcmp(cubox_ui->active_id,"card_1"));
    tap(50,80);
    CHECK(app.page==THINGSOS_UI_CUBOX && cubox_ui->view==CUBOX_VIEW_LIST);
    tap(300,400);
    CHECK(cubox_ui->view==CUBOX_VIEW_DETAIL && !strcmp(cubox_ui->active_id,"card_1"));
    tap(100,900);CHECK(host_cubox_cards[0].read);
    tap(300,900);CHECK(host_cubox_cards[0].starred);

    /* Prepare real local Markdown + image fixture and simulate download completion. */
    snprintf(host_cubox_article,sizeof(host_cubox_article),"%s/cubox-article.md",fixture_root);
    FILE* cf=fopen(host_cubox_article,"wb");
    CHECK(cf && fputs("# Cubox Reader Article\nBody with inline image:\n\n![Inline](picture.png)\n",cf)>=0 && !fclose(cf));
    tap(300,800);
    CHECK(cubox_ui->open_after_download && host_cubox.busy);
    host_cubox.busy=false;host_cubox_cards[0].cached=true;host_cubox_cards[0].cached_images=1;++host_cubox.serial;
    cubox_poll();
    CHECK(app.page==THINGSOS_UI_READING && app.opened && app.reader_from_cubox);
    CHECK(!cubox_ui->painted && !app_cubox_region());
    CHECK(app.chapter.markdown_index && app.chapter.image_count==1 && app.chapter.images[0].width>0 && app.chapter.images[0].height>0);

    /* Return from reader via top-left back arrow navigates to Cubox detail, then < returns to article list, then < to Apps. */
    app.menu=true;draw();
    tap(50,40);
    CHECK(app.page==THINGSOS_UI_CUBOX && cubox_ui->view==CUBOX_VIEW_DETAIL && !app.opened && !app.reader_from_cubox && !app_cubox_region());
    tap(50,140);
    CHECK(app.page==THINGSOS_UI_CUBOX && cubox_ui->view==CUBOX_VIEW_LIST);
    app.transition_refresh=app.force_refresh=app.feedback_refresh=app.controls_refresh=false;app.message[0]=0;
    app_cubox_presented(true);CHECK(app_cubox_region());

    /* Settings -> Disconnect -> Exiting to Library uses transition_refresh and clears Cubox painted state cleanly. */
    cubox_ui->view=CUBOX_VIEW_LIST;draw();
    tap(600,150);CHECK(cubox_ui->view==CUBOX_VIEW_SETTINGS && !app.transition_refresh && !app.force_refresh && app_cubox_region());draw();
    tap(300,880);CHECK(!host_cubox.configured && cubox_ui->view==CUBOX_VIEW_LIST);
    app.transition_refresh=app.force_refresh=false;
    navigate(THINGSOS_UI_LIBRARY);
    app_cubox_presented(true);
    CHECK(app.page==THINGSOS_UI_LIBRARY && app.transition_refresh && !app.force_refresh && !cubox_ui->painted && !app_cubox_region());
    host_network_connected=prev_wifi;host_sd=prev_sd;
    free(cubox_ui);cubox_ui=NULL;
}

int main(int argc, char** argv) {
    CHECK(argc == 2 || argc == 4);
    fixture_root = argv[1];
    snprintf(host_font, sizeof(host_font), "%s/default.ttf", fixture_root);
    char cold_cache[600];snprintf(cold_cache,sizeof(cold_cache),"%s.pages",host_font);
    remove(cold_cache); /* Begin cold; reopen below must exercise persistent reuse. */
    state = calloc(1, sizeof(*state));
    CHECK(state);
    app.canvas = (thingsos_canvas_t){ malloc(684u * 1216u), 684, 1216 };
    CHECK(app.canvas.pixels);
    app.px = 30;app.tilt_sensitivity=1;
    app.line_choice = app.margin_choice = 1;
    app.refresh_pages = 15;
    app.ui_px=30;app.ui_refresh_pages=15;
    app.auto_lock_minutes = 5;
    app.auto_time = true;
    app.hotspot = true;
    app.battery = 100;
    thingsos_runtime_init(&app.runtime);
    thingsos_records_init(&app.records);
    CHECK(thingsos_board_font(&app.font));
    CHECK(thingsos_board_font(&app.ui_font));
    if(argc==4) {
        /* Render a supplied local EPUB chapter through the real controller;
           artifacts only, no firmware-only preview mode or hardware access. */
        host_sd=true;app.px=46;
        CHECK(thingsos_epub_open(&app.epub,argv[2]));app.epub_open=true;
        unsigned chapter=0;
        while(chapter<app.epub.spine_count && strcmp(app.epub.items[app.epub.spine[chapter]].path,argv[3]))++chapter;
        CHECK(chapter<app.epub.spine_count && load_chapter(chapter));
        app.opened=true;app.start=0;app.page=THINGSOS_UI_READING;app.menu=false;
        strlcpy(app.read_path,argv[2],sizeof(app.read_path));strlcpy(app.read_title,app.epub.title,sizeof(app.read_title));
        app.indexed_px=app.px;app.indexed_line=app.line_choice;app.indexed_margin=app.margin_choice;
        app.indexed_weight=app.weight_choice;app.indexed_indent=app.indent_chars;
        CHECK(app.chapter.image_count);app.start=app.chapter.images[0].offset;
        draw();CHECK(app.image_hit_count);
        CHECK(app.image_hits[0].w<=reader_body_width() && app.image_hits[0].h<=reader_body_height());
        canvas_snapshot("real-book-image-fit");
        puts("Real EPUB chapter illustration fits the reading body");return 0;
    }
    charging_status_test();
    font_recovery_test();
    reader_annotation_test();
    reader_background_test();
    shared_qwerty_test();
    frontlight_controls_test();
    usb_controls_test();
    online_transfer_test();
    file_picture_test();
    host_nvs_available=true;
    preferences_save();CHECK(host_nvs_commits==1);
    navigate(THINGSOS_UI_SETTINGS);CHECK(host_nvs_commits==1);
    host_nvs_commit_fail=true;app.px=34;
    preferences_save();CHECK(host_nvs_commits==2);
    host_nvs_commit_fail=false;preferences_save();CHECK(host_nvs_commits==3);
    preferences_save();CHECK(host_nvs_commits==3);
    app.px=30;preferences_save();CHECK(host_nvs_commits==4);
    host_nvs_available=false;
    /* The new tab has a real independent login/shelf/cache/reader controller. */
    navigate(THINGSOS_UI_LIBRARY);host_sd=true;host_network_connected=false;
    tap(250,1180);CHECK(app.page==THINGSOS_UI_WEIDU);
    snapshot("weidu-login");tap(300,830);CHECK(host_weidu_calls==0);
    host_network_connected=true;tap(300,830);CHECK(host_weidu_action==WD_DEVICE_LOGIN && host_weidu_calls==1);
    host_weidu.phase=WD_QR;strcpy(host_weidu.qr,"https://weread.qq.com/web/confirm?uid=public-test-code");++host_weidu.serial;
    weidu_poll();snapshot("weidu-qr");tap(300,1010);CHECK(!host_weidu.busy);
    host_weidu.busy=true;host_weidu.phase=WD_DOWNLOAD;host_weidu.total=host_weidu.completed=0;
    strcpy(host_weidu.message,"正在准备微读服务");++host_weidu.serial;weidu_poll();draw();
    app.dirty=app.transition_refresh=app.force_refresh=false;app.weidu_progress_refresh=false;
    strcpy(host_weidu.message,"正在读取章节目录");++host_weidu.serial;weidu_poll();
    CHECK(app.dirty&&!app.weidu_progress_refresh);snapshot("weidu-download-toc-stage");
    app.dirty=app.transition_refresh=app.force_refresh=false;
    host_weidu.total=100;host_weidu.completed=5;strcpy(host_weidu.message,"第一章");++host_weidu.serial;weidu_poll();
    CHECK(app.dirty&&!app.weidu_progress_refresh); /* Leaving the TOC stage clears the antialiased status line. */
    app.dirty=app.transition_refresh=app.force_refresh=false;app.weidu_progress_refresh=false;
    host_weidu.completed=10;strcpy(host_weidu.message,"第二章");++host_weidu.serial;weidu_poll();
    CHECK(app.dirty&&app.weidu_progress_refresh); /* Later chapter progress keeps its bounded DU path. */
    host_weidu.busy=true;++host_weidu.serial;weidu_poll();
    navigate(THINGSOS_UI_DISPLAY);unsigned locked_size=app.px;tap(550,340);
    CHECK(app.px==locked_size && strstr(app.message,"微读"));
    app.activity_at=0;host_weidu.busy=false;++host_weidu.serial;weidu_poll();
    CHECK(app.activity_at==now());weidu_enter();
    host_weidu.authenticated=true;host_weidu.phase=WD_READY;host_weidu.count=5;++host_weidu.serial;
    weidu_poll();CHECK(app.weidu_count==4 && app.weidu_total==5);snapshot("weidu-shelf");
    unsigned shelf_reads=host_weidu_search_calls;
    weidu_shelf_load();CHECK(host_weidu_search_calls==shelf_reads+1 && app.weidu_count==4);
    shelf_reads=host_weidu_search_calls;app.dirty=false;
    tap(250,1180);CHECK(app.page==THINGSOS_UI_WEIDU && host_weidu_search_calls==shelf_reads && !app.dirty);
    navigate(THINGSOS_UI_SETTINGS);tap(250,1180);
    CHECK(app.page==THINGSOS_UI_WEIDU && host_weidu_search_calls==shelf_reads && app.weidu_count==4);
    navigate(THINGSOS_UI_SETTINGS);host_weidu.background=true;app.library_valid=true;
    ++host_weidu.serial;weidu_poll();CHECK(app.library_valid);tap(250,1180);
    CHECK(host_weidu_search_calls==shelf_reads); /* Reading status preserves shelf and covers. */
    {
        thingsos_weidu_action_t previous_action=app.weidu_action;app.weidu_action=WD_DEVICE_SEARCH;
        app.weidu_covers_pending=app.weidu_covers_redraw=false;
        host_weidu.background=true;host_weidu.busy=false;host_weidu.phase=WD_READY;host_weidu.message[0]=0;
        host_wd_reading.action=WD_DEVICE_REPORT;++host_weidu.serial;app.dirty=false;weidu_poll();
        CHECK(!app.dirty && host_weidu_search_calls==shelf_reads && !app.weidu_covers_pending);
        unsigned cover_calls=host_weidu_cover_calls;
        app.activity_at=now()-1000;
        app.weidu_covers_pending=true;
        host_weidu_background_ready=false;weidu_poll();
        CHECK(app.weidu_covers_pending && host_weidu_cover_calls==cover_calls);
        host_weidu_background_ready=true;
        app.wd_sync_panel=true;weidu_poll();
        CHECK(app.weidu_covers_pending && host_weidu_cover_calls==cover_calls);
        app.wd_sync_panel=false;weidu_poll();
        CHECK(!app.weidu_covers_pending && host_weidu_cover_calls==cover_calls+1);
        app.activity_at=now();
        host_wd_reading.action=WD_DEVICE_COVERS;++host_weidu.serial;weidu_poll();
        CHECK(!app.dirty && !app.weidu_covers_redraw && host_weidu_search_calls==shelf_reads);
        ++host_cover_serial;++host_weidu.serial;weidu_poll();
        CHECK(!app.dirty && app.weidu_covers_redraw);
        app.activity_at=now()-1000;weidu_poll();CHECK(app.dirty && !app.weidu_covers_redraw);
        app.dirty=false;weidu_poll();CHECK(!app.dirty);
        host_wd_reading.action=WD_DEVICE_RT_UPLOAD;host_weidu.phase=WD_ERROR;
        strcpy(host_weidu.message,thingsos_weidu_error_text(WD_NETWORK));
        app.weidu_banner[0]=0;++host_weidu.serial;weidu_poll();
        CHECK(!app.dirty && !app.weidu_banner[0] && host_weidu_search_calls==shelf_reads);
        CHECK(app.weidu.phase==WD_ERROR && app.weidu.message[0]);
        strcpy(host_weidu.message,thingsos_weidu_error_text(WD_MEMORY));
        ++host_weidu.serial;weidu_poll();
        CHECK(!app.dirty && !app.weidu_banner[0] && host_weidu_search_calls==shelf_reads);
        host_weidu.background=false;app.weidu_action=WD_DEVICE_SYNC;++host_weidu.serial;weidu_poll();
        CHECK(app.dirty && weidu_banner_visible(false) && !strcmp(app.weidu_banner,host_weidu.message));
        app.weidu_banner[0]=0;host_weidu.phase=WD_READY;host_weidu.message[0]=0;
        shelf_reads=host_weidu_search_calls;
        app.weidu_action=previous_action;app.weidu_covers_retry_at=0;
    }
    navigate(THINGSOS_UI_SETTINGS);host_weidu.background=false;++host_weidu.serial;weidu_poll();tap(250,1180);
    CHECK(host_weidu_search_calls==shelf_reads+1); /* Explicit shelf changes invalidate reused rows. */
    host_weidu_search_blocked=true;weidu_shelf_load();
    CHECK(app.weidu_count==4 && app.weidu_total==5); /* A transient background lock retains the shelf. */
    app.weidu_count=app.weidu_total=0;
    host_weidu.background=true;host_weidu.busy=true;++host_weidu.serial;weidu_poll();
    host_weidu_search_blocked=false;host_weidu.busy=false;host_weidu.phase=WD_READY;
    host_weidu.message[0]=0;++host_weidu.serial;weidu_poll();
    CHECK(app.weidu_count==4 && app.weidu_total==5); /* Completion reloads without tab navigation. */
    host_weidu.background=false;
    app.transition_refresh=false;shelf_reads=host_weidu_search_calls;
    weidu_scroll(-120);CHECK(app.weidu_start==3 && app.weidu_count==2 && !app.transition_refresh);
    CHECK(host_weidu_search_calls==shelf_reads+1); /* Reuse hero; scan only the requested rows. */
    weidu_scroll(120);CHECK(app.weidu_start==0 && app.weidu_count==4);
    host_weidu_shelf_total=12;
    shelf_reads=host_weidu_search_calls;
    tap(610,260);CHECK(app.weidu_cards && app.weidu_count==9 && host_weidu_search_calls==shelf_reads+1);snapshot("weidu-cards");
    int previous_cover_bottom=app.shelf_cover_y+app.shelf_cover_h;
    weidu_scroll(-120);CHECK(app.weidu_start==9 && app.weidu_count==3);snapshot("weidu-cards-last");
    CHECK(app.shelf_cover_y+app.shelf_cover_h>=previous_cover_bottom);
    unsigned cleanups=host_cleanup_requests;
    app.force_refresh=app.transition_refresh=false;
    navigate(THINGSOS_UI_SETTINGS);
    CHECK(host_cleanup_requests==cleanups+1 && !app.force_refresh && !app.transition_refresh);
    CHECK(host_cleanup_y==90 && host_cleanup_h==1008 && host_cleanup_x==0 && host_cleanup_w==684);
    navigate(THINGSOS_UI_APPS);CHECK(host_cleanup_requests==cleanups+2);
    navigate(THINGSOS_UI_WEIDU);
    weidu_scroll(-120);CHECK(app.weidu_start==9);
    tap(610,260);CHECK(!app.weidu_cards && app.weidu_start==0 && app.weidu_count==4);
    /* Search covers the full shelf, including books beyond the visible page. */
    tap(545,260);CHECK(app.page==THINGSOS_UI_PASSWORD && app.editing==5);
    snapshot("weidu-search-keyboard");
    /* Language changes preserve the input field and submit/cancel buttons.
       A layout event has one final board presentation and no idle job. */
    unsigned char* search_top=malloc(684u*638);CHECK(search_top);
    memcpy(search_top,app.canvas.pixels,684u*638);
    bool search_chinese=app.input.chinese;
    app.force_refresh=app.transition_refresh=false;
    for(unsigned i=0;i<2;++i) {
        CHECK(keyboard_mode_press(480,1030) && app.keyboard_refresh && app.input_refresh);
        CHECK(app.input.chinese==(i==0?!search_chinese:search_chinese));
        draw();CHECK(!memcmp(search_top,app.canvas.pixels,684u*638) && !app.force_refresh);
        app.keyboard_refresh=app.input_refresh=false;
    }
    free(search_top);
    thingsos_input_open(&app.input,"测试书 12",96,false);tap(590,1030);
    CHECK(app.page==THINGSOS_UI_WEIDU && app.weidu_total==1 && !strcmp(app.weidu_books[0].id,"12"));
    snapshot("weidu-search-result");
    tap(100,260);CHECK(!app.weidu_query[0] && app.weidu_total==12 && app.weidu_start==0);
    tap(545,260);thingsos_input_open(&app.input,"测试书 12",96,false);tap(590,1030);
    tap(250,1180);CHECK(!app.weidu_query[0] && app.weidu_total==12 && app.weidu_count==4);
    tap(545,260);thingsos_input_open(&app.input,"测试书 12",96,false);tap(590,1030);
    capacitive_key(THINGSOS_KEY_HOME);CHECK(!app.weidu_query[0] && app.page==THINGSOS_UI_WEIDU && app.weidu_total==12);

    tap(545,260);thingsos_input_open(&app.input,"不存在",96,false);tap(590,1030);
    CHECK(app.weidu_count==0 && app.weidu_total==0);snapshot("weidu-search-empty");
    tap(100,260);CHECK(!app.weidu_query[0] && app.weidu_total==12);
    unsigned calls_before_online_search=host_weidu_calls;
    host_online_search_async=true;host_weidu.busy=false;
    tap(545,260);thingsos_input_open(&app.input,"线上书籍",96,false);tap(590,1030);
    CHECK(weidu_search.pending && weidu_search.running && app.weidu.busy && !weidu_search.failed);
    CHECK(!app.weidu_count);snapshot("weidu-online-search-pending");
    host_online_search_ready=true;host_weidu.busy=false;host_weidu.phase=WD_READY;++host_weidu.serial;
    weidu_poll();CHECK(!weidu_search.pending && app.weidu_count==1 && !app.weidu_shelf_member[0]);
    snapshot("weidu-online-search-result");
    tap(100,420);CHECK(app.weidu_detail && !online_pending());snapshot("weidu-online-add-shelf");
    tap(300,710);CHECK(host_weidu_action==WD_DEVICE_SHELF_ADD && !strcmp(app.weidu_query,"线上书籍"));
    host_online_added=true;host_weidu.busy=false;host_weidu.background=false;host_weidu.phase=WD_READY;++host_weidu.serial;
    weidu_poll();CHECK(app.weidu_detail && app.weidu_shelf_member[0]);
    unsigned added_calls=host_weidu_calls;tap(300,710);CHECK(host_weidu_calls==added_calls);
    snapshot("weidu-online-on-shelf");tap(300,275);CHECK(!app.weidu_detail && app.weidu_count==1);
    /* Completion while editing must neither repaint the static keyboard nor
       lose successful remote results when Cancel returns to the result page. */
    host_online_search_ready=false;weidu_search_submit();CHECK(weidu_search.running && !app.weidu_count);
    tap(545,260);CHECK(app.page==THINGSOS_UI_PASSWORD);
    app.dirty=false;host_online_search_ready=true;host_weidu.busy=false;host_weidu.phase=WD_READY;++host_weidu.serial;
    weidu_poll();CHECK(!weidu_search.pending && !app.dirty && !app.weidu_shelf_valid);
    tap(100,590);CHECK(app.page==THINGSOS_UI_WEIDU && app.weidu_count==1 && app.weidu_shelf_valid);
    /* A failed search is not an empty successful result; exiting a pending
       search prevents its later completion from reopening the results. */
    host_online_search_ready=false;weidu_search_submit();CHECK(weidu_search.pending);
    host_weidu.busy=false;host_weidu.phase=WD_ERROR;++host_weidu.serial;weidu_poll();
    CHECK(weidu_search.failed && !weidu_search.pending);snapshot("weidu-online-search-error");
    weidu_search_submit();CHECK(weidu_search.running);tap(100,260);
    CHECK(!weidu_search.pending && !app.weidu_query[0]);
    host_online_search_ready=true;host_weidu.busy=false;host_weidu.phase=WD_READY;++host_weidu.serial;weidu_poll();
    CHECK(!app.weidu_query[0] && app.weidu_total==12);
    host_online_search_async=host_online_search_ready=host_online_added=false;
    host_weidu_calls=calls_before_online_search;
    tap(610,260);CHECK(app.weidu_cards);
    tap(545,260);thingsos_input_open(&app.input,"测试书 12",96,false);tap(590,1030);
    tap(100,260);CHECK(!app.weidu_query[0] && app.weidu_total==12 && app.weidu_count==9);
    tap(610,260);CHECK(!app.weidu_cards);

    tap(545,260);thingsos_input_open(&app.input,"",96,false);tap(590,1030);
    CHECK(app.weidu_total==12 && app.weidu_count==4);
    tap(545,260);thingsos_input_open(&app.input,"cancel",96,false);tap(100,590);
    CHECK(!app.weidu_query[0] && app.weidu_total==12);
    host_weidu_shelf_total=5;weidu_shelf_load();
    snprintf(host_weidu_epub,sizeof(host_weidu_epub),"%s/valid.epub",fixture_root);
    snapshot("weidu-shelf-cached");
    CHECK(app.canvas.pixels[500u*684+36]==0);
    tap(610,260);snapshot("weidu-cards-cached");
    CHECK(app.canvas.pixels[500u*684+60]==0);
    tap(610,260);
    CHECK(weidu_hold_delay(150,420)==1000);
    CHECK(!weidu_release_hold(150,420,999) && !app.weidu_detail);
    CHECK(weidu_release_hold(150,420,1000) && app.weidu_detail);snapshot("weidu-book");
    tap(300,275);CHECK(!app.weidu_detail && app.page==THINGSOS_UI_WEIDU);
    {
        char saved_path[512];strcpy(saved_path,host_weidu_epub);host_weidu_epub[0]=0;
        thingsos_weidu_status_t saved_status=host_weidu;unsigned saved_calls=host_weidu_calls;
        CHECK(weidu_release_hold(150,420,1000) && app.weidu_detail && !online_pending());
        snapshot("weidu-uncached-cover-menu");
        CHECK(region_ink(790,850)>0 && region_ink(885,945)>0 && region_ink(985,1045)>0);
        tap(300,910);CHECK(host_weidu_action==WD_DEVICE_DOWNLOAD && host_weidu.busy);
        tap(300,1010);CHECK(!host_weidu.busy && host_weidu.phase==WD_CANCELLED);
        host_weidu=saved_status;++host_weidu.serial;host_weidu_calls=saved_calls;app.weidu=host_weidu;
        strcpy(host_weidu_epub,saved_path);app.weidu_detail=false;
    }
    tap(150,420);CHECK(app.page==THINGSOS_UI_READING && app.reader_from_weidu && app.opened);
    app.menu=true;tap(40,35);CHECK(app.page==THINGSOS_UI_WEIDU && !app.opened);
    CHECK(weidu_hold(150,420) && app.weidu_detail);
    tap(300,1010);CHECK(app.page==THINGSOS_UI_READING && app.reader_from_weidu && app.opened);
    app.menu=true;tap(40,35);CHECK(app.page==THINGSOS_UI_WEIDU && !app.opened);
    tap(580,150);CHECK(app.weidu_logout_confirm && host_weidu_calls==1);
    tap(500,930);CHECK(host_weidu_action==WD_DEVICE_LOGOUT && host_weidu_calls==2);
    memset(&host_weidu,0,sizeof(host_weidu));host_weidu_epub[0]=0;host_network_connected=false;
    thingsos_runtime_init(&app.runtime);thingsos_records_init(&app.records);app.recent_count=0;
    app.force_refresh=false;app.transition_refresh=false;
    navigate(THINGSOS_UI_APPS);CHECK(!app.transition_refresh && !app.force_refresh);
    app.transition_refresh=false;navigate(THINGSOS_UI_SETTINGS);CHECK(!app.transition_refresh && !app.force_refresh);
    const thingsos_ui_page_t home_pages[]={THINGSOS_UI_LIBRARY,THINGSOS_UI_WEIDU,THINGSOS_UI_APPS,THINGSOS_UI_SETTINGS};
    for(unsigned from=0;from<4;++from)for(unsigned to=0;to<4;++to) {
        app.page=home_pages[from];app.transition_refresh=false;navigate(home_pages[to]);CHECK(!app.transition_refresh);
    }
    const thingsos_ui_page_t subpages[]={THINGSOS_UI_DISPLAY,THINGSOS_UI_TIME,THINGSOS_UI_POWER,
        THINGSOS_UI_WIFI,THINGSOS_UI_PASSWORD,THINGSOS_UI_UPGRADE,THINGSOS_UI_RECOVERY,
        THINGSOS_UI_USB,THINGSOS_UI_FILES,THINGSOS_UI_FONTS,THINGSOS_UI_WALLPAPER};
    for(unsigned root=2;root<4;++root)for(unsigned sub=0;sub<sizeof(subpages)/sizeof(subpages[0]);++sub) {
        app.page=home_pages[root];app.transition_refresh=false;
        unsigned before=host_cleanup_requests;
        navigate(subpages[sub]);CHECK(!app.transition_refresh && host_cleanup_requests==before+1);
        CHECK(host_cleanup_y==74 && host_cleanup_h==1024);
        navigate(home_pages[root]);CHECK(!app.transition_refresh && host_cleanup_requests==before+2);
    }
    app.page=THINGSOS_UI_LIBRARY;app.confirm_action=0;app.time_edit=false;
    CHECK(home_tab_press(600,1150) && app.page==THINGSOS_UI_SETTINGS);
    CHECK(!home_tab_press(600,1097));
    app.confirm_action=1;CHECK(!home_tab_press(20,1150) && app.page==THINGSOS_UI_SETTINGS);
    app.confirm_action=0;app.time_edit=true;CHECK(!home_tab_press(20,1150));app.time_edit=false;
    app.page=THINGSOS_UI_READING;CHECK(!home_tab_press(600,1150));
    app.page=THINGSOS_UI_STATS;app.transition_refresh=false;navigate(THINGSOS_UI_LIBRARY);
    CHECK(app.transition_refresh); /* Leaving dense statistics retains ghost cleanup. */
    app.transition_refresh=false;
    app.transition_refresh=false;navigate(THINGSOS_UI_SETTINGS);CHECK(!app.transition_refresh && !app.force_refresh);
    /* Real EPUB cover is decoded once and its final thumbnail is reused across
       draws and Tab returns. A forced scan invalidates both list and covers. */
    host_sd=true;
    app.book_count=1;app.books[0].id=991;app.books[0].epub=true;
    strlcpy(app.books[0].name,"cache test",sizeof(app.books[0].name));
    snprintf(app.books[0].path,sizeof(app.books[0].path),"%s/valid.epub",fixture_root);
    app.library_valid=true;app.library_card=app.records.card_id;
    strlcpy(app.library_path,app.records.library_path,sizeof(app.library_path));
    navigate(THINGSOS_UI_LIBRARY);draw();CHECK(app.cover_loads==1);
    const thingsos_thumbnail_t* cached_cover=thingsos_thumbnail_find(&app.thumbnails,991,COVER_WIDTH,COVER_HEIGHT);
    CHECK(cached_cover && cached_cover->pixels);
    unsigned char white[4]={255,255,255,255};
    CHECK(!cover_has_content(white,2,2));white[3]=100;CHECK(cover_has_content(white,2,2));
    char white_path[640];snprintf(white_path,sizeof(white_path),"%s/white-cover.bmp",fixture_root);
    unsigned char bmp[58]={0x42,0x4d,58,0,0,0,0,0,0,0,54,0,0,0,40,0,0,0,1,0,0,0,1,0,0,0,1,0,24};
    bmp[54]=bmp[55]=bmp[56]=255;
    FILE* blank_file=fopen(white_path,"wb");CHECK(blank_file && fwrite(bmp,1,sizeof(bmp),blank_file)==sizeof(bmp) && !fclose(blank_file));
    thingsos_thumbnail_clear(&app.thumbnails);
    CHECK(cover_from_mode(&app.books[0],36,344,124,174,white_path,false)); /* White sidecar falls back to real EPUB. */
    cached_cover=thingsos_thumbnail_find(&app.thumbnails,991,COVER_WIDTH,COVER_HEIGHT);
    CHECK(cached_cover && cover_has_content(cached_cover->pixels,cached_cover->width,cached_cover->height));
    /* Reproduce a stale blank thumbnail for the first card and verify retry. */
    white[3]=255;CHECK(thingsos_thumbnail_store(&app.thumbnails,991,COVER_WIDTH,COVER_HEIGHT,white,2,2));
    CHECK(cover_from_mode(&app.books[0],36,344,124,174,white_path,false));
    snapshot("cover-white-recovered");
    CHECK(remove(white_path)==0);
    snprintf(host_thumbnail_cache_root,sizeof(host_thumbnail_cache_root),"%s/thumbnails-test",fixture_root);
    mkdir(host_thumbnail_cache_root,0775);
    char thumb_path[640];uint64_t thumb_stamp;
    CHECK(thumbnail_disk_path(&app.books[0],NULL,thumb_path,&thumb_stamp));
    remove(thumb_path);thingsos_thumbnail_clear(&app.thumbnails);
    unsigned disk_loads=app.cover_loads;
    CHECK(cover_from_mode(&app.books[0],36,344,124,174,NULL,false) && app.cover_loads==disk_loads+1);
    thingsos_thumbnail_clear(&app.thumbnails);
    CHECK(cover_from_mode(&app.books[0],36,344,124,174,NULL,false) && app.cover_loads==disk_loads+1);
    /* A mismatched content version must rebuild instead of showing stale pixels. */
    unsigned char* tiny=thingsos_image_buffer(COVER_WIDTH*COVER_HEIGHT);CHECK(tiny);
    memset(tiny,0,COVER_WIDTH*COVER_HEIGHT);
    thingsos_canvas_t disk_canvas={.pixels=tiny,.width=COVER_WIDTH,.height=COVER_HEIGHT};
    CHECK(thingsos_cover_cache_save(thumb_path,thumb_stamp^1,&disk_canvas,0,0,COVER_WIDTH,COVER_HEIGHT));
    free(tiny);thingsos_thumbnail_clear(&app.thumbnails);
    CHECK(cover_from_mode(&app.books[0],36,344,124,174,NULL,false) && app.cover_loads==disk_loads+2);
    FILE* bad_thumb=fopen(thumb_path,"r+b");CHECK(bad_thumb && !fseek(bad_thumb,50,SEEK_SET));
    int old_pixel=fgetc(bad_thumb);CHECK(old_pixel!=EOF && !fseek(bad_thumb,-1,SEEK_CUR) && fputc(old_pixel^255,bad_thumb)!=EOF && !fclose(bad_thumb));
    thingsos_thumbnail_clear(&app.thumbnails);
    CHECK(cover_from_mode(&app.books[0],36,344,124,174,NULL,false) && app.cover_loads==disk_loads+3);
    host_thumbnail_cache_root[0]=0;CHECK(remove(thumb_path)==0);
    book_t retry_book={0};retry_book.id=993;strcpy(retry_book.path,"not-present-cover.txt");
    FILE* damaged=fopen(white_path,"wb");CHECK(damaged && fwrite("invalid-image",1,13,damaged)==13 && !fclose(damaged));
    unsigned retry_loads=app.cover_loads;uint64_t retry_clock=host_clock_us;
    CHECK(!cover_from_mode(&retry_book,36,344,124,174,white_path,false));
    CHECK(app.cover_loads==retry_loads+1);
    CHECK(!cover_from_mode(&retry_book,36,344,124,174,white_path,false));
    CHECK(app.cover_loads==retry_loads+1); /* Failed decode cannot repeat on each Tab. */
    host_clock_us+=16000000;
    CHECK(!cover_from_mode(&retry_book,36,344,124,174,white_path,false));
    CHECK(app.cover_loads==retry_loads+2); /* Failures remain retryable. */
    cover_retry_clear();host_clock_us=retry_clock;CHECK(remove(white_path)==0);
    thingsos_rect(&app.canvas,36,344,124,174,255);
    CHECK(cover_from_mode(&app.books[0],36,344,124,174,NULL,false));
    unsigned char cover_pixels[124*174];
    for(unsigned y=0;y<174;++y) memcpy(cover_pixels+y*124,app.canvas.pixels+(344u+y)*684+36,124);
    navigate(THINGSOS_UI_SETTINGS);app.dirty=false;tap(70,1180);
    CHECK(app.page==THINGSOS_UI_LIBRARY && app.book_count==1);
    draw();CHECK(app.cover_loads==0);
    for(unsigned y=0;y<174;++y) CHECK(!memcmp(cover_pixels+y*124,app.canvas.pixels+(344u+y)*684+36,124));
    /* Once the card image exists, list/hero sizes must not read/decode again.
       A temporarily unavailable original file cannot break only one view. */
    char cover_path[THINGSOS_RECORD_PATH_BYTES];strcpy(cover_path,app.books[0].path);
    strcpy(app.books[0].path,"temporarily-unavailable.epub");
    cover(&app.books[0],36,620,94,130);CHECK(app.cover_loads==0);
    app.library_cards=true;snapshot("cover-shared-card");CHECK(app.cover_loads==0);
    app.library_cards=false;snapshot("cover-shared-list");CHECK(app.cover_loads==0);
    strcpy(app.books[0].path,cover_path);
    size_t old_recent=app.recent_count;uint64_t old_recent_id=app.recent_ids[0];
    app.recent_count=1;app.recent_ids[0]=991;app.lock_wallpaper=1;
    app.wallpaper_book=app.books[0];
    lock_draw(0);canvas_snapshot("lock-cover");
    CHECK(!region_ink(0,74) && !region_ink(1098,1216) && region_ink(150,836)>1000);
    CHECK(region_ink(150,836)>100000);
    CHECK(!region_ink(1040,1098)); /* No reading-progress row. */
    size_t thumbnail_bytes=app.thumbnails.bytes;
    snprintf(app.books[0].path,sizeof(app.books[0].path),"%s/lock-cover.epub",fixture_root);
    app.wallpaper_book=app.books[0];
    snprintf(host_lock_cache_root,sizeof(host_lock_cache_root),"%s/lock-covers-test",fixture_root);
    mkdir(host_lock_cache_root,0775);
    char wallpaper_cache[640],wallpaper_backup[650];
    snprintf(wallpaper_cache,sizeof(wallpaper_cache),"%s/%016llx.gray",host_lock_cache_root,991ULL);
    snprintf(wallpaper_backup,sizeof(wallpaper_backup),"%s.bak",wallpaper_cache);
    remove(wallpaper_cache);remove(wallpaper_backup);
    lock_draw(0);canvas_snapshot("lock-cover-original");
    /* A stale 148x188 cover for this same ID must never replace original detail. */
    for(unsigned x=0;x<684;++x)CHECK(app.canvas.pixels[300u*684+x]==(x%2?255:0));
    CHECK(app.thumbnails.bytes==thumbnail_bytes);
    unsigned original_loads=app.cover_loads;
    lock_draw(0);CHECK(app.cover_loads==original_loads); /* Reads SD pixels, no decode. */
    for(unsigned x=0;x<684;++x)CHECK(app.canvas.pixels[300u*684+x]==(x%2?255:0));
    uint64_t cover_stamp=lock_cover_stamp(&app.books[0],NULL);
    char blocked_temp[650];snprintf(blocked_temp,sizeof(blocked_temp),"%s.tmp",wallpaper_cache);
    remove(blocked_temp);CHECK(!mkdir(blocked_temp,0775));
    CHECK(!thingsos_cover_cache_save(wallpaper_cache,cover_stamp,&app.canvas,0,72,684,869));
    CHECK(thingsos_cover_cache_load(wallpaper_cache,cover_stamp,&app.canvas,0,72,684,869));
    CHECK(!_rmdir(blocked_temp)); /* Failed writes leave the committed cache intact. */
    app.canvas.pixels[72u*684]=123;
    CHECK(!thingsos_cover_cache_load(wallpaper_cache,cover_stamp^1,&app.canvas,0,72,684,869));
    CHECK(app.canvas.pixels[72u*684]==123);
    FILE* corrupt_cover=fopen(wallpaper_cache,"r+b");CHECK(corrupt_cover);
    CHECK(!fseek(corrupt_cover,48,SEEK_SET));int cover_byte=fgetc(corrupt_cover);CHECK(cover_byte!=EOF);
    CHECK(!fseek(corrupt_cover,48,SEEK_SET) && fputc(cover_byte^1,corrupt_cover)!=EOF && !fclose(corrupt_cover));
    lock_draw(0);CHECK(app.cover_loads==original_loads+1); /* Corruption rebuilds from original. */
    for(unsigned x=0;x<684;++x)CHECK(app.canvas.pixels[300u*684+x]==(x%2?255:0));
    CHECK(!rename(wallpaper_cache,wallpaper_backup));
    lock_draw(0);CHECK(app.cover_loads==original_loads+1); /* Interrupted rename retains backup. */
    CHECK(!rename(wallpaper_backup,wallpaper_cache));
    CHECK(!thingsos_cover_cache_load(wallpaper_cache,cover_stamp,&app.canvas,-1,72,684,869));
    strcpy(app.books[0].path,cover_path);
    app.wallpaper_book=app.books[0];
    lock_draw(0);CHECK(app.cover_loads==original_loads+2); /* Changed source path invalidates SD data. */
    host_lock_cache_root[0]=0;
    strcpy(app.books[0].path,cover_path);
    app.lock_wallpaper=0;app.recent_count=old_recent;app.recent_ids[0]=old_recent_id;
    draw();
    unsigned hits_before_status=app.thumbnails.hits;
    draw_status();CHECK(app.thumbnails.hits==hits_before_status && app.cover_loads==0);
    for(unsigned y=0;y<174;++y) CHECK(!memcmp(cover_pixels+y*124,app.canvas.pixels+(344u+y)*684+36,124));
    app.dirty=false;tap(70,1180);CHECK(!app.dirty && app.book_count==1);
    app.message[0]='x';tap(70,1180);CHECK(app.dirty && !app.message[0]);
    app.library_card^=1;library_load();CHECK(app.book_count==0 && app.thumbnails.bytes==0);
    host_sd=false;
    app.activity_at = 10001;
    CHECK(!lock_due(10000)); /* Loop timestamp predates touch activity. */
    CHECK(!lock_due(10001 + 300000));
    CHECK(lock_due(10001 + 300001));
    host_network_connected=false;
    navigate(THINGSOS_UI_TRANSFER);CHECK(app.hotspot && host_hotspot);
    app.activity_at=10001;
    host_network_busy=true;transfer_activity(310002);auto_sleep_poll(310002);CHECK(!app.asleep);
    host_network_busy=false;
    unsigned auto_suspends=host_suspend;
    auto_sleep_poll(310003);CHECK(!app.asleep && host_suspend==auto_suspends);
    CHECK(app.activity_at==310002);
    auto_sleep_poll(610002);CHECK(!app.asleep); /* Existing five-minute boundary. */
    auto_sleep_poll(610003);CHECK(app.asleep && host_suspend==auto_suspends+1 && !app.hotspot);
    auto_sleep_poll(610004);CHECK(host_suspend==auto_suspends+1);
    tap(300,1100);capacitive_key(THINGSOS_KEY_HOME);CHECK(app.asleep && app.page==THINGSOS_UI_TRANSFER);
    power_action(1);CHECK(!app.asleep && app.page==THINGSOS_UI_TRANSFER && app.hotspot && host_hotspot);
    navigate(THINGSOS_UI_LIBRARY);CHECK(!app.hotspot && !host_hotspot);
    app.lock_return = THINGSOS_UI_LIBRARY;
    navigate(THINGSOS_UI_LOCK);
    snapshot("lock-centered");
    CHECK(region_ink(250,550)>500 && !region_ink(0,74) && !region_ink(1098,1216));
    unsigned char* locked=malloc(684u*1216u);CHECK(locked);memcpy(locked,app.canvas.pixels,684u*1216u);
    time_t lock_time=host_wall_time;host_wall_time+=86400;draw();
    CHECK(!memcmp(locked,app.canvas.pixels,684u*1216u));free(locked);host_wall_time=lock_time;
    /* Sleeping retains the lock frame; the short power key restores its destination. */
    unsigned network_sleeps=host_network_sleeps,network_wakes=host_network_wakes;
    power_action(1);
    CHECK(app.asleep && app.page==THINGSOS_UI_LOCK && host_sleep_body_ink>500 && !region_ink(0,74) && !region_ink(1098,1216));
    CHECK(host_network_sleeps==network_sleeps+1 && host_network_wakes==network_wakes);
    canvas_snapshot("sleep-from-lock");
    app.wifi_requested=1; /* A pre-sleep manual request must not survive wake. */
    power_action(1);
    CHECK(!app.asleep && app.page==THINGSOS_UI_LIBRARY);
    CHECK(host_network_wakes==network_wakes+1 && !app.wifi_requested);
    CHECK(app.lock_wave_pending&&!app.lock_wave_forward);
    CHECK(present_lock_transition()==ESP_OK&&!app.lock_wave_pending&&!host_lock_forward);
    snapshot("lock-after-wake");
    uint64_t stale_time = now();
    CHECK(app.page == THINGSOS_UI_LIBRARY && app.dirty && app.force_refresh);
    CHECK(!lock_due(stale_time));
    snapshot("unlock-library");
    app.lock_return = THINGSOS_UI_LOCK;
    navigate(THINGSOS_UI_LOCK);
    capacitive_key(THINGSOS_KEY_HOME);
    CHECK(app.page == THINGSOS_UI_LIBRARY && app.force_refresh);
    navigate(THINGSOS_UI_FILES);
    snapshot("files-no-sd");
    tap(120, 320); /* Navigation controls cannot open the internal example. */
    CHECK(!app.opened && app.page == THINGSOS_UI_FILES);
    app.force_refresh=false;app.transition_refresh=false;
    tap(570,330);CHECK(app.file_more);tap(120,595);
    CHECK(app.opened && app.runtime.location.book_id == 1 && app.transition_refresh && !app.force_refresh);
    snapshot("example");
    CHECK(!app.pagination_valid && app.pagination_started && app.end > 0);
    CHECK(pagination_step() && app.pagination_valid);
    CHECK(app.chapter.bytes == 12 && !memcmp(app.chapter.text, "Example Test", 12));
    app.transition_refresh=false;app.dirty=false;
    capacitive_key(THINGSOS_KEY_HOME);
    CHECK(app.page==THINGSOS_UI_LIBRARY && app.transition_refresh && !app.force_refresh);
    navigate(THINGSOS_UI_READING); /* Restore the open book for menu/footer checks. */
    reader_surface_refresh_test();
    reader_key_priority_test();reader_font_parent_test();
    size_t menu_start=app.start;
    for(unsigned wide=0;wide<2;++wide)for(unsigned immersive=0;immersive<2;++immersive) {
        app.landscape_enabled=wide!=0;app.immersive_enabled=immersive!=0;reader_canvas_geometry();
        int left=reader_width()/3,right=reader_width()*2/3;
        int top=reader_height()/3,bottom=reader_height()*2/3;
        const int points[][3]={{left,top,1},{right-1,bottom-1,1},
            {left,top-1,0},{right-1,bottom,0},{right,top,0},
            {reader_width()/2,100,0},{reader_width()/2,reader_height()-100,0}};
        for(unsigned i=0;i<sizeof(points)/sizeof(points[0]);++i) {
            app.menu=false;thingsos_runtime_overlay(&app.runtime,false,now());app.dirty=false;
            tap(points[i][0],points[i][1]);
            CHECK(app.menu==(points[i][2]!=0) && app.runtime.reading_overlay==app.menu);
            CHECK(app.start==menu_start && app.dirty==app.menu);
        }
    }
    app.landscape_enabled=false;app.immersive_enabled=false;reader_canvas_geometry();
    app.menu=false;thingsos_runtime_overlay(&app.runtime,false,now());
    for(unsigned i=0;i<2;++i) {
        app.dirty=false;app.transition_refresh=false;
        capacitive_key(THINGSOS_KEY_MENU);
        CHECK(app.menu && app.runtime.reading_overlay && app.dirty && !app.transition_refresh && reader_ui_active());
        app.dirty=false;app.transition_refresh=false;
        capacitive_key(THINGSOS_KEY_MENU);
        CHECK(!app.menu && !app.jump && !app.runtime.reading_overlay && app.dirty && !app.transition_refresh && !reader_ui_active());
        CHECK(app.page==THINGSOS_UI_READING && app.start==menu_start && !app.force_refresh);
    }
    tap(340, 500);
    CHECK(app.menu);
    snapshot("menu");
    tap(580, 560);
    CHECK(app.px == 34);
    tap(620, 490);
    CHECK(!app.menu);
    size_t footer_start=app.start;
    app.dirty=false;app.force_refresh=false;app.transition_refresh=false;
    for(unsigned i=0;i<3;++i) {
        tap((int)(i*341),1140+(int)i*37);
        CHECK(!app.menu && !app.jump && !app.dirty && !app.force_refresh && !app.transition_refresh && app.start==footer_start);
    }
    tap(340, 1192);CHECK(!app.menu && !app.jump && !app.dirty);
    tap(340, 500);CHECK(app.menu);
    tap(500, 1070);CHECK(app.jump); /* Explicit menu jump remains available. */
    app.jump_percent=50;snapshot("jump");
    tap(100,820);CHECK(app.jump_percent==45);
    tap(600,820);CHECK(app.jump_percent==50);
    tap(342,820);tap(342,985);CHECK(app.jump && app.jump_percent==50);
    app.jump_percent=2;tap(100,820);CHECK(!app.jump_percent);
    app.jump_percent=98;tap(600,820);CHECK(app.jump_percent==100);
    tap(342,1100);CHECK(app.jump); /* Gap above footer buttons is inert. */
    tap(342, 930);
    CHECK(app.jump_percent == 50);
    tap(100, 1150);
    CHECK(!app.jump);
    navigate(THINGSOS_UI_SETTINGS);
    snapshot("settings");
    tap(590,150);CHECK(app.page==THINGSOS_UI_SETTINGS);
    navigate(THINGSOS_UI_LIBRARY);tap(590,150);CHECK(app.page==THINGSOS_UI_STATS);
    tap(40,140);CHECK(app.page==THINGSOS_UI_LIBRARY);
    navigate(THINGSOS_UI_SETTINGS);
    tap(140, 280);
    CHECK(app.page == THINGSOS_UI_DISPLAY);
    snapshot("display");
    unsigned reader_px=app.px,reader_weight=app.weight_choice;
    bool reader_ripple=app.ripple_enabled;
    tap(550,735);CHECK(app.ui_weight==2 && app.weight_choice==reader_weight);
    snapshot("display-global-weight");
    CHECK(!app.font.ui_size_percent && !app.font.ui_weight_override);
    for (int i = 0; i < 20; ++i) tap(550, 340);
    CHECK(app.ui_px==50 && app.px==reader_px);
    snapshot("display-ui-size-50");
    for(int i=0;i<20;++i)tap(270,340);
    CHECK(app.ui_px==26 && app.px==reader_px);
    tap(550,340);tap(550,340);CHECK(app.ui_px==30);
    tap(438,480);CHECK(app.ui_refresh_pages==15 && app.refresh_pages==15);
    for(unsigned i=0;i<4;++i) {
        tap(240+(int)i*105,480);CHECK(app.ui_refresh_pages==5+i*5);
        tap(240+(int)i*105,804);CHECK(app.refresh_pages==15);
    }
    app.force_refresh=false;tap(550,872);
    CHECK(!app.canvas.inverted && !app.force_refresh);
    CHECK(app.refresh_pages==15 && app.refresh_mode==0 && app.ui_refresh_pages==20);
    tap(438,610);CHECK(app.ui_refresh_mode==1 && app.refresh_mode==0);
    tap(560,610);CHECK(app.ui_refresh_mode==2 && app.refresh_mode==0);
    tap(298,610);CHECK(app.ui_refresh_mode==0);
    app.force_refresh=false;tap(300,980);CHECK(app.force_refresh && !app.canvas.inverted);
    tap(250,735);CHECK(app.ui_weight==0 && app.ripple_enabled==reader_ripple);
    CHECK(!strcmp(font_display_name("FZSongYJW.TTF"),"方正宋圆"));
    CHECK(!strcmp(font_display_name("fzs3jw.ttf"),"方正宋三"));
    CHECK(!strcmp(font_display_name("FZPingXYSJW.TTF"),"方正屏显雅宋"));
    CHECK(!strcmp(font_display_name("xiawuxinzhisong.ttf"),"霞鹜新致雅宋"));
    CHECK(!strcmp(font_display_name("落霞文楷.ttf"),"落霞文楷.ttf"));
    CHECK(!strcmp(font_display_name("Custom.ttf"),"Custom.ttf"));
    tap(100,1060);CHECK(app.page==THINGSOS_UI_DISPLAY);
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(app.page==THINGSOS_UI_SETTINGS);
    tap(100, 650); /* 74 px settings rows include the log entry. */
    CHECK(app.page == THINGSOS_UI_POWER);
    CHECK(app.lock_wallpaper==0);tap(460,490);CHECK(app.page==THINGSOS_UI_POWER);
    navigate(THINGSOS_UI_APPS);tap(330,680);CHECK(app.page==THINGSOS_UI_WALLPAPER);
    snapshot("wallpaper-sources");tap(120,340);CHECK(app.wallpaper_step==3 && app.lock_wallpaper==0);
    snapshot("wallpaper-stats-preview");host_nvs_available=true;
    tap(500,1140);CHECK(app.page==THINGSOS_UI_APPS && app.lock_wallpaper==0);host_nvs_available=false;
    navigate(THINGSOS_UI_POWER);snapshot("power");
    tap(120, 735); /* The password row precedes the compact power actions. */
    CHECK(app.asleep && app.page == THINGSOS_UI_POWER);
    canvas_snapshot("lock");
    tap(300, 1100);capacitive_key(THINGSOS_KEY_HOME);CHECK(app.asleep);
    power_action(1);
    CHECK(!app.asleep && app.page == THINGSOS_UI_POWER);
    navigate(THINGSOS_UI_FONTS);
    font_preview(-1);
    void* original = app.font.face;
    app.page=THINGSOS_UI_READING;app.menu=true;strcpy(app.wd_id,"55");
    bool note_opened=app.opened;uint64_t note_book=app.runtime.location.book_id;
    bool note_sd=host_sd;host_sd=true;
    app.opened=true;app.runtime.location.book_id=99881;strcpy(app.wd_owner,"0123456789abcdef0123456789abcdef");
    CHECK(reading_menu_press(300,1160) && app.annotation_open);
    tap(80,150);CHECK(!app.annotation_open);app.menu=true;
    reader_annotations_close();app.opened=note_opened;app.runtime.location.book_id=note_book;
    host_sd=note_sd;
    CHECK(reading_menu_press(180,1160)); /* One contact enters; release is consumed by the input owner. */
    CHECK(app.page==THINGSOS_UI_FONTS && !app.menu && app.font.face==original);
    app.wd_id[0]=0;
    app.font_count=4;
    strcpy(app.fonts[0],"FZSongYJW.TTF");strcpy(app.fonts[1],"FZS3JW.TTF");
    strcpy(app.fonts[2],"FZPingXYSJW.TTF");strcpy(app.fonts[3],"xiawuxinzhisong.ttf");
    snapshot("fonts");
    CHECK(app.canvas.pixels[1015u*684+36]==0 && app.canvas.pixels[1082u*684+647]==0);
    tap(342,1050);CHECK(app.page==THINGSOS_UI_FONTS); /* Gap does not apply or cancel. */
    CHECK(app.canvas.pixels[302u*684+50]==232); /* Selected name has a visible background. */
    char saved_font_path[320];strlcpy(saved_font_path,app.font_path,sizeof(saved_font_path));
    strlcpy(app.font_path,"/sdcard/fonts/FZSongYJW.TTF",sizeof(app.font_path));
    thingsos_font_io_t before_font_preview,after_font_preview;
    CHECK(thingsos_font_io(&app.font,&before_font_preview));
    font_preview_current();
    CHECK(app.font_candidate==0 && !app.preview_font.face && app.font.face==original);
    CHECK(!strcmp(app.preview_path,app.font_path));
    CHECK(thingsos_font_io(&app.font,&after_font_preview));
    CHECK(before_font_preview.requests==after_font_preview.requests && before_font_preview.disk_reads==after_font_preview.disk_reads);
    snapshot("fonts-selected-custom");
    CHECK(app.canvas.pixels[402u*684+50]==232);
    strlcpy(app.font_path,saved_font_path,sizeof(app.font_path));font_preview(-1);
    CHECK(!strcmp(app.fonts[0],"FZSongYJW.TTF")); /* Labels never mutate SD paths. */
    app.font_count=0;
    tap(100, 1070);
    CHECK(app.font.face == original);
    navigate(THINGSOS_UI_FONTS);
    font_preview(-1);
    tap(500, 1070);
    CHECK(app.font.face == original && !app.preview_font.face);
    /* Font family, cache and persisted choice are independent in both directions. */
    {
        bool retained_nvs=host_nvs_available;host_nvs_available=true;
        thingsos_font_t retained_reader=app.font,retained_ui=app.ui_font;
        char reader_path[320],ui_path[320];strlcpy(reader_path,app.font_path,320);strlcpy(ui_path,app.ui_font_path,320);
        memset(&app.font,0,sizeof(app.font));memset(&app.ui_font,0,sizeof(app.ui_font));
        CHECK(thingsos_board_font(&app.font)&&thingsos_board_font(&app.ui_font));
        void* body_face=app.font.face;unsigned body_px=app.px,system_px=app.ui_px;
        navigate(THINGSOS_UI_SETTINGS);tap(100,200);CHECK(!app.font_from_reader);
        CHECK(thingsos_board_font(&app.preview_font));strcpy(app.preview_path,"/sdcard/fonts/system-only.ttf");
        app.pagination_valid=true;app.history_count=3;app.progress_total=123;
        tap(500,1050);CHECK(app.font.face==body_face&&app.pagination_valid&&app.history_count==3&&app.progress_total==123&&app.px==body_px);
        CHECK(!strcmp(app.ui_font_path,"/sdcard/fonts/system-only.ttf")&&!strcmp(app.font_path,reader_path));
        void* system_face=app.ui_font.face;
        navigate(THINGSOS_UI_READING);app.menu=true;tap(180,1160);CHECK(app.font_from_reader);
        CHECK(thingsos_board_font(&app.preview_font));strcpy(app.preview_path,"/sdcard/fonts/reading-only.ttf");
        tap(500,1050);CHECK(app.ui_font.face==system_face&&app.ui_px==system_px&&!app.pagination_valid);
        CHECK(!strcmp(app.font_path,"/sdcard/fonts/reading-only.ttf")&&!strcmp(app.ui_font_path,"/sdcard/fonts/system-only.ttf"));
        CHECK(!strcmp(host_font_saved[0],app.font_path)&&!strcmp(host_font_saved[1],app.ui_font_path));
        thingsos_font_close(&app.font);thingsos_font_close(&app.ui_font);app.font=retained_reader;app.ui_font=retained_ui;
        strcpy(app.font_path,reader_path);strcpy(app.ui_font_path,ui_path);CHECK(preferences_save());
        host_nvs_available=retained_nvs;
        app.history_count=0;app.progress_total=0;
    }
    release_book();
    host_sd=true;
    char reading_cover[700];
    for(unsigned variant=0;variant<2;++variant) {
        snprintf(reading_cover,sizeof(reading_cover),"%s/pack-limit/微读-%u.epub",fixture_root,variant?56:55);
        CHECK(thingsos_epub_open(&app.epub,reading_cover));thingsos_epub_navigation(&app.epub);app.epub_open=true;
        if(!variant) {
            snprintf(app.reader_cover_path,sizeof(app.reader_cover_path),"%s/covers/missing-weidu-cover.jpg",fixture_root);
            CHECK(load_chapter(0) && !app.chapter.image_count && !app.chapter.cover_image);
            strlcpy(app.reader_cover_path,reading_cover,sizeof(app.reader_cover_path));
            CHECK(load_chapter(0) && !app.chapter.image_count && !app.chapter.cover_image);
        }
        snprintf(app.reader_cover_path,sizeof(app.reader_cover_path),"%s/covers/55.jpg",fixture_root);
        CHECK(load_chapter(0) && app.chapter.image_count==1);
        unsigned char* pixels=NULL;unsigned w=0,h=0;
        CHECK(image_load(&app.chapter.images[0],&pixels,&w,&h) && w && h);free(pixels);
        app.opened=true;app.start=0;app.px=30;app.menu=false;app.page=THINGSOS_UI_READING;
        app.read_title[0]=0;app.read_path[0]=0;app.indexed_px=app.px;app.indexed_line=app.line_choice;
        app.indexed_margin=app.margin_choice;app.indexed_weight=app.weight_choice;
        app.page_cache_key=0;
        snapshot(variant?"reading-cover-packed":"reading-cover-legacy");
        CHECK(app.chapter.cover_image==1 && app.image_hit_count==1);
        CHECK(app.image_hits[0].h>=900 && app.image_hits[0].y>=90 && app.image_hits[0].y+app.image_hits[0].h<=1130);
        /* Heading removal must not skip the image's newline anchor. */
        release_book();
    }
    app.reader_cover_path[0]=0;
    snprintf(reading_cover,sizeof(reading_cover),"%s/reading-cover-local.epub",fixture_root);
    CHECK(thingsos_epub_open(&app.epub,reading_cover));thingsos_epub_navigation(&app.epub);app.epub_open=true;
    CHECK(load_chapter(0) && app.chapter.cover_image==1);
    app.opened=true;app.start=0;app.px=46;app.page=THINGSOS_UI_READING;app.menu=false;
    snapshot("reading-cover-local-large");CHECK(app.image_hit_count==1 && app.image_hits[0].h>=1000);
    unsigned left_ink=684,right_ink=0,top_ink=1216,bottom_ink=0;
    for(unsigned yy=90;yy<1130;++yy)for(unsigned xx=0;xx<684;++xx)if(app.canvas.pixels[yy*684+xx]<180){if(xx<left_ink)left_ink=xx;if(xx>right_ink)right_ink=xx;if(yy<top_ink)top_ink=yy;if(yy>bottom_ink)bottom_ink=yy;}
    CHECK(right_ink-left_ink>550 && bottom_ink-top_ink>900 && abs((int)(left_ink+right_ink)-683)<=2);
    app.pagination_started=false;CHECK(pagination_prepare());
    while(!app.pagination_valid && !app.pagination_failed)CHECK(pagination_step());
    CHECK(app.pagination_valid && app.page_count>=2 && app.pages[0].chapter==0 && app.pages[1].chapter==1);
    tap((int)(left_ink+right_ink)/2,(int)(top_ink+bottom_ink)/2);CHECK(!app.image_view.open && app.menu);app.menu=false;
    CHECK(load_chapter(1) && !app.chapter.cover_image);
    app.landscape_enabled=true;app.start=0;draw();
    CHECK(!app.image_hit_count && app.end==app.chapter.images[0].offset);
    next_page(true);
    CHECK(app.start==app.chapter.images[0].offset && app.runtime.location.byte_offset==app.start);
    snapshot("reading-figure-fit-landscape");
    CHECK(app.image_hit_count==1 && app.image_hits[0].w<reader_body_width() && app.image_hits[0].h<reader_body_height());
    CHECK(app.image_hits[0].w==app.image_hits[0].h*200/300);
    int figure_middle=app.image_hits[0].x+app.image_hits[0].w/2;
    int figure_top=app.image_hits[0].y,figure_bottom=figure_top+app.image_hits[0].h-1;
    CHECK(app.canvas.pixels[(size_t)figure_top*app.canvas.width+figure_middle]<90);
    CHECK(app.canvas.pixels[(size_t)figure_bottom*app.canvas.width+figure_middle]<90);
    app.landscape_enabled=false;snapshot("reading-cover-ordinary-figure");
    CHECK(app.image_hit_count==1 && app.image_hits[0].w==reader_body_width() && app.image_hits[0].h>294);
    CHECK(app.canvas.pixels[(size_t)(app.image_hits[0].y+10)*app.canvas.width+app.image_hits[0].x]<90);
    size_t image_return=app.start;
    int hold_x=app.image_hits[0].x+app.image_hits[0].w/2,hold_y=app.image_hits[0].y+50;
    tap(hold_x,hold_y);CHECK(!app.image_view.open&&!app.menu&&app.start==image_return);
    CHECK(reader_hold_delay(hold_x,hold_y)==1000);
    CHECK(!reader_content_hold(hold_x,hold_y,999)&&!app.image_view.open&&!app.annotation_selecting);
    CHECK(reader_content_hold(hold_x,hold_y,1000));
    CHECK(app.image_view.open && app.image_view.zoom_percent==100);
    thingsos_size_t width_fit;CHECK(thingsos_image_fit_width(app.image_w,app.image_h,reader_width()-72,&width_fit));
    CHECK(width_fit.width==(unsigned)reader_width()-72);
    thingsos_image_pan(&app.image_view,0,INT_MAX,width_fit.width,width_fit.height,reader_width()-72,reader_height()-426);
    CHECK(app.image_view.pan_y==(int)width_fit.height-(reader_height()-426));
    tap(60,130);CHECK(!app.image_view.open && app.start==image_return);
    reader_image_hold_layout_test();
    release_book();
    app.px = 30;
    host_sd = true;
    char path[600];
    snprintf(path, sizeof(path), "%s/valid.epub", fixture_root);
    CHECK(thingsos_epub_open(&app.epub, path));
    thingsos_epub_navigation(&app.epub);
    app.epub_open = true;
    CHECK(load_chapter(0));
    app.opened = true;
    app.runtime.location = (thingsos_location_t){ 42, 0, 0 };
    strlcpy(app.read_title, app.epub.title, sizeof(app.read_title));
    strlcpy(app.read_path, "/sdcard/books/test.epub", sizeof(app.read_path));
    CHECK(thingsos_records_book(&app.records, 42, app.read_path, app.read_title));
    CHECK(pagination_step());
    CHECK(app.page_count == 1 && !app.pagination_valid);
    snapshot("reading-indexing");
    /* A repeated draw/open must reuse decoded pixels without reading the ZIP;
       the returned allocations remain independently owned by each caller. */
    CHECK(app.chapter.image_count);reader_image_cache_clear();
    unsigned char *image_first=NULL,*image_second=NULL;unsigned image_w1,image_h1,image_w2,image_h2;
    CHECK(image_load(app.chapter.images,&image_first,&image_w1,&image_h1));
    size_t image_entries=app.epub.zip.count;app.epub.zip.count=0;
    CHECK(image_load(app.chapter.images,&image_second,&image_w2,&image_h2));app.epub.zip.count=image_entries;
    CHECK(image_first!=image_second && image_w1==image_w2 && image_h1==image_h2 &&
          !memcmp(image_first,image_second,(size_t)image_w1*image_h1));
    image_first[0]^=255;CHECK(image_first[0]!=image_second[0]);free(image_first);free(image_second);
    reader_image_cache_clear();CHECK(!reader_images[0].pixels && !reader_images[1].pixels);
    unsigned battery_saved=app.battery;
    for(unsigned percent=1;percent<=100;percent=percent==1?9:percent==9?99:100) {
        app.battery=percent;draw_status();int right=-1;
        for(int y=15;y<62;++y)for(int x=508;x<620;++x)
            if(app.canvas.pixels[(size_t)y*684+x]<128 && x>right)right=x;
        CHECK(right>=602 && right<=612);
        if(percent==100)break;
    }
    app.battery=100;
    bool wifi_saved=host_network_connected,hotspot_saved=app.hotspot;
    host_network_connected=true;snapshot("status-wifi-connected");
    unsigned char* online_status=malloc(684u*74u);CHECK(online_status);
    memcpy(online_status,app.canvas.pixels,684u*74u);
    host_network_connected=false;snapshot("status-wifi-disconnected");
    unsigned changed=0;
    for(unsigned y=0;y<74;++y)for(unsigned x=0;x<684;++x) {
        bool different=online_status[y*684+x]!=app.canvas.pixels[y*684+x];
        if(different) {CHECK(x>=460 && x<540 && y>=24 && y<48);++changed;}
    }
    CHECK(changed>40);free(online_status);
    app.hotspot=true;draw_status();
    CHECK(!status_wifi_ink(5.5f,3.5f,true) && status_wifi_ink(5.5f,3.5f,false));
    host_network_connected=wifi_saved;app.hotspot=hotspot_saved;
    app.battery=battery_saved;draw();
    CHECK(app.canvas.pixels[24u*684+620]==0 && app.canvas.pixels[30u*684+670]==0);
    size_t marks_before_status_tap=app.runtime.bookmark_count;
    tap(660,35);
    CHECK(app.runtime.bookmark_count==marks_before_status_tap && !app.menu);
    CHECK(app.end > 0);
    size_t first_bytes = app.chapter.bytes;
    app.start = first_bytes / 2;
    unsigned middle_progress = book_progress();
    CHECK(middle_progress > 0 && middle_progress < 100);
    app.start = 0;
    CHECK(book_progress() == 0);
    size_t indexed_pages = app.page_count;
    next_page(true);
    CHECK(app.page_count == indexed_pages && !app.pagination_valid);
    CHECK(pagination_step());
    CHECK(app.page_count > indexed_pages);
    CHECK(load_chapter(0));
    app.start = 0;
    CHECK(paginate());
    CHECK(app.page_count >= 2);
    /* Cross-chapter construction must not change live position, text or image hits. */
    unsigned cache_chapter=app.chapter_index;
    char* cache_text=app.chapter.text;
    size_t cache_start=app.start;
    CHECK(reader_cache_build(0,(thingsos_page_position_t){1,0}));
    CHECK(reader_frames[0].valid && app.chapter_index==cache_chapter &&
        app.chapter.text==cache_text && app.start==cache_start);
    CHECK(reader_cache_build(1,(thingsos_page_position_t){0,0}));
    CHECK(!reader_frames[1].valid); /* This page includes an image; retain decoder path. */
    reader_cache_clear();
    navigate(THINGSOS_UI_READING);
    snapshot("reading");
    unsigned date_ink=0,title_ink=0;
    for(int yy=0;yy<74;++yy) {
        for(int xx=28;xx<218;++xx)date_ink+=app.canvas.pixels[(size_t)yy*684+xx]<128;
        for(int xx=222;xx<502;++xx)title_ink+=app.canvas.pixels[(size_t)yy*684+xx]<128;
    }
    CHECK(date_ink>0 && title_ink>0);
    size_t saved_start = app.start;
    unsigned suspends_before=host_suspend,resumes_before=host_resume;
    power_action(1);
    CHECK(app.asleep && host_suspend == suspends_before+1 && !app.dirty);
    CHECK(host_sleep_body_ink>500 && !region_ink(0,74) && !region_ink(1098,1216) && app.start==saved_start);
    canvas_snapshot("sleep-from-reader");
    power_action(1);
    CHECK(
        !app.asleep && host_resume == resumes_before+1 && app.page == THINGSOS_UI_READING &&
        app.start == saved_start);
    CHECK(app.lock_wave_pending&&!app.lock_wave_forward&&app.force_refresh);
    snapshot("reader-after-wake");
    /* A failed wake scan must retry the same reverse lock cleanup, never
       drop to an ordinary reader/background update and retain wallpaper. */
    unsigned reader_wake_presents=host_lock_presents;
    host_lock_result=ESP_FAIL;
    CHECK(present_lock_transition()==ESP_FAIL&&app.lock_wave_pending);
    host_lock_result=ESP_OK;
    CHECK(present_lock_transition()==ESP_OK&&!app.lock_wave_pending&&!host_lock_forward);
    CHECK(host_lock_presents==reader_wake_presents+2&&app.start==saved_start);
    host_suspend_failure=1;
    power_action(1);
    CHECK(!app.asleep && app.dirty && app.page==THINGSOS_UI_READING && app.start==saved_start);
    host_suspend_failure=0;
    snapshot("reader-after-sleep-failure");
    tap(30, 35);
    CHECK(app.page == THINGSOS_UI_READING && !app.menu);
    tap(342, 500);
    CHECK(!app.image_view.open&&!app.menu);
    CHECK(!reader_image_hold(342,500,999));
    CHECK(reader_image_hold(342,500,1000)&&app.image_view.open);
    tap(60,130);CHECK(!app.image_view.open);
    capacitive_key(THINGSOS_KEY_MENU);
    CHECK(app.menu);
    tap(200, 140);
    CHECK(app.page == THINGSOS_UI_READING && !app.menu); /* Old lowered region only dismisses. */
    capacitive_key(THINGSOS_KEY_MENU);
    snapshot("reader-back-top-left");
    CHECK(app.canvas.pixels[36u*684+40]==0 && app.canvas.pixels[174u*684+24]==255);
    tap(40,35);
    CHECK(app.page == THINGSOS_UI_LIBRARY && !app.menu);
    navigate(THINGSOS_UI_READING);
    CHECK(go_page(app.page_count - 1));
    next_page(false);
    CHECK(current_page() == app.page_count - 2);
    size_t last = app.page_count - 1;
    CHECK(go_page(last));
    app.history_count = 0;
    app.pagination_valid=false;
    app.pagination_chapter=(unsigned)app.epub.spine_count;
    next_page(false);
    app.pagination_valid=true;
    CHECK(current_page() == last - 1);
    app.start = app.chapter.bytes;
    CHECK(current_page() < app.page_count);
    CHECK(go_page(0));
    feature_toggle_mark();
    app.runtime.bookmarks[app.runtime.bookmark_count++] = (thingsos_location_t){ 999, 0, 0 };
    navigate(THINGSOS_UI_BOOKMARKS);
    CHECK(mark_index(0) != SIZE_MAX && mark_index(1) == SIZE_MAX);
    snapshot("bookmarks");
    size_t mark_total=app.runtime.bookmark_count;
    tap(100,378);tap(525,530);
    CHECK(app.page==THINGSOS_UI_BOOKMARKS && app.runtime.bookmark_count==mark_total);
    tap(590,530);CHECK(app.runtime.bookmark_count==mark_total-1 && mark_index(0)==SIZE_MAX);
    snapshot("bookmarks-empty");
    tap(300,430);CHECK(app.runtime.bookmark_count==mark_total && mark_index(0)!=SIZE_MAX);
    navigate(THINGSOS_UI_CHAPTERS);
    snapshot("chapters");
    tap(100, 490);
    CHECK(app.page == THINGSOS_UI_READING && app.chapter_index == 1);
    app.book_count = 1;
    strlcpy(app.books[0].name, app.read_title, sizeof(app.books[0].name));
    app.books[0].id = 42;
    app.books[0].epub = true;
    strlcpy(app.books[0].path, path, sizeof(app.books[0].path));
    app.recent_count = 1;
    app.recent_ids[0] = 42;
    CHECK(open_book(0));
    CHECK(app.pagination_valid && app.page_count>=2); /* Persistent index reuse. */
    CHECK(!strcmp(app.epub.chapter_names[0],"第二章"));
    unsigned cached_size=app.px;
    app.px=thingsos_reader_size_step(app.px,true);
    CHECK(pagination_prepare() && !app.pagination_valid && app.page_count==0);
    app.px=cached_size;
    CHECK(pagination_prepare() && app.pagination_valid && app.page_count>=2);
    uint64_t font_signature=app.font.signature;
    app.font.signature^=1;app.pagination_started=false;
    CHECK(pagination_prepare() && !app.pagination_valid);
    app.font.signature=font_signature;app.pagination_started=false;
    CHECK(pagination_prepare() && app.pagination_valid);
    app.epub.zip.entries[0].crc^=1;app.pagination_started=false;
    CHECK(pagination_prepare() && !app.pagination_valid);
    app.epub.zip.entries[0].crc^=1;app.pagination_started=false;
    CHECK(pagination_prepare() && app.pagination_valid);
    size_t real_chapters=app.epub.spine_count;
    CHECK(thingsos_epub_reserve_chapters(&app.epub,10));
    app.epub.spine_count=10;
    for(size_t i=real_chapters;i<10;++i)
        snprintf(app.epub.chapter_names[i],160,"滚动测试章节 %u",(unsigned)i+1);
    unsigned toc_saved_chapter=app.chapter_index;
    app.chapter_index=5;navigate(THINGSOS_UI_CHAPTERS);
    CHECK(app.list_start==2); /* Current chapter occupies the middle row. */
    app.chapter_index=toc_saved_chapter;
    navigate(THINGSOS_UI_CHAPTERS);
    snapshot("chapters-scrollbar");
    CHECK(app.canvas.pixels[350u*684+650]==0);
    tap(650,949);
    CHECK(app.list_start==4);
    snapshot("chapters-scrollbar-end");
    CHECK(app.canvas.pixels[949u*684+650]==0);
    const char* toc_names[]={"第3章 市电电源及引入","3.1 公共电网供电及可靠性","3.1.1 公共电网供电现状","3.1.2 公共电网供电可靠性","3.2 电力设施","3.2.1 变电站与开关站"};
    char saved_names[6][160];uint8_t saved_levels[6];
    for(unsigned i=0;i<6;++i){strlcpy(saved_names[i],app.epub.chapter_names[i],160);saved_levels[i]=app.epub.chapter_levels[i];strlcpy(app.epub.chapter_names[i],toc_names[i],160);app.epub.chapter_levels[i]=(uint8_t)(i==0?0:i==1||i==4?1:2);}
    app.list_start=0;snapshot("chapters-hierarchy");
    for(unsigned i=0;i<6;++i){strlcpy(app.epub.chapter_names[i],saved_names[i],160);app.epub.chapter_levels[i]=saved_levels[i];}
    toc_current_ui_test();
    app.epub.spine_count=real_chapters;
    {
        book_t original_book=app.books[0];
        snprintf(app.books[0].path,sizeof(app.books[0].path),"%s/hierarchy-nav.epub",fixture_root);app.books[0].id=778899;
        CHECK(open_book(0) && app.epub.spine_count==4 && app.epub.chapter_levels[2]==2);
        navigate(THINGSOS_UI_CHAPTERS);snapshot("chapters-hierarchy-real");
        tap(150,590);CHECK(app.page==THINGSOS_UI_READING && app.chapter_index==2 && strstr(app.chapter.text,"ONLY_2") && !strstr(app.chapter.text,"ONLY_1"));
        /* A section target is a byte position, not a new source chapter. */
        app.epub.section_toc=calloc(2,sizeof(*app.epub.section_toc));CHECK(app.epub.section_toc);
        app.epub.section_toc_count=2;strcpy(app.epub.section_toc[0].title,"Parent");
        thingsos_toc_entry_t* section=&app.epub.section_toc[1];section->chapter=2;section->level=1;
        section->offset=(uint32_t)(strstr(app.chapter.text,"ONLY_2")-app.chapter.text);strcpy(section->title,"Section target");
        uint32_t expected_offset=section->offset;
        app.pagination_valid=false;navigate(THINGSOS_UI_CHAPTERS);CHECK(reader_toc_count()==2);
        snapshot("chapters-section-target");tap(150,490);
        CHECK(app.page==THINGSOS_UI_READING && app.chapter_index==2 && app.start==expected_offset && app.epub.spine_count==4);
        release_book();app.books[0]=original_book;CHECK(open_book(0));
    }
    /* Footnote markers: tapping one opens an in-page window showing the note,
       tapping blank space (or turning the page) closes it again. */
    {
        char saved_path[sizeof(app.books[0].path)];
        unsigned saved_id = app.books[0].id;
        strlcpy(saved_path, app.books[0].path, sizeof(saved_path));
        app.books[0].id = 4242; /* a fresh id: no saved position, no page cache */
        snprintf(app.books[0].path, sizeof(app.books[0].path), "%s/footnote.epub", fixture_root);
        CHECK(open_book(0));
        navigate(THINGSOS_UI_READING);
        /* WeRead owns taps while its first-run consent or a panel is up. */
        if (app.wd_consent) { tap(100, 880); CHECK(!app.wd_consent); }
        app.wd_cloud_prompt = app.wd_notes_open = app.wd_sync_panel = app.wd_long_open = false;
        draw();
        CHECK(app.chapter_index == 0);
        CHECK(app.chapter.footnote_count == 2 && app.text_hits.count == 2);
        const thingsos_text_hit_t* box = app.text_hits.items;
        int marker_x = box->x + box->w / 2, marker_y = box->y + box->h / 2;
        tap(marker_x, marker_y);
        CHECK(app.footnote_open && app.footnote_index == 0 && app.footnote_offset == 0);
        snapshot("footnote-window");
        CHECK(region_ink(300, 700) > 0);
        tap(300, 150); /* blank space above the window closes it */
        CHECK(!app.footnote_open);
        tap(marker_x, marker_y);
        CHECK(app.footnote_open);
        tap(660, 1100); /* blank space beside the window closes it */
        CHECK(!app.footnote_open);
        /* The long note needs several window pages: tapping inside advances. */
        box = app.text_hits.items + 1;
        tap(box->x + box->w / 2, box->y + box->h / 2);
        CHECK(app.footnote_open && app.footnote_index == 1);
        draw();
        size_t first_page = app.footnote_next;
        CHECK(first_page > 0 && first_page < app.chapter.footnotes[1].note_bytes);
        snapshot("footnote-window-paged");
        tap(300, 400);
        CHECK(app.footnote_open && app.footnote_offset == first_page);
        tap(300, 150);
        CHECK(!app.footnote_open);
        tap(100, 600); /* turning the page dismisses the window */
        CHECK(!app.footnote_open);
        /* On a page without markers a body tap stays an ordinary page turn. */
        tap(600, 700);
        CHECK(!app.footnote_open);
        snprintf(app.books[0].path,sizeof(app.books[0].path),"%s/footnote-compat.epub",fixture_root);app.books[0].id=4343;
        CHECK(open_book(0));app.wd_cloud_prompt=app.wd_notes_open=app.wd_sync_panel=app.wd_long_open=app.wd_consent=false;draw();
        CHECK(app.chapter.footnote_count==3 && app.text_hits.count==3 && !app.chapter.image_count);
        for(unsigned i=0;i<3;++i) {
            thingsos_text_hit_t target=app.text_hits.items[i];size_t position=app.start;
            tap(target.x+target.w/2,target.y+target.h/2);
            CHECK(app.footnote_open && app.footnote_index==i && app.start==position);
            if(i==1)snapshot("footnote-icon-fixed");else draw();
            tap(300,150);CHECK(!app.footnote_open);
        }
        app.chapter.footnotes[1].note_bytes=0;draw();thingsos_text_hit_t target=app.text_hits.items[1];size_t position=app.start;
        tap(target.x+target.w/2,target.y+target.h/2);CHECK(!app.footnote_open && app.start==position && strstr(app.message,"脚注"));
        strlcpy(app.books[0].path, saved_path, sizeof(app.books[0].path));
        app.books[0].id = saved_id;
        CHECK(open_book(0));
    }
    /* Sorting includes all books and maps list/card hits to the same book. */
    size_t saved_count = app.book_count;
    book_t saved_books[BOOK_LIMIT];
    memcpy(saved_books, app.books, sizeof(saved_books));
    app.book_count = 12;
    for (size_t i = 0; i < app.book_count; ++i) {
        memset(&app.books[i], 0, sizeof(app.books[i]));
        app.books[i].id = 1000 + i;
        snprintf(app.books[i].name, sizeof(app.books[i].name), "%02u 这是一个超出卡片宽度的长书名", (unsigned)(12 - i));
        snprintf(app.books[i].path, sizeof(app.books[i].path), "/sdcard/books/%u.txt", (unsigned)i);
        thingsos_book_record_t* r = thingsos_records_book(&app.records, app.books[i].id, app.books[i].path, app.books[i].name);
        CHECK(r); r->order = i + 1;
    }
    navigate(THINGSOS_UI_LIBRARY);
    tap(535, 265);
    CHECK(app.library_name_sort && library_sorted(0) == 11 && library_sorted(11) == 0);
    tap(535, 265);
    CHECK(!app.library_name_sort && library_sorted(0) == 11);
    CHECK(!app.message[0]); /* Sorting must not leave a persistent overlay. */
    thingsos_records_book(&app.records, app.books[0].id, NULL, NULL)->order = 99;
    CHECK(library_sorted(0) == 0);
    tap(610, 265);
    CHECK(app.library_cards && app.list_start == 0);
    host_library_order_reads=0;snapshot("library-cards");
    CHECK(host_library_order_reads==app.book_count); /* One order snapshot per frame, including comparisons. */
    CHECK(app.canvas.pixels[244u * 684 + 597] == 0); /* Card mode offers list icon. */
    CHECK(app.canvas.pixels[241u * 684 + 614] == 255); /* No grid icon simultaneously. */
    CHECK(library_hit(100, 400) == 0 && library_hit(300, 400) == 11 && library_hit(540, 400) == 10);
    CHECK(library_hit(234, 400) == app.book_count);
    app.list_start = 9;
    CHECK(library_hit(100, 400) == 3 && library_hit(100, 700) == app.book_count);
    CHECK(management_hold(100, 400) && app.manage_target == 3);
    navigate(THINGSOS_UI_LIBRARY);
    tap(610, 265);
    CHECK(!app.library_cards && app.list_start == 0 && library_hero() == 0);
    snapshot("library-list-toggle");
    CHECK(app.canvas.pixels[241u * 684 + 614] == 0); /* List mode offers grid icon. */
    CHECK(app.canvas.pixels[244u * 684 + 603] == 255); /* No list icon simultaneously. */
    tap(420, 265); /* Former left toolbar position is inactive. */
    CHECK(!app.library_cards);
    CHECK(library_row(0) == 11);
    book_t offline_saved=app.books[11];
    strcpy(app.books[11].path,"/sdcard/books/微读-123.epub");app.books[11].epub=true;
    app.library_order_valid=false;
    CHECK(library_visible_count()==11);
    tap(390,150);
    CHECK(app.page==THINGSOS_UI_LIBRARY && app.library_weidu && library_visible_count()==1);
    CHECK(library_sorted(0)==11 && library_sorted(1)==app.book_count && library_hit(100,400)==11);
    snapshot("library-weread-list");
    thingsos_weidu_status_t saved_weidu_status=host_weidu;
    host_weidu.authenticated=true;host_weidu.busy=false;app.weidu=host_weidu;
    CHECK(weidu_hold_delay(100,400)==1000 && !weidu_release_hold(100,400,999));
    CHECK(weidu_release_hold(100,400,1000) && app.page==THINGSOS_UI_WEIDU && app.weidu_detail);
    CHECK(!strcmp(app.weidu_books[0].id,"123") && app.weidu_detail_return==THINGSOS_UI_LIBRARY);
    host_weidu.background=true;++host_weidu.serial;weidu_poll();
    CHECK(!strcmp(app.weidu_books[0].id,"123")); /* A background completion cannot change the held book. */
    tap(300,275);CHECK(app.page==THINGSOS_UI_LIBRARY && app.library_weidu);
    host_weidu=app.weidu=saved_weidu_status;
    tap(450,260);CHECK(app.editing==5 && app.page==THINGSOS_UI_PASSWORD);
    thingsos_input_open(&app.input,"不存在",96,false);tap(590,1030);
    CHECK(app.page==THINGSOS_UI_LIBRARY && library_visible_count()==0 && library_sorted(0)==app.book_count);
    snapshot("library-search-empty");
    tap(450,260);thingsos_input_open(&app.input,"长书名",96,false);tap(590,1030);
    CHECK(library_visible_count()==1 && library_hit(100,400)==11);
    tap(450,260);thingsos_input_open(&app.input,"",96,false);tap(590,1030);
    CHECK(!app.library_query[0] && library_visible_count()==1);

    tap(610,265);CHECK(app.library_cards);
    snapshot("library-weread-cards");
    CHECK(app.canvas.pixels[500u*684+60]==0);
    CHECK(management_hold(100,400) && app.manage_count==1 && app.manage_target==0);
    tap(500,330);CHECK(app.page==THINGSOS_UI_LIBRARY && app.library_weidu);
    tap(300,150);CHECK(!app.library_weidu && library_visible_count()==11 && app.list_start==0);
    snapshot("library-local-tab");tap(610,265);CHECK(!app.library_cards);
    app.books[11]=offline_saved;app.library_order_valid=false;
    /* No hidden view or hide action remains; all local covers stay visible. */
    CHECK(!weidu_hold(100,400));
    memcpy(app.books, saved_books, sizeof(saved_books));
    app.book_count = saved_count;
    navigate(THINGSOS_UI_LIBRARY);
    snapshot("library");
    navigate(THINGSOS_UI_RECENT);
    snapshot("recent");
    navigate(THINGSOS_UI_STATS);
    snapshot("stats");
    CHECK(!region_ink(1080,1098));
    tap(300,1070);CHECK(app.page==THINGSOS_UI_STATS);
    statistics_heatmap_test();
    statistics_period_test();
    navigate(THINGSOS_UI_TRANSFER);
    app.service_on = true;
    snapshot("transfer");
    tap(500, 990); CHECK(!app.service_on);
    tap(500, 990); CHECK(app.service_on);
    host_network_busy = true;
    tap(500, 990); CHECK(app.service_on && app.message[0]);
    host_transfer.active = true; host_transfer.percent = 40;
    strlcpy(host_transfer.name, "demo.epub", sizeof(host_transfer.name));
    snapshot("transfer-progress");
    host_network_busy = false; memset(&host_transfer, 0, sizeof(host_transfer));
    app.message[0] = 0;
    CHECK(!strcmp(thingsos_network_ssid(),"ThingsOS") && !strcmp(thingsos_network_password(),"picoread") && strlen(thingsos_network_password())==8);
    unsigned scans_before=host_wifi_scans;
    CHECK(app_controls_swipe(342,30,0,100));
    CHECK(app_hold(150,250,600)); CHECK(app.page == THINGSOS_UI_WIFI);
    CHECK(app.wifi_scan_pending && app.dirty && host_wifi_scans==scans_before);
    wifi_scan_step();CHECK(host_wifi_scans==scans_before); /* Entry frame precedes the radio scan. */
    snapshot("wifi-scanning");app.dirty=false;host_wifi_scan_ready=false;
    wifi_scan_step();CHECK(app.wifi_scan_running && host_wifi_scans==scans_before+1);
    for(unsigned i=0;i<200;++i)wifi_scan_step();
    CHECK(app.wifi_scan_running && !app.dirty && !app.wifi_count);
    navigate(THINGSOS_UI_SETTINGS);draw();app.dirty=false;
    host_wifi_scan_ready=true;wifi_scan_step();
    CHECK(!app.wifi_scan_running && app.wifi_scan_valid && app.wifi_count==1 && !app.dirty);
    navigate(THINGSOS_UI_WIFI);CHECK(!app.wifi_scan_pending && host_wifi_scans==scans_before+1);
    host_network_connected=true;app.wifi_connected=true;app.wifi_requested=now();
    strcpy(host_saved_wifi,"saved-wifi-test");
    unsigned disconnects=host_wifi_disconnects;unsigned reconnects=host_wifi_connects;
    snapshot("wifi-connected-disconnect");
    host_network_busy=true;tap(180,470);CHECK(host_network_connected && host_wifi_disconnects==disconnects);
    host_network_busy=false;host_wifi_disconnect_error=ESP_ERR_INVALID_STATE;tap(180,470);
    CHECK(host_network_connected && app.wifi_requested);host_wifi_disconnect_error=0;
    tap(180,470);CHECK(!host_network_connected && !app.wifi_requested && host_wifi_disconnects==disconnects+2);
    CHECK(thingsos_network_saved_name()[0]);snapshot("wifi-disconnected-saved");
    tap(180,470);CHECK(host_wifi_connects==reconnects+1 && app.wifi_requested);
    /* Reported defect: music playing (or merely stopped-but-claimed) made the
       WiFi page answer "正在传输". An audio session is not a transfer, so the
       same tap must still reach the network layer. */
    {   thingsos_audio_status_t saved_audio=host_audio;
        bool saved_connected=host_network_connected;
        host_audio.state=THINGSOS_AUDIO_PLAYING;host_network_busy=true;
        host_network_connected=true;app.message[0]=0;
        unsigned before=host_wifi_disconnects;
        tap(180,470);
        CHECK(host_wifi_disconnects==before+1 && !host_network_connected && !strstr(app.message,"传输"));
        host_network_connected=true;app.message[0]=0;
        host_audio.state=THINGSOS_AUDIO_STOPPING;   /* stop in flight keeps the claim */
        tap(180,470);
        CHECK(host_wifi_disconnects==before+2 && !strstr(app.message,"传输"));
        /* A real transfer with audio idle still refuses, unchanged. */
        host_network_connected=true;app.message[0]=0;host_audio.state=THINGSOS_AUDIO_IDLE;
        tap(180,470);
        CHECK(host_wifi_disconnects==before+2 && strstr(app.message,"传输"));
        host_audio=saved_audio;host_network_busy=false;host_network_connected=saved_connected;
        app.message[0]=0;
    }
    app.wifi_requested=0;host_saved_wifi[0]=0;host_wifi_connects=reconnects;
    CHECK(!strcmp(app.wifi_networks[0].ssid,"异步测试网络"));
    wifi_scan(true);wifi_scan(true);draw();app.dirty=false;host_wifi_scan_ready=false;
    wifi_scan_step();CHECK(host_wifi_scans==scans_before+2 && app.wifi_count==1);
    host_wifi_scan_result_error=ESP_ERR_INVALID_STATE;host_wifi_scan_ready=true;
    wifi_scan_step();CHECK(!app.wifi_scan_running && app.wifi_count==1 && app.message[0]);
    host_wifi_scan_result_error=ESP_OK;app.message[0]=0;
    app.wifi_scan_at=now()-30001;navigate(THINGSOS_UI_TRANSFER);navigate(THINGSOS_UI_WIFI);
    CHECK(app.wifi_scan_pending);draw();app.dirty=false;host_wifi_scan_start_error=ESP_ERR_INVALID_STATE;
    wifi_scan_step();CHECK(!app.wifi_scan_pending && !app.wifi_scan_running && app.wifi_count==1);
    host_wifi_scan_start_error=ESP_OK;app.message[0]=0;
    app.wifi_count = 8;
    for (size_t i = 0; i < 8; ++i) {
        snprintf(app.wifi_networks[i].ssid, sizeof(app.wifi_networks[i].ssid), "Network-%u", (unsigned)i);
        app.wifi_networks[i].rssi = -45 - (int)i * 5;
        app.wifi_networks[i].open = i == 7;
    }
    snapshot("wifi-networks");
    tap(650, 815); CHECK(app.list_start == 4);
    tap(100, 760); CHECK(app.wifi_requested && host_wifi_connects == 1 && !app.hotspot);
    app.list_start = 0; app.wifi_requested = 0;
    tap(100, 400); CHECK(app.page == THINGSOS_UI_PASSWORD && app.editing == 1 && app.wifi_join_after);
    snapshot("wifi-password");
    CHECK(wifi_key_hit(60, 770) == 100 + 'q' && wifi_key_hit(50, 860) == 100+'a');
    tap(60, 770); CHECK(!strcmp(app.input.text, "q"));
    tap(100, 470); CHECK(app.input.reveal);
    tap(420, 470); CHECK(!app.input.length);
    tap(60, 950); CHECK(app.input.mode == THINGSOS_KEYS_UPPER);
    tap(60, 770); CHECK(!strcmp(app.input.text, "Q"));
    tap(48, 320); CHECK(app.input.cursor == 0);
    tap(90, 338); CHECK(app.input.cursor == 1);
    tap(610, 950); CHECK(!app.input.length);
    tap(60, 770); CHECK(!strcmp(app.input.text, "Q"));
    CHECK(keyboard_mode_press(60,1030)); CHECK(app.input.mode == THINGSOS_KEYS_SYMBOLS && app.keyboard_refresh);
    CHECK(!keyboard_mode_press(580,1030)); /* Submit still waits for release. */
    snapshot("wifi-password-numbers");
    CHECK(keyboard_mode_press(60,770)); CHECK(!strcmp(app.input.text, "Q1") && !app.keyboard_refresh && app.input_refresh);
    tap(140, 1030); CHECK(app.wifi_symbols_more && app.keyboard_refresh);
    tap(60, 770); CHECK(!strcmp(app.input.text, "Q1["));
    tap(60, 1030); CHECK(app.input.mode == THINGSOS_KEYS_LOWER && app.keyboard_refresh);
    tap(480, 1030); CHECK(app.input.chinese);
    CHECK(thingsos_ime_letter(&app.input, 'z') && thingsos_ime_letter(&app.input, 'h') && thingsos_ime_letter(&app.input, 'o') && thingsos_ime_letter(&app.input, 'n') && thingsos_ime_letter(&app.input, 'g'));
    snapshot("wifi-password-chinese");
    tap(70, 730); CHECK(!app.input.composition[0] && app.input.length > 3);
    tap(420, 470); CHECK(!app.input.length);
    tap(500, 590); CHECK(app.page == THINGSOS_UI_PASSWORD && app.message[0]);
    CHECK(thingsos_input_insert(&app.input, "valid-test-pass"));
    tap(500, 590); CHECK(app.page == THINGSOS_UI_WIFI && app.wifi_requested && host_wifi_connects == 2);
    CHECK(!app.input.text[0] && !app.wifi_password[0]);
    host_network_connected = true; strlcpy(host_saved_wifi, "Network-0", sizeof(host_saved_wifi));
    app.wifi_requested = 0; app.list_start = 0;
    host_network_connected=false;
    snapshot("wifi-saved");unsigned connects_before=host_wifi_connects;
    tap(100,470);CHECK(app.page==THINGSOS_UI_WIFI && host_wifi_connects==connects_before+1);
    tap(100,620);CHECK(app.page==THINGSOS_UI_WIFI && host_wifi_connects==connects_before+2);
    host_wifi_connects=connects_before;host_network_connected=true;
    snapshot("wifi-connected");
    tap(650, 797); CHECK(app.list_start == 6);
    tap(500, 470); CHECK(app.confirm_action == 7);
    tap(100, 1100); CHECK(!app.confirm_action);
    tap(100, 1010); CHECK(app.editing == 0 && app.page == THINGSOS_UI_PASSWORD);
    thingsos_input_clear(&app.input);
    tap(500, 590); CHECK(app.page == THINGSOS_UI_PASSWORD);
    CHECK(thingsos_input_insert(&app.input, "Hidden-Network"));
    tap(500, 590); CHECK(app.page == THINGSOS_UI_PASSWORD && app.editing == 1);
    CHECK(!app.wifi_requires_password);
    tap(100, 590); CHECK(app.page == THINGSOS_UI_WIFI && !app.wifi_join_after);
    tap(500, 890); CHECK(app.page == THINGSOS_UI_TRANSFER && !app.hotspot && !host_hotspot);
    host_network_connected = false; host_saved_wifi[0] = 0;
    memcpy(app.books, saved_books, sizeof(saved_books));
    app.book_count = saved_count; /* Host scan has no mounted /sdcard fixture. */

    navigate(THINGSOS_UI_TIME);
    snapshot("time");
    tap(400, 460);
    CHECK(!app.auto_time);
    tap(400, 600);
    CHECK(app.time_edit);
    app.time_preview = (struct tm){ .tm_year = 126, .tm_mon = 0, .tm_mday = 31 };
    tap(580, 450);
    CHECK(app.time_preview.tm_mon == 1 && app.time_preview.tm_mday == 28);
    snapshot("time-picker");
    tap(100, 1080);
    CHECK(!app.time_edit);
    navigate(THINGSOS_UI_UPGRADE);
    snapshot("upgrade");
    navigate(THINGSOS_UI_RECOVERY);
    snapshot("recovery");
    navigate(THINGSOS_UI_LIBRARY);
    CHECK(management_hold(100, 400));
    CHECK(app.page == THINGSOS_UI_MANAGE && management_selected() == 1);
    snapshot("management");
    tap(100, 1060);
    CHECK(app.page == THINGSOS_UI_PASSWORD && app.editing == 3);
    tap(420, 490);
    CHECK(app.input.chinese);
    CHECK(
        thingsos_ime_letter(&app.input, 'z') && thingsos_ime_letter(&app.input, 'h') &&
        thingsos_ime_letter(&app.input, 'o') && thingsos_ime_letter(&app.input, 'n') &&
        thingsos_ime_letter(&app.input, 'g'));
    snapshot("pinyin-keyboard");
    tap(490, 920);
    CHECK(app.page == THINGSOS_UI_MANAGE);
    tap(500, 1060);
    CHECK(app.delete_confirm);
    snapshot("delete-confirm");
    tap(100, 1100);
    CHECK(!app.delete_confirm && app.page == THINGSOS_UI_MANAGE);
    tap(500, 330);
    CHECK(app.page == THINGSOS_UI_LIBRARY);
    navigate(THINGSOS_UI_READING);
    app.menu = app.jump = true;
    tap(100, 1060);
    CHECK(app.editing == 4 && app.page == THINGSOS_UI_PASSWORD);
    CHECK(thingsos_input_insert(&app.input, "99999"));
    tap(550, 1050);
    CHECK(app.page == THINGSOS_UI_PASSWORD);
    tap(100, 1050);
    CHECK(app.page == THINGSOS_UI_READING);
    navigate(THINGSOS_UI_FILES);
    CHECK(host_sd);
    strlcpy(app.browser.path, "/sdcard/fonts", sizeof(app.browser.path));
    app.browser.count = 0;
    snapshot("files-font-directory");
    tap(200, 900);
    CHECK(app.page == THINGSOS_UI_FILES); /* Fonts cannot become the book root. */
    strlcpy(app.browser.path, "/sdcard/books", sizeof(app.browser.path));
    app.browser.count = 80;
    app.list_start = 75;
    for (size_t i = 0; i < 80; ++i) {
        snprintf(
            app.browser.entries[i].name, sizeof(app.browser.entries[i].name), "book-%u.txt",
            (unsigned)i);
        app.browser.entries[i].directory = false;
    }
    snapshot("files-prototype");
    CHECK(app.canvas.pixels[390u * 684 + 650] == 210);
    tap(650, 965);
    CHECK(app.list_start == 72);
    CHECK(!management_hold(650, 400));
    tap(650, 390);
    CHECK(app.list_start == 0);
    tap(180,420);CHECK(app.file_action_open && app.page==THINGSOS_UI_FILES);snapshot("files-book-actions");
    CHECK(!app.cover_loads);tap(100,760);CHECK(!app.file_action_open && app.page==THINGSOS_UI_FILES);
    tap(180,420);tap(100,650);CHECK(app.page==THINGSOS_UI_MANAGE && app.delete_confirm && app.manage_single_delete);
    CHECK(strstr(app.manage_paths[0],"book-0.txt"));snapshot("files-book-delete-confirm");
    tap(100,1100);CHECK(app.page==THINGSOS_UI_FILES && !app.delete_confirm && !app.manage_single_delete && app.browser.count==80);
    {
        thingsos_file_entry_t saved[3];memcpy(saved,app.browser.entries,sizeof(saved));
        strlcpy(app.browser.path,"/sdcard/music",sizeof(app.browser.path));app.browser.count=3;
        strlcpy(app.browser.entries[0].name,"夜航.mp3",sizeof(app.browser.entries[0].name));
        strlcpy(app.browser.entries[1].name,"海浪.WAV",sizeof(app.browser.entries[1].name));
        strlcpy(app.browser.entries[2].name,"说明.txt",sizeof(app.browser.entries[2].name));
        host_audio=(thingsos_audio_status_t){.state=THINGSOS_AUDIO_IDLE};app.audio=host_audio;
        snapshot("files-music-notes");
        tap(180,420);CHECK(app.file_action_open && host_audio.state==THINGSOS_AUDIO_IDLE);
        snapshot("files-music-actions");
        tap(100,540);CHECK(!app.file_action_open && host_audio.state==THINGSOS_AUDIO_PLAYING && app_media.preview);
        CHECK(app_back() && host_audio.state==THINGSOS_AUDIO_IDLE);
        tap(180,420);tap(100,650);
        CHECK(app.page==THINGSOS_UI_MANAGE && app.delete_confirm && app.manage_single_delete &&
              !strcmp(app.manage_paths[0],"/sdcard/music/夜航.mp3"));
        snapshot("files-music-delete-confirm");
        tap(100,1100);CHECK(app.page==THINGSOS_UI_FILES && app.browser.count==3);
        /* Confirmed music delete: no book record and no writable records store
           are required, and a real failure reports its reason. */
        app.records_bound=false;app.records_blocked=true;app.records_dirty=true;
        tap(180,420);tap(100,650);
        CHECK(app.page==THINGSOS_UI_MANAGE && app.delete_confirm &&
              !strcmp(app.manage_paths[0],"/sdcard/music/夜航.mp3"));
        tap(500,1100);
        CHECK(app.page==THINGSOS_UI_FILES && !app.delete_confirm && !app.manage_single_delete);
        CHECK(!strcmp(app.message,"删除失败：SD 卡写入失败"));
        CHECK(app.records_dirty && app.browser.count==3);
        app.records_bound=true;app.records_blocked=false;app.records_dirty=false;
        tap(180,492);tap(100,650);
        CHECK(app.delete_confirm && !strcmp(app.manage_paths[0],"/sdcard/music/海浪.WAV"));
        tap(100,1100);
        host_audio.state=THINGSOS_AUDIO_PLAY_PAUSED;strlcpy(host_audio.path,"/sdcard/music/夜航.mp3",sizeof(host_audio.path));
        app_media.playlist=true;CHECK(management_audio_prepare(host_audio.path));
        CHECK(host_audio.state==THINGSOS_AUDIO_IDLE && !app_media.playlist);
        host_audio.state=THINGSOS_AUDIO_PLAYING;host_audio_stop_reject=true;
        CHECK(!management_audio_prepare("/sdcard/music/夜航.mp3") && host_audio.state==THINGSOS_AUDIO_PLAYING);
        host_audio_stop_reject=false;
        CHECK(management_audio_prepare("/sdcard/music/海浪.WAV") && host_audio.state==THINGSOS_AUDIO_PLAYING);
        host_audio.state=THINGSOS_AUDIO_STARTING;CHECK(!management_audio_prepare("/sdcard/music/海浪.WAV"));
        host_audio.state=THINGSOS_AUDIO_RECORDING;CHECK(!management_audio_prepare("/sdcard/music/夜航.mp3"));
        host_audio.state=THINGSOS_AUDIO_IDLE;app.audio=host_audio;
        memcpy(app.browser.entries,saved,sizeof(saved));app.browser.count=80;
        strlcpy(app.browser.path,"/sdcard/books",sizeof(app.browser.path));
    }
    /* Book deletes still require a usable records store. */
    app.records_bound=false;app.records_blocked=true;app.records_dirty=false;
    tap(180,420);CHECK(app.file_action_open);
    tap(100,650);CHECK(app.page==THINGSOS_UI_MANAGE && app.delete_confirm);
    tap(500,1100);
    CHECK(app.page==THINGSOS_UI_FILES && !strcmp(app.message,"SD 或记录不可写"));
    app.records_bound=true;app.records_blocked=false;
    tap(400,330);CHECK(app.page==THINGSOS_UI_PASSWORD && app.editing==5);
    thingsos_input_open(&app.input,"book-79",96,false);tap(590,1030);
    CHECK(app.page==THINGSOS_UI_FILES && thingsos_browser_matches(&app.browser,app.file_query)==1);snapshot("files-search-result");
    tap(180,420);CHECK(app.file_action_open && strstr(app.file_action_path,"book-79.txt"));tap(100,760);
    tap(400,330);thingsos_input_open(&app.input,"",96,false);tap(590,1030);CHECK(!app.file_query[0]);
    app.browser.count=300;for(unsigned i=80;i<300;++i){snprintf(app.browser.entries[i].name,sizeof(app.browser.entries[i].name),"book-%u.txt",i);app.browser.entries[i].directory=false;}
    app.list_start=0;for(unsigned i=0;i<40;++i)tap(550,1030);snapshot("files-many-last-page");
    CHECK(app.list_start==296 && !app.cover_loads);app.browser.count=80;
    app.list_start = 75;
    CHECK(
        management_hold(100, 400) && app.manage_count == 80 && app.manage_target == 0 &&
        strstr(app.manage_paths[0], "book-75.txt"));
    tap(500, 330);
    power_action(2);
    CHECK(app.asleep && host_off == 1);
    CHECK(host_sleep_body_ink>500 && !region_ink(0,74) && !region_ink(1098,1216));
    release_book();
    CHECK(app.progress_total == 0);
    /* A long TXT must finish in bounded steps while reading continues. */
    app.chapter.bytes = 240000;
    app.chapter.text = malloc(app.chapter.bytes + 1);
    CHECK(app.chapter.text);
    for (size_t i = 0; i < app.chapter.bytes; i += 3)
        memcpy(app.chapter.text + i, "中", 3);
    app.chapter.text[app.chapter.bytes] = 0;
    app.opened = true;
    app.px = 30;
    app.start = app.chapter.bytes / 2;
    app.chapter_index=0;
    CHECK(book_progress() == 50);
    unsigned steps = 0;
    while (!app.pagination_valid && steps++ < 4096) CHECK(pagination_step());
    CHECK(app.pagination_valid && app.page_count > 50 && !app.pagination_failed);
    jump_job_test();
    /* A newly selected font must not wait for the entire book index before
       known next-page pixels can be prepared and reused. */
    navigate(THINGSOS_UI_READING);app.asleep=false;app.weight_choice=0;
    CHECK(go_page(1));draw();app.dirty=false;reader_cache_after=0;
    app.pagination_valid=false;reader_cache_idle();CHECK(reader_frames[0].valid);
    unsigned early_hits=reader_cache_hits;size_t known_next=app.end;
    next_page(true);draw();CHECK(app.start==known_next&&reader_cache_hits==early_hits+1);
    app.pagination_valid=true;app.history_count=0;reader_cache_clear();
    /* Both directions: cached pixels/end offset equal a fresh production draw. */
    thingsos_text_underline_t cached_mark={app.pages[2].offset,app.pages[2].offset+30,false};
    const thingsos_text_underline_t* saved_marks=app.chapter.underlines;size_t saved_mark_count=app.chapter.underline_count;
    uint64_t cache_note_book=app.runtime.location.book_id;app.runtime.location.book_id=778899;
    reader_annotations_close();CHECK(reader_annotations_store());
    thingsos_annotation_t cache_note={0};cache_note.chapter=0;cache_note.begin=(uint32_t)cached_mark.begin;cache_note.end=(uint32_t)cached_mark.end;
    cache_note.text_crc=thingsos_annotation_crc(app.chapter.text,app.chapter.bytes);
    memcpy(cache_note.quote,app.chapter.text+cache_note.begin,30);
    for(unsigned i=app.annotations->count;i>0;--i)CHECK(thingsos_annotations_remove(app.annotations,app.annotations->notes[i-1].id));
    CHECK(thingsos_annotations_put(app.annotations,&cache_note));
    navigate(THINGSOS_UI_READING);
    app.asleep=false;app.weight_choice=0;
    CHECK(go_page(1));draw();app.dirty=false;reader_cache_after=0;
    unsigned char* expected=malloc(READER_CACHE_BYTES);CHECK(expected);
    memcpy(expected,app.canvas.pixels,READER_CACHE_BYTES);
    reader_cache_idle();reader_cache_idle();
    CHECK(reader_frames[0].valid && reader_frames[1].valid);
    CHECK(!memcmp(expected,app.canvas.pixels,READER_CACHE_BYTES));
    unsigned cache_hits=reader_cache_hits;
    next_page(true);draw();CHECK(reader_cache_hits==cache_hits+1);
    CHECK(reader_cache_restored_hits && app.text_hits.line_count>0);
    thingsos_text_hit_t saved_hit=app.text_hits.lines[0];size_t saved_hit_count=app.text_hits.line_count;
    size_t cached_end=app.end;
    memcpy(expected,app.canvas.pixels,READER_CACHE_BYTES);
    reader_cache_clear();draw();
    CHECK(app.end==cached_end && !memcmp(expected,app.canvas.pixels,READER_CACHE_BYTES));
    CHECK(app.text_hits.line_count==saved_hit_count && app.text_hits.lines[0].begin==saved_hit.begin &&
          app.text_hits.lines[0].end==saved_hit.end && app.text_hits.lines[0].x==saved_hit.x && app.text_hits.lines[0].y==saved_hit.y);
    app.dirty=false;reader_cache_after=0;
    reader_cache_idle();reader_cache_idle();
    cache_hits=reader_cache_hits;next_page(false);draw();
    CHECK(reader_cache_hits==cache_hits+1 && current_page()==1);
    CHECK(thingsos_annotations_remove(app.annotations,cache_note.id));
    reader_cache_clear();draw();CHECK(app.text_hits.glyph_count>3);
    thingsos_glyph_hit_t first_glyph=app.text_hits.glyphs[0],third_glyph=app.text_hits.glyphs[2];
    CHECK(first_glyph.bytes==3 && reader_annotations_hold(first_glyph.x+2,first_glyph.y+2));
    reader_annotations_drag(third_glyph.x+2,third_glyph.y+2);app.annotation_dragging=false;
    CHECK(app.annotation_end-app.annotation_begin==9);snapshot("reader-select-text");
    tap(100,1070);CHECK(app.annotation_open && !app.annotation_selecting && app.annotations->count==1);
    uint32_t selected_id=app.annotation_edit.id;CHECK(selected_id);
    tap(80,145);draw();CHECK(!app.annotation_open && app.text_hits.line_count);
    thingsos_text_hit_t local_hit=app.text_hits.lines[0];tap(local_hit.x+2,local_hit.y+2);
    CHECK(app.annotation_open && app.annotation_edit.id==selected_id);
    snapshot("reader-my-thought-card");annotation_card_geometry_t note_card=reader_annotations_card_geometry();
    CHECK(!note_card.paginated && note_card.height<600 && app.canvas.pixels[(size_t)note_card.top*684+28]==0);
    tap(80,note_card.actions_y);CHECK(app.page==THINGSOS_UI_PASSWORD && app.editing==6 && app.input.limit==1024);
    char long_thought[901];memset(long_thought,'a',900);long_thought[900]=0;
    CHECK(thingsos_input_open(&app.input,long_thought,1024,false));tap(590,1030);
    CHECK(app.page==THINGSOS_UI_READING && app.annotation_open && !strcmp(app.annotation_edit.thought,long_thought));
    snapshot("reader-my-thought");
    size_t card_start=app.start;note_card=reader_annotations_card_geometry();
    CHECK(note_card.paginated && app.annotation_next<strlen(long_thought));
    tap(500,note_card.footer_y);CHECK(app.annotation_page>0 && app.start==card_start);draw();
    tap(100,note_card.footer_y);CHECK(!app.annotation_page && app.start==card_start);
    tap(80,note_card.actions_y);CHECK(app.page==THINGSOS_UI_PASSWORD);thingsos_input_open(&app.input,"cancel this edit",1024,false);tap(80,590);
    CHECK(app.page==THINGSOS_UI_READING && !strcmp(app.annotation_edit.thought,long_thought));
    app.annotation_edit.sync=THINGSOS_NOTE_SENDING;feature_editor(6);CHECK(app.page==THINGSOS_UI_READING);
    app.annotation_edit.sync=THINGSOS_NOTE_LOCAL;
    note_card=reader_annotations_card_geometry();tap(500,note_card.actions_y+30);
    CHECK(app.annotation_delete_confirm && app.annotation_delete_line && app.annotations->count==1);
    snapshot("reader-my-thought-delete-confirm");note_card=reader_annotations_card_geometry();
    tap(100,note_card.body_y+135);CHECK(!app.annotation_delete_confirm && app.annotation_edit.thought[0]);
    note_card=reader_annotations_card_geometry();tap(80,note_card.actions_y+30);
    CHECK(app.page==THINGSOS_UI_PASSWORD && app.input.text[0]);
    thingsos_input_open(&app.input,"edited after cancel",1024,false);tap(590,1030);
    CHECK(!strcmp(app.annotations->notes[0].thought,"edited after cancel"));
    reader_annotations_dismiss();draw();
    CHECK(reader_annotations_hold(third_glyph.x+2,third_glyph.y+2));reader_annotations_drag(first_glyph.x+2,first_glyph.y+2);
    app.annotation_dragging=false;CHECK(app.annotation_end-app.annotation_begin==9 && reader_annotations_selection_note());
    CHECK(app.annotation_edit.id==selected_id && reader_annotations_save() && app.annotations->count==1);
    note_card=reader_annotations_card_geometry();tap(500,note_card.actions_y+30);
    CHECK(app.annotation_delete_confirm && app.annotation_delete_line && app.annotations->count==1);
    snapshot("reader-delete-underline-confirm");note_card=reader_annotations_card_geometry();
    tap(100,note_card.body_y+135);CHECK(!app.annotation_delete_confirm && app.annotations->count==1);
    note_card=reader_annotations_card_geometry();tap(500,note_card.actions_y+30);note_card=reader_annotations_card_geometry();
    tap(500,note_card.body_y+135);CHECK(!app.annotation_open && !app.annotations->count);
    draw();CHECK(!app.chapter.underline_count);
    CHECK(app.message[0] && app.message_until>now());host_clock_us+=2300000;app.dirty=false;message_expire_poll();
    CHECK(!app.message[0] && !app.message_until && app.dirty && app.reader_background_refresh);
    reader_annotations_close();
    app.runtime.location.book_id=cache_note_book;
    app.chapter.underlines=saved_marks;app.chapter.underline_count=saved_mark_count;
    /* Allocation failure and style/font changes cannot return an old frame. */
    reader_cache_clear();reader_cache_fail_allocate=true;
    CHECK(reader_cache_build(0,app.pages[2]) && !reader_frames[0].valid);
    reader_cache_fail_allocate=false;
    CHECK(reader_frames[0].retry_after>now() && !reader_cache_build(0,app.pages[2]));
    host_clock_us+=1001000;CHECK(reader_cache_build(0,app.pages[2]) && reader_frames[0].valid);
    reader_cache_clear();
    /* Failed glyph rendering must not become a reusable missing-text page.
       Restore the allocator and retry the SAME cache key successfully. */
    thingsos_font_t saved_font=app.font;
    memset(&app.font,0,sizeof(app.font));
    char fault_font_path[700];snprintf(fault_font_path,sizeof(fault_font_path),"%s/default.ttf",fixture_root);
    CHECK(thingsos_font_file(&app.font,fault_font_path));
    font_fault_begin(&app.font);
    CHECK(page_end(&app.chapter,app.pages[2].offset)==app.pages[2].offset);
    CHECK(reader_cache_build(0,app.pages[2]));
    font_fault_end(&app.font);
    CHECK(app.font.errors && !reader_frames[0].valid && !reader_frames[0].attempted);
    unsigned font_failures=app.font.errors;
    CHECK(reader_cache_build(0,app.pages[2]) && reader_frames[0].valid);
    CHECK(app.font.errors==font_failures);
    unsigned char* recovered_frame=malloc(READER_CACHE_BYTES);CHECK(recovered_frame);
    memcpy(recovered_frame,reader_frames[0].pixels,READER_CACHE_BYTES);
    thingsos_font_close(&app.font);app.font=saved_font;reader_cache_clear();
    CHECK(reader_cache_build(0,app.pages[2]) && reader_frames[0].valid);
    CHECK(!memcmp(recovered_frame,reader_frames[0].pixels,READER_CACHE_BYTES));
    free(recovered_frame);
    CHECK(reader_cache_key(&reader_frames[0],app.pages[2].chapter,app.pages[2].offset));
    app.font.signature^=1;
    CHECK(!reader_cache_key(&reader_frames[0],app.pages[2].chapter,app.pages[2].offset));
    app.font.signature^=1;app.weight_choice=2;
    CHECK(!reader_cache_key(&reader_frames[0],app.pages[2].chapter,app.pages[2].offset));
    app.weight_choice=0;
    uint64_t weight_layout=app.page_cache_key;size_t weight_pages=app.page_count;
    thingsos_page_position_t* weight_positions=app.pages;
    unsigned char* weight_canvas=reader_frames[0].pixels;
    app.menu=true;tap(568,898);CHECK(app.weight_choice==2 && reader_frames[0].pixels==weight_canvas && !reader_frames[0].valid);
    app.menu=false;
    CHECK(pagination_prepare() && app.pagination_valid && app.page_cache_key==weight_layout && app.page_count==weight_pages && app.pages==weight_positions);
    app.weight_choice=0;
    /* Synthetic weights add ink while preserving text advances; caches separate weights. */
    unsigned long ink[3]={0};
    for(unsigned weight=0;weight<3;++weight) {
        memset(app.canvas.pixels,255,READER_CACHE_BYTES);
        CHECK(thingsos_font_weight(&app.font,weight));
        CHECK(thingsos_font_size(&app.font,40));
        thingsos_glyph(&app.font,&app.canvas,0x4e2d,100,150);
        for(size_t p=0;p<READER_CACHE_BYTES;++p) ink[weight]+=255-app.canvas.pixels[p];
    }
    CHECK(ink[0]<ink[1] && ink[1]<ink[2]);
    CHECK(!thingsos_font_weight(&app.font,3));thingsos_font_weight(&app.font,0);
    /* The new effect consumes a request and never duplicates the old canvas. */
    app.ripple_enabled=true;host_network_busy=false;
    app.force_refresh=app.transition_refresh=app.menu=app.jump=false;
    app.image_hit_count=0;app.refresh_mode=0;
    for(unsigned direction=0;direction<2;++direction) {
        app.ripple_pending=true;app.ripple_forward=direction!=0;
        CHECK(reader_wave_requested() && !app.ripple_pending);
        CHECK(!reader_wave_requested());
    }
    app.ripple_pending=true;app.force_refresh=true;CHECK(!reader_wave_requested());
    app.force_refresh=false;app.ripple_pending=true;app.image_hit_count=1;
    CHECK(!reader_wave_requested());app.image_hit_count=0;
    app.ripple_pending=true;app.refresh_mode=2;CHECK(!reader_wave_requested());app.refresh_mode=0;
    app.ripple_pending=true;host_network_busy=true;CHECK(!reader_wave_requested());host_network_busy=false;
    /* Borrow the production cache/font while a synchronous output is pending. */
    reader_resume_equivalence();
    CHECK(pagination_prepare());
    while(!app.pagination_valid)CHECK(pagination_step());
    CHECK(go_page(1));draw();app.dirty=false;reader_cache_clear();
    memcpy(expected,app.canvas.pixels,READER_CACHE_BYTES);
    bool overlapped=reader_cache_overlap_begin();CHECK(overlapped);
    CHECK(WaitForSingleObject(reader_cache_test_thread,10000)==WAIT_OBJECT_0);
    reader_cache_overlap_end(overlapped);
    CHECK(!memcmp(expected,app.canvas.pixels,READER_CACHE_BYTES));
    CHECK(reader_frames[0].valid);
    cache_hits=reader_cache_hits;next_page(true);draw();CHECK(reader_cache_hits==cache_hits+1);
    /* Cancel during output: stop at a row boundary and never publish a partial page. */
    reader_cache_clear();reader_cache_test_slow=true;
    reader_cache_output_begin();CHECK(reader_scan_preparing && host_scan_barrier);
    CHECK(WaitForSingleObject(reader_cache_test_started,10000)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(reader_cache_test_progress,10000)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(reader_cache_test_thread,0)==WAIT_TIMEOUT);
    host_scan_barrier();CHECK(!reader_scan_preparing && !reader_cache_test_thread);
    reader_cache_output_end();CHECK(!host_scan_barrier);reader_cache_test_slow=false;
    CHECK(!reader_frames[0].valid && reader_frames[0].attempted && reader_frames[0].cursor.active);
    size_t paused_offset=reader_frames[0].cursor.offset;
    CHECK(paused_offset>reader_frames[0].start);
    reader_cache_output_begin();CHECK(reader_scan_preparing && host_scan_barrier);
    CHECK(WaitForSingleObject(reader_cache_test_started,10000)==WAIT_OBJECT_0);
    reader_cache_output_end();CHECK(!reader_scan_preparing && !reader_cache_test_thread && !host_scan_barrier);
    CHECK(app.font.size==app.px && app.font.weight==0);
    CHECK(reader_cache_build(0,app.pages[current_page()+1]) && reader_frames[0].valid);
    CHECK(reader_frames[0].cursor.offset>paused_offset && !reader_frames[0].cursor.active);
    CHECK(reader_cache_build(1,app.pages[current_page()+1]) && reader_frames[1].valid);
    CHECK(reader_frames[0].end==reader_frames[1].end);
    CHECK(!memcmp(reader_frames[0].pixels,reader_frames[1].pixels,READER_CACHE_BYTES));
    CHECK(!memcmp(reader_frames[0].hits,reader_frames[1].hits,sizeof(*reader_frames[0].hits)));
    /* Rapid input can finish its partial target directly on the cache-hit path. */
    reader_cache_clear();reader_cache_test_slow=true;reader_cache_output_begin();
    CHECK(WaitForSingleObject(reader_cache_test_started,10000)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(reader_cache_test_progress,10000)==WAIT_OBJECT_0);host_scan_barrier();reader_cache_output_end();reader_cache_test_slow=false;
    CHECK(reader_frames[0].cursor.active);
    cache_hits=reader_cache_hits;next_page(true);draw();
    CHECK(reader_cache_hits==cache_hits+1 && reader_frames[0].valid && !reader_frames[0].cursor.active);
    memcpy(expected,app.canvas.pixels,READER_CACHE_BYTES);reader_cache_clear();draw();
    CHECK(!memcmp(expected,app.canvas.pixels,READER_CACHE_BYTES));
    /* Menu controls and reboot preference loading use actual controller paths. */
    unsigned char* retained_canvas=reader_frames[0].pixels;
    app.menu=true;tap(550,890);CHECK(app.weight_choice==2 && reader_frames[0].pixels==retained_canvas && !reader_frames[0].valid);
    tap(250,975);CHECK(!app.ripple_enabled);tap(400,975);CHECK(app.ripple_enabled);
    tap(400,655);CHECK(app.indent_chars==THINGSOS_READER_INDENT_CHARS && !reader_frames[0].pixels);
    /* Feed the actual reader controller, then simulate its completed frame. */
    app.menu=app.jump=false;app.tilt_enabled=true;app.dirty=false;
    app.wd_long_open=true;app.wd_sync_panel=true;app.wd_notes_open=false;
    CHECK(!go_page(1) && !app.pagination_valid); /* Indent invalidates the old index without blocking. */
    CHECK(paginate()); /* Simulate completion before testing indexed tilt turns. */
    CHECK(go_page(1));draw();app.dirty=false;
    size_t tilt_before=current_page();
    for(unsigned i=0;i<20;++i){host_clock_us+=40000;reader_tilt_poll(false);}
    CHECK(host_tilt_active && app.tilt.calibrated);
    app.wd_long_open=app.wd_sync_panel=false; /* Hidden child-page flags cannot block reading gestures. */
    host_tilt_x=400;host_tilt_y=260;host_tilt_z=880;
    for(unsigned i=0;i<20;++i){host_clock_us+=40000;reader_tilt_poll(false);if(app.dirty){draw();app.dirty=false;}}
    CHECK(current_page()==tilt_before+1);
    for(unsigned i=0;i<25;++i){host_clock_us+=40000;reader_tilt_poll(false);}
    CHECK(current_page()==tilt_before+1);
    host_tilt_x=host_tilt_y=0;host_tilt_z=1000;
    for(unsigned i=0;i<30;++i){host_clock_us+=40000;reader_tilt_poll(false);}
    host_tilt_x=-400;host_tilt_y=260;host_tilt_z=880;
    for(unsigned i=0;i<20;++i){host_clock_us+=40000;reader_tilt_poll(false);if(app.dirty){draw();app.dirty=false;}}
    CHECK(current_page()==tilt_before);
    reader_tilt_poll(true);CHECK(host_tilt_active && app.tilt.calibrated);
    app.menu=true;reader_tilt_poll(false);CHECK(!host_tilt_active);
    app.menu=false;app.footnote_open=true;reader_tilt_poll(false);CHECK(!host_tilt_active);
    app.footnote_open=false;app.wd_cloud_prompt=true;reader_tilt_poll(false);CHECK(!host_tilt_active);
    app.wd_cloud_prompt=false;app.asleep=true;reader_tilt_poll(false);CHECK(!host_tilt_active);
    app.asleep=false;app.tilt_enabled=false;reader_tilt_poll(false);CHECK(!host_tilt_active);
    host_tilt_x=host_tilt_y=0;host_tilt_z=1000;host_clock_us=1000000;app.menu=true;
    /* Input arbitration must ignore a busy sampler and a finished drag. The
       production wrapper consumes actual contact, not the generic pending API. */
    app.tilt_enabled=true;app.menu=false;app.dirty=false;app.tilt_poll_at=0;
    host_clock_us=20000000;host_tilt_x=0;host_tilt_y=-980;host_tilt_z=180;
    for(unsigned i=0;i<20;++i){host_clock_us+=40000;reader_tilt_input_poll(false,false);}
    CHECK(app.tilt.calibrated && host_tilt_active);
    host_touch_contact=true;reader_tilt_input_poll(false,false);CHECK(app.tilt.calibrated && host_tilt_active);
    host_touch_contact=false;app.tilt_enabled=false;reader_tilt_input_poll(false,false);
    /* A slow UI frame must not turn 25Hz sensor history into sparse samples. */
    app.tilt_enabled=true;app.menu=false;app.dirty=false;app.tilt_poll_at=0;
    thingsos_tilt_reset(&app.tilt);host_tilt_manual=true;host_clock_us=30000000;
    for(unsigned i=0;i<20;++i)thingsos_tilt_queue_push(&host_tilt_samples,(thingsos_tilt_input_t){0,0,-1000,29240+i*40});
    reader_tilt_poll(false);CHECK(app.tilt.calibrated);
    size_t buffered_page=current_page();
    for(unsigned i=0;i<20;++i)thingsos_tilt_queue_push(&host_tilt_samples,(thingsos_tilt_input_t){-450,0,-890,30040+i*40});
    host_clock_us=30800000;reader_tilt_poll(false);CHECK(current_page()==buffered_page+1);
    draw();app.dirty=false;
    for(unsigned i=0;i<30;++i)thingsos_tilt_queue_push(&host_tilt_samples,(thingsos_tilt_input_t){0,0,-1000,30840+i*40});
    host_clock_us=32000000;reader_tilt_poll(false);CHECK(app.tilt.armed);
    /* A fresh complete wrist turn during one frame survives until the UI is ready. */
    for(unsigned i=0;i<10;++i)thingsos_tilt_queue_push(&host_tilt_samples,(thingsos_tilt_input_t){-450,0,-890,32040+i*40});
    for(unsigned i=0;i<10;++i)thingsos_tilt_queue_push(&host_tilt_samples,(thingsos_tilt_input_t){0,0,-1000,32440+i*40});
    host_clock_us=32800000;reader_tilt_poll(false);CHECK(current_page()==buffered_page+2 && app.dirty);
    host_tilt_manual=false;app.tilt_enabled=false;reader_tilt_poll(false);
    host_tilt_x=host_tilt_y=0;host_tilt_z=1000;host_clock_us=1000000;app.menu=true;
    /* More is a reader child page; both menu layouts preserve existing actions. */
    CHECK(thingsos_reader_menu_hit_weidu(500,1150,NULL)==THINGSOS_MENU_MORE);
    CHECK(thingsos_reader_menu_hit_weidu(300,1150,NULL)==THINGSOS_MENU_NOTES);
    CHECK(thingsos_reader_menu_hit(342,1150,NULL)==THINGSOS_MENU_NONE);
    CHECK(thingsos_reader_menu_hit_weidu(235,1150,NULL)==THINGSOS_MENU_NONE);
    CHECK(thingsos_reader_menu_hit(104,1205,NULL)==THINGSOS_MENU_FONTS);
    CHECK(thingsos_reader_menu_hit_weidu(103,1210,NULL)==THINGSOS_MENU_FONTS);
    CHECK(thingsos_reader_menu_hit_weidu(517,1215,NULL)==THINGSOS_MENU_MORE);
    CHECK(thingsos_reader_menu_hit_weidu(235,1210,NULL)==THINGSOS_MENU_NONE);
    CHECK(thingsos_reader_menu_hit(342,1210,NULL)==THINGSOS_MENU_NONE);
    CHECK(thingsos_reader_menu_hit(104,1216,NULL)==THINGSOS_MENU_NONE);
    CHECK(thingsos_reader_menu_hit_landscape(100,683,NULL,false)==THINGSOS_MENU_FONTS);
    CHECK(thingsos_reader_menu_hit_landscape(1100,683,NULL,true)==THINGSOS_MENU_MORE);
    app.dirty=false;unsigned miss_commits=host_nvs_commits;tap(342,1210);
    CHECK(!app.dirty&&host_nvs_commits==miss_commits&&app.menu);
    CHECK(!app.tilt_enabled);
    tap(550,1150);CHECK(app.page==THINGSOS_UI_READER_MORE && !app.menu);
    snapshot("reader-more-off");
    CHECK(reader_more_toggle_hit(580,450)==0 && reader_more_toggle_hit(580,650)==1);
    CHECK(reader_more_toggle_hit(300,450)==-1);
    host_nvs_available=true;tap(580,450);CHECK(app.tilt_enabled && host_saved_tilt==1);
    snapshot("reader-more-on");
    CHECK(app.tilt_sensitivity==1 && reader_more_toggle_hit(550,550)==2);
    tap(490,550);CHECK(app.tilt_sensitivity==0 && host_saved_tilt_sensitivity==0);
    snapshot("reader-more-low");
    tap(610,550);CHECK(app.tilt_sensitivity==2 && host_saved_tilt_sensitivity==2);
    host_nvs_commit_fail=true;tap(550,550);CHECK(app.tilt_sensitivity==2);host_nvs_commit_fail=false;
    tap(550,550);CHECK(app.tilt_sensitivity==1 && host_saved_tilt_sensitivity==1);
    host_nvs_read=true;host_saved_tilt_sensitivity=2;preferences_load();CHECK(app.tilt_sensitivity==2);
    host_saved_tilt_sensitivity=99;preferences_load();CHECK(app.tilt_sensitivity==1);
    host_tilt_sensitivity_present=false;preferences_load();CHECK(app.tilt_sensitivity==1);
    host_tilt_sensitivity_present=true;host_saved_tilt_sensitivity=1;host_nvs_read=false;

    tap(580,450);CHECK(app.tilt_enabled && host_saved_tilt==1); /* Right On remains On. */
    tap(30,120);CHECK(app.page==THINGSOS_UI_READING && app.menu);
    app.tilt_enabled=false;host_nvs_read=true;preferences_load();CHECK(app.tilt_enabled);
    host_saved_tilt=99;preferences_load();CHECK(!app.tilt_enabled);
    tap(550,1150);CHECK(app.page==THINGSOS_UI_READER_MORE);
    host_nvs_commit_fail=true;tap(580,450);CHECK(!app.tilt_enabled);host_nvs_commit_fail=false;
    tap(580,450);CHECK(app.tilt_enabled);
    tap(500,450);CHECK(!app.tilt_enabled && !host_saved_tilt);
    capacitive_key(THINGSOS_KEY_MENU);CHECK(app.page==THINGSOS_UI_READING && app.menu);
    reader_tilt_poll(false);CHECK(!host_tilt_active);
    host_nvs_read=false;
    /* Landscape persistence, source position, repagination, and real input mapping. */
    CHECK(!app.landscape_enabled);CHECK(pagination_prepare());while(!app.pagination_valid)CHECK(pagination_step());
    CHECK(go_page(1));draw();size_t landscape_start=app.start;uint64_t portrait_key=app.page_cache_key;
    app.menu=true;tap(550,1150);CHECK(app.page==THINGSOS_UI_READER_MORE);
    host_nvs_commit_fail=true;tap(580,650);CHECK(!app.landscape_enabled);host_nvs_commit_fail=false;
    tap(580,650);CHECK(app.landscape_enabled && host_saved_landscape && app.canvas.width==1216);
    CHECK(app.start==landscape_start && !app.pagination_started);snapshot("landscape-more");
    CHECK(reader_more_toggle_hit(1150,330)==2);
    tap(1150,330);CHECK(app.tilt_sensitivity==2);
    tap(1100,330);CHECK(app.tilt_sensitivity==1);

    tap(800,640);CHECK(app.page==THINGSOS_UI_READING && app.menu);snapshot("landscape-menu");
    CHECK(thingsos_reader_menu_hit_landscape(1000,620,NULL,false)==THINGSOS_MENU_MORE);
    CHECK(thingsos_reader_menu_hit_landscape(600,620,NULL,true)==THINGSOS_MENU_NOTES);
    CHECK(thingsos_reader_menu_hit_landscape(410,520,NULL,false)==THINGSOS_MENU_NONE);
    unsigned wide_choice=9;CHECK(thingsos_reader_menu_hit_landscape(1060,390,&wide_choice,false)==THINGSOS_MENU_NONE);
    CHECK(thingsos_reader_menu_hit_landscape(460,390,&wide_choice,false)==THINGSOS_MENU_LINE && wide_choice==2);
    tap(1160,106);CHECK(!app.menu);snapshot("landscape-reading");
    CHECK(app.canvas.width==1216 && app.canvas.height==684 && app.end==page_end(&app.chapter,app.start));
    CHECK(pagination_prepare());while(!app.pagination_valid)CHECK(pagination_step());CHECK(app.page_cache_key!=portrait_key);
    CHECK(go_page(1));draw();size_t wide_page=current_page();
    int tx=433,ty=1100;CHECK(reader_touch_coordinates(&tx,&ty) && tx==1100 && ty==250);tap(tx,ty);
    CHECK(current_page()==wide_page+1);draw();
    tx=433;ty=100;CHECK(reader_touch_coordinates(&tx,&ty) && tx==100 && ty==250);tap(tx,ty);CHECK(current_page()==wide_page);
    tx=433;ty=608;CHECK(reader_touch_coordinates(&tx,&ty));tap(tx,ty);CHECK(app.menu);
    tx=0;ty=0;CHECK(reader_touch_coordinates(&tx,&ty) && tx==0 && ty==683);
    tx=683;ty=1215;CHECK(reader_touch_coordinates(&tx,&ty) && tx==1215 && ty==0);
    tx=100;ty=1300;CHECK(!reader_touch_coordinates(&tx,&ty) && ty==1300); /* Physical keys stay raw. */
    tap(1000,520);CHECK(app.jump);app.jump_percent=50;snapshot("landscape-jump");
    tap(100,250);CHECK(app.jump_percent==45);
    tap(1100,250);CHECK(app.jump_percent==50);
    tap(608,250);tap(608,400);CHECK(app.jump && app.jump_percent==50);
    tap(608,550);CHECK(app.jump);tap(60,610);CHECK(!app.jump);
    tap(1160,106);app.wd_thought_open=true;app.wd_thought_full=false;app.wd_thought_cached=false;app.wd_thought_loading=false;
    snapshot("landscape-thought");tap(1140,120);CHECK(!app.wd_thought_open);
    CHECK(go_page(1));draw();reader_cache_clear();CHECK(reader_cache_build(0,app.pages[current_page()]));CHECK(reader_frames[0].valid);
    CHECK(reader_cache_restore() && app.end==reader_frames[0].end);reader_cache_clear();
    /* Tilt directions follow the rotated screen axes too. */
    app.menu=false;app.tilt_enabled=true;app.tilt_poll_at=0;app.dirty=false;host_clock_us=10000000;
    for(unsigned i=0;i<20;++i){host_clock_us+=40000;reader_tilt_poll(false);}
    CHECK(app.tilt.calibrated);size_t wide_tilt_page=current_page();
    host_tilt_x=-260;host_tilt_y=400;host_tilt_z=880;
    for(unsigned i=0;i<20;++i){host_clock_us+=40000;reader_tilt_poll(false);if(app.dirty){draw();app.dirty=false;}}
    CHECK(current_page()==wide_tilt_page+1);
    app.tilt_enabled=false;reader_tilt_poll(false);host_tilt_x=host_tilt_y=0;host_tilt_z=1000;host_clock_us=1000000;
    /* Wide footnote hit boxes and image controls share the same coordinates. */
    thingsos_footnote_t* old_notes=app.chapter.footnotes;size_t old_note_count=app.chapter.footnote_count;
    char* old_note_text=app.chapter.notes;size_t old_note_bytes=app.chapter.notes_bytes;
    char wide_note_text[6001];for(unsigned i=0;i<2000;++i)memcpy(wide_note_text+i*3,"注",3);wide_note_text[6000]=0;
    thingsos_footnote_t wide_note={app.start,app.start+3,0,6000};
    app.chapter.footnotes=&wide_note;app.chapter.footnote_count=1;app.chapter.notes=wide_note_text;app.chapter.notes_bytes=6000;
    app.footnote_open=true;app.footnote_index=app.footnote_offset=0;reader_cache_clear();snapshot("landscape-footnote");
    CHECK(app.footnote_next>0 && app.footnote_next<6000 && app.footnote_card_height+FOOTNOTE_CARD_TOP<=684);
    size_t note_start=app.footnote_next;tap(1100,300);CHECK(app.footnote_open && app.footnote_offset==note_start);
    tap(1210,650);CHECK(!app.footnote_open);
    app.chapter.footnotes=old_notes;app.chapter.footnote_count=old_note_count;app.chapter.notes=old_note_text;app.chapter.notes_bytes=old_note_bytes;
    app.image_pixels=malloc(16);CHECK(app.image_pixels);memset(app.image_pixels,128,16);app.image_w=app.image_h=4;
    thingsos_image_open(&app.image_view,app.runtime.location);snapshot("landscape-image");
    tap(500,534);CHECK(app.image_view.zoom_percent==150);tap(50,100);CHECK(!app.image_view.open && !app.image_pixels);
    app.wd_active=false;power_action(1);CHECK(app.asleep && app.canvas.width==684 && app.landscape_enabled);
    power_action(1);CHECK(!app.asleep && app.landscape_enabled);draw();CHECK(app.canvas.width==1216);
    app.menu=true;app.wd_id[0]='x';app.wd_id[1]=0;snapshot("landscape-menu-weidu");app.wd_id[0]=0;
    tap(1000,620);CHECK(app.page==THINGSOS_UI_READER_MORE);
    app.landscape_enabled=false;host_nvs_read=true;preferences_load();CHECK(app.landscape_enabled);host_nvs_read=false;
    landscape_start=app.start;tap(430,430);CHECK(!app.landscape_enabled && !host_saved_landscape && app.start==landscape_start);
    CHECK(app.canvas.width==684 && app.canvas.height==1216);tap(30,120);CHECK(app.menu);
    host_nvs_read=true;preferences_load();CHECK(!app.landscape_enabled);
    host_saved_landscape=99;preferences_load();CHECK(!app.landscape_enabled);host_saved_landscape=0;host_nvs_read=false;
    app.ui_px=50;app.ui_weight=1;app.ui_refresh_pages=20;app.ui_refresh_mode=2;app.lock_wallpaper=1;
    host_saved_invert=1; /* A persisted switch from 0.1.5 must be ignored. */
    wallpaper_book_set(&app.wallpaper_book,"/sdcard/books/pinned.epub","固定的封面");
    host_nvs_available=true;preferences_save();
    app.weight_choice=0;app.ripple_enabled=false;app.indent_chars=0;host_nvs_read=true;
    app.ui_px=30;app.ui_weight=app.ui_refresh_mode=0;app.ui_refresh_pages=15;app.lock_wallpaper=0;
    app.canvas.inverted=true;
    memset(&app.wallpaper_book,0,sizeof(app.wallpaper_book));
    preferences_load();CHECK(app.weight_choice==2 && app.ripple_enabled);
    CHECK(app.indent_chars==THINGSOS_READER_INDENT_CHARS);
    CHECK(app.ui_px==50 && app.ui_weight==1 && app.ui_refresh_pages==20 && app.ui_refresh_mode==2 && app.lock_wallpaper==1);
    CHECK(!app.canvas.inverted && host_saved_invert==1);
    CHECK(!strcmp(app.wallpaper_book.path,"/sdcard/books/pinned.epub") && !strcmp(app.wallpaper_book.name,"固定的封面"));
    unsigned body_weight_before_ui=app.font.weight;
    navigate(THINGSOS_UI_LIBRARY);draw();
    CHECK(app.ui_font.weight==1 && app.font.weight==body_weight_before_ui && !app.font.ui_size_percent && !app.font.ui_weight_override);
    navigate(THINGSOS_UI_READING);draw();CHECK(app.font.weight==0);
    host_saved_weight=99;host_saved_ripple=99;app.weight_choice=1;app.ripple_enabled=false;
    preferences_load();CHECK(app.weight_choice==1 && !app.ripple_enabled);
    host_nvs_available=host_nvs_read=false;
    /* The 50 boundary/persistence checks above are complete. Keep subsequent
       unrelated geometry fixtures at their established UI scale. */
    app.ui_px=34;
    snapshot("reader-weight-ripple-settings");
    free(expected);
    release_book();
    CHECK(!reader_frames[0].pixels && !reader_frames[1].pixels);
    /* Old bookmarks store original spine and absolute text offsets. */
    char split_path[600];snprintf(split_path,sizeof(split_path),"%s/navigation.epub",fixture_root);
    CHECK(thingsos_epub_open(&app.epub,split_path));app.epub_open=true;
    thingsos_epub_navigation(&app.epub);CHECK(app.epub.spine_count==2);
    CHECK(load_chapter(1));size_t original_offset=app.chapter.source_start;
    CHECK(original_offset>0 && app.epub.chapter_sources[1]==0);
    CHECK(reader_restore_source(0,original_offset+4));
    CHECK(app.chapter_index==1 && app.start==4);
    /* Production UI checkpoints resume at the next whole chapter, and TOC
       entry presents a root list before title discovery runs. */
    {
        bool saved_sd=host_sd;host_sd=true;app.opened=true;app.wd_id[0]=0;
        strlcpy(app.read_path,split_path,sizeof(app.read_path));app.runtime.location.book_id=98431;
        char cache[640];snprintf(cache,sizeof(cache),"%s.pages",host_font);remove(cache);
        char partial[680];snprintf(partial,sizeof(partial),"%s.partial",cache);remove(partial);
        app.pagination_started=false;CHECK(pagination_prepare());
        while(!app.pagination_chapter)CHECK(pagination_step());
        CHECK(!app.pagination_valid && app.pagination_chapter==1 && app.pagination_done_pages==app.page_count);
        size_t checkpoint_pages=app.page_count;pagination_checkpoint();
        CHECK(app.pagination_saved_chapters==1);
        free(app.pages);app.pages=NULL;app.page_count=0;app.pagination_started=false;
        CHECK(pagination_prepare() && !app.pagination_valid && app.pagination_chapter==1 && app.page_count==checkpoint_pages);
        while(!app.pagination_valid)CHECK(pagination_step());
        CHECK(app.page_count>checkpoint_pages);
        app.epub.titles_next=0;navigate(THINGSOS_UI_CHAPTERS);
        CHECK(app.toc_pending && app.epub.titles_next==0);
        draw();app.dirty=false;host_clock_us+=1600000;app.activity_at=0;
        app.ui_action_at=0;app.reader_background_refresh=false;
        app.toc_invalidate_pending=true;
        reader_toc_poll();CHECK(app.epub.titles_next==0&&app.toc_pending&&app.toc_invalidate_pending&&!app.dirty);
        CHECK(!reader_background_work_allowed());
        navigate(THINGSOS_UI_READING);app.dirty=false;
        app.menu=true;reader_toc_poll();CHECK(app.epub.titles_next==0);
        app.menu=false;CHECK(reader_background_work_allowed());
        reader_toc_poll();CHECK(app.epub.titles_next==1&&app.toc_pending&&!app.toc_invalidate_pending&&!app.dirty);
        reader_toc_poll();CHECK(!app.toc_pending&&!app.dirty&&!app.reader_background_refresh);
        navigate(THINGSOS_UI_CHAPTERS);draw();app.dirty=false;
        reader_toc_poll();CHECK(!app.dirty&&!app.reader_background_refresh);
        /* A dynamic insert may change indices before the next scan. A tap on
           the painted first row must retain its original chapter target. */
        app.list_start=0;draw();app.dirty=false;
        app.epub.section_toc=calloc(2,sizeof(*app.epub.section_toc));CHECK(app.epub.section_toc);
        app.epub.section_toc_count=2;app.epub.section_toc[0].chapter=1;
        tap(100,400);CHECK(app.page==THINGSOS_UI_READING && app.chapter_index==0);
        remove(cache);remove(partial);host_sd=saved_sd;
    }
    CHECK(reader_lines[1]==145 && reader_margins[1]==24 && reader_lines[0]<145 && reader_margins[0]<24);
    release_book();
    /* Explicit cover selection: cancel and commit failure preserve the old
       source; reading another book cannot silently replace the chosen cover. */
    host_sd=host_nvs_available=true;app.book_count=1;app.lock_wallpaper=0;
    snprintf(app.books[0].path,sizeof(app.books[0].path),"%s/lock-cover.epub",fixture_root);
    strcpy(app.books[0].name,"固定封面");app.books[0].epub=true;app.books[0].id=881;
    app.library_valid=true;app.library_card=app.records.card_id;
    strlcpy(app.library_path,app.records.library_path,sizeof(app.library_path));
    navigate(THINGSOS_UI_POWER);tap(460,490);CHECK(app.page==THINGSOS_UI_POWER);
    navigate(THINGSOS_UI_APPS);snapshot("apps-wallpaper-entry");tap(330,680);
    CHECK(app.page==THINGSOS_UI_WALLPAPER && app.wallpaper_step==0);snapshot("wallpaper-app");
    tap(40,120);CHECK(app.page==THINGSOS_UI_APPS);tap(330,680);tap(300,480);
    CHECK(app.wallpaper_step==1);snapshot("wallpaper-book-list");
    tap(300,340);CHECK(app.wallpaper_step==3 && app.lock_wallpaper==0);
    snapshot("wallpaper-book-preview");CHECK(app.wallpaper_preview_ok);
    tap(120,1140);CHECK(app.wallpaper_step==1 && app.lock_wallpaper==0);
    tap(300,340);draw();host_nvs_commit_fail=true;tap(500,1140);
    CHECK(app.page==THINGSOS_UI_WALLPAPER && app.lock_wallpaper==0 && strstr(app.message,"保存失败"));
    host_nvs_commit_fail=false;tap(500,1140);
    CHECK(app.page==THINGSOS_UI_APPS && app.lock_wallpaper==1 && app.wallpaper_book.id==881);
    app.recent_count=1;app.recent_ids[0]=999;strcpy(app.books[0].path,"missing-other-book.epub");
    lock_draw(0);
    CHECK(app.wallpaper_book.id==881 && app.canvas.pixels[300u*684]==0 && app.canvas.pixels[300u*684+1]==255);
    CHECK(wallpaper_image_path("/sdcard/wallpapers/a.JPG"));
    CHECK(!wallpaper_image_path("/sdcard/wallpapers-else/a.png"));
    CHECK(!wallpaper_image_path("/sdcard/wallpapers/../books/a.png"));
    CHECK(!wallpaper_image_path("/sdcard/wallpapers/a.txt"));
    navigate(THINGSOS_UI_WALLPAPER);app.wallpaper_step=2;app.wallpaper_start=0;
    strcpy(app.browser.path,"/sdcard/wallpapers");app.browser.count=5;
    for(size_t i=0;i<5;++i) {
        snprintf(app.browser.entries[i].name,sizeof(app.browser.entries[i].name),"wallpaper-%u.png",(unsigned)i);
        app.browser.entries[i].directory=false;
    }
    app.browser.entries[1].directory=true;
    CHECK(wallpaper_card_at(100,400)==0 && wallpaper_card_at(450,400)==1);
    CHECK(wallpaper_card_at(100,700)==2 && wallpaper_card_at(450,700)==3);
    CHECK(wallpaper_card_at(340,400)==SIZE_MAX && wallpaper_card_at(100,615)==SIZE_MAX);
    CHECK(!app_hold(100,400,649) && !wallpaper_delete_path[0]);
    CHECK(!app_hold(450,400,700));
    snapshot("wallpaper-image-cards");
    CHECK(app_hold(100,400,700) && !strcmp(wallpaper_delete_path,"/sdcard/wallpapers/wallpaper-0.png"));
    snapshot("wallpaper-delete-confirm");tap(100,690);CHECK(!wallpaper_delete_path[0] && app.wallpaper_step==2);
    CHECK(app_hold(100,400,700));app.lock_wallpaper=2;
    strcpy(app.wallpaper_image,wallpaper_delete_path);tap(100,830);
    CHECK(wallpaper_delete_path[0] && strstr(app.message,"正在使用"));
    tap(40,120);CHECK(!wallpaper_delete_path[0]);app.lock_wallpaper=1;
    tap(550,1000);CHECK(app.wallpaper_start==4);tap(100,1000);CHECK(!app.wallpaper_start);
    tap(450,700);CHECK(app.wallpaper_step==3 && strstr(app.wallpaper_candidate_image,"wallpaper-3.png"));
    char card_source[640];snprintf(card_source,sizeof(card_source),"%s/picture.png",fixture_root);
    CHECK(wallpaper_thumbnail(card_source,48,308,270,244));
    const thingsos_thumbnail_t* card_thumb=thingsos_thumbnail_find(&app.thumbnails,identity(card_source),270,244);
    CHECK(card_thumb && card_thumb->pixels && card_thumb->version==thingsos_cover_source_stamp(card_source));
    unsigned char* card_pixels=malloc(270u*244u);CHECK(card_pixels);
    memcpy(card_pixels,card_thumb->pixels,270u*244u);
    for(unsigned i=0;i<4;++i)if(i!=1) {
        char path[THINGSOS_RECORD_PATH_BYTES];CHECK(thingsos_browser_path(&app.browser,i,path));
        CHECK(thingsos_thumbnail_store(&app.thumbnails,identity(path),270,244,card_pixels,270,244));
    }
    free(card_pixels);app.wallpaper_step=2;snapshot("wallpaper-image-covers");
    CHECK(app.canvas.pixels[299u*684+48]==255 && app.canvas.pixels[605u*684+48]==255);
    navigate(THINGSOS_UI_WALLPAPER);app.wallpaper_step=3;app.wallpaper_kind=2;
    strcpy(app.wallpaper_candidate_image,"/sdcard/wallpapers/missing.jpg");
    snapshot("wallpaper-preview-unavailable");CHECK(!app.wallpaper_preview_ok);
    tap(500,1140);CHECK(app.lock_wallpaper==1 && app.page==THINGSOS_UI_WALLPAPER);
    /* Landscape wallpapers preview in 16:9 framing and lock at 1216x684 with cached reload. */
    strlcpy(host_picture_root,fixture_root,sizeof(host_picture_root));
    snprintf(host_lock_cache_root,sizeof(host_lock_cache_root),"%s/lock-covers-test",fixture_root);
    mkdir(host_lock_cache_root,0775);
    char selected_cache[640];snprintf(selected_cache,sizeof(selected_cache),"%s/selected-image.gray",host_lock_cache_root);
    remove(selected_cache);
    char actual_land[640];snprintf(actual_land,sizeof(actual_land),"%s/wallpapers/landscape.png",fixture_root);
    uint64_t old_land_stamp=thingsos_cover_source_stamp(actual_land);
    thingsos_canvas_t old_wallpaper={app.canvas.pixels,1216,684,false};
    memset(old_wallpaper.pixels,0,1216u*684u);
    CHECK(old_land_stamp && thingsos_cover_cache_save(selected_cache,old_land_stamp,&old_wallpaper,0,0,1216,684));
    app.wallpaper_step=3;app.wallpaper_kind=2;
    strcpy(app.wallpaper_candidate_image,"/sdcard/wallpapers/landscape.png");
    snapshot("wallpaper-preview-landscape");CHECK(app.wallpaper_preview_ok && app.canvas.width==684 && app.canvas.height==1216);
    CHECK(app.canvas.pixels[434u*684+36]==76);
    app.lock_wallpaper=2;strcpy(app.wallpaper_image,"/sdcard/wallpapers/landscape.png");
    lock_draw(0);
    CHECK(app.canvas.width==1216 && app.canvas.height==684 && app.canvas.pixels[0]==76);
    uint64_t land_stamp=wallpaper_picture_stamp(actual_land);
    CHECK(land_stamp && thingsos_cover_cache_load(selected_cache,land_stamp,&app.canvas,0,0,1216,684));
    lock_draw(0);CHECK(app.canvas.width==1216 && app.canvas.height==684 && app.canvas.pixels[0]==76);
    strcpy(app.wallpaper_image,"/sdcard/wallpapers/portrait.png");
    lock_draw(0);CHECK(app.canvas.width==684 && app.canvas.height==1216);
    app.lock_wallpaper=1;host_picture_root[0]=0;host_lock_cache_root[0]=0;remove(selected_cache);
    host_saved_ui[2]=6;host_saved_ui[5]=12;host_saved_ui[4]=1;host_wall_strings[0][0]=0;
    memset(&app.wallpaper_book,0,sizeof(app.wallpaper_book));host_nvs_read=true;preferences_load();
    CHECK(app.lock_wallpaper==0 && app.refresh_pages==15 && app.ui_refresh_pages==15);
    CHECK(THINGSOS_READER_SIZE_DEFAULT==46);
    host_saved_ui[6]=30;host_saved_px_default=0;app.px=30;
    unsigned migration_commits=host_nvs_commits;preferences_load();
    CHECK(app.px==46 && host_saved_px_default==1 && host_nvs_commits==migration_commits+1);
    app.px=30;CHECK(preferences_save());app.px=46;preferences_load();CHECK(app.px==30);
    app.px=50;CHECK(preferences_save());app.px=46;preferences_load();CHECK(app.px==50);
    host_nvs_available=host_nvs_read=false;
    weread_reading_ui_test();
    /* Public accounts reuse the nine visible-book slots, with five rows/page. */
    host_network_connected=true;host_mp_available=true;host_weidu.authenticated=true;
    host_weidu.busy=host_weidu.background=false;host_weidu.phase=WD_READY;++host_weidu.serial;
    app.page=THINGSOS_UI_LIBRARY;app.weidu_detail=app.weidu_logout_confirm=false;
    weidu_enter();tap(270,150);
    CHECK(app.mp_mode==1 && app.weidu_count==5 && app.weidu_total==7);snapshot("mp-accounts");
    CHECK(!weidu_hold(100,420));tap(500,1040);CHECK(app.weidu_start==5 && app.weidu_count==2);
    tap(150,1040);CHECK(app.weidu_start==0);tap(100,420);
    CHECK(app.mp_mode==2 && !strcmp(app.mp_account.id,"MP_WXS_1") && app.weidu_total==10);snapshot("mp-articles");
    tap(500,1040);CHECK(app.weidu_start==5 && app.weidu_count==5);tap(150,1040);
    unsigned mp_calls=host_weidu_calls;tap(500,275);CHECK(host_weidu_action==WD_DEVICE_MP_ARTICLES && host_weidu_calls==mp_calls+1);
    host_weidu.busy=false;host_weidu.phase=WD_READY;++host_weidu.serial;weidu_poll();
    snprintf(host_mp_article,sizeof(host_mp_article),"%s/mp-read.txt",fixture_root);
    FILE* mp_file=fopen(host_mp_article,"wb");CHECK(mp_file && fputs("Article body.\nSecond paragraph.",mp_file)>=0 && !fclose(mp_file));
    tap(100,420);CHECK(app.page==THINGSOS_UI_READING && app.opened && !app.epub_open && !app.wd_id[0] && app.reader_from_weidu);
    /* Existing Markdown article retains styles/source offsets, while its old image is hidden. */
    snprintf(host_mp_article,sizeof(host_mp_article),"%s/mp-read.md",fixture_root);
    mp_file=fopen(host_mp_article,"wb");CHECK(mp_file);
    CHECK(fputs("# Markdown article\n**Bold** and *italic* text.\n> Quoted paragraph\n`code sample` and [link](https://example.com)\n![Local image](picture.png)\n",mp_file)>=0 && !fclose(mp_file));
    weidu_enter();tap(100,420);
    CHECK(app.chapter.markdown_index && !app.chapter.image_count && app.chapter.text[0]=='#');
    unsigned char* md_pixels=NULL;unsigned md_w,md_h;thingsos_inline_t local_image={0};
    snprintf(local_image.path,sizeof(local_image.path),"%s/picture.png",fixture_root);
    CHECK(!image_load(&local_image,&md_pixels,&md_w,&md_h)); /* MP never decodes a picture. */
    app.reader_from_weidu=false;CHECK(image_load(&local_image,&md_pixels,&md_w,&md_h));free(md_pixels);
    app.reader_from_weidu=true;
    app.start=0;app.pagination_started=false;pagination_prepare();snapshot("markdown-article-text-only");CHECK(!app.image_hit_count);
    CHECK(!app.font.italic);
    weidu_enter();CHECK(app.page==THINGSOS_UI_WEIDU && app.mp_mode==2);
    tap(100,275);CHECK(app.mp_mode==1);tap(100,275);CHECK(app.mp_mode==0);
    host_mp_available=false;host_mp_article[0]=0;
    /* Long HOME follows the entry menu, independent of book storage/type. */
    host_weidu.busy=false;host_weidu.background=false;app.wd_active=false;
    navigate(THINGSOS_UI_LIBRARY);CHECK(open_book(app.book_count));
    size_t home_position=app.start;capacitive_key(THINGSOS_KEY_HOME);
    CHECK(app.page==THINGSOS_UI_LIBRARY && app.start==home_position && !app.menu);
    capacitive_key(THINGSOS_KEY_HOME);CHECK(app.page==THINGSOS_UI_LIBRARY);
    weidu_enter();CHECK(open_book(app.book_count));
    navigate(THINGSOS_UI_CHAPTERS);capacitive_key(THINGSOS_KEY_HOME);
    CHECK(app.page==THINGSOS_UI_WEIDU && !app.weidu_detail && !app.wd_notes_open);
    CHECK(open_book(app.book_count));app.lock_return=THINGSOS_UI_READING;navigate(THINGSOS_UI_LOCK);unlock_screen();
    capacitive_key(THINGSOS_KEY_HOME);CHECK(app.page==THINGSOS_UI_WEIDU);
    CHECK(open_book(app.book_count));navigate(THINGSOS_UI_SETTINGS);navigate(THINGSOS_UI_READING);
    navigate(THINGSOS_UI_FONTS);capacitive_key(THINGSOS_KEY_HOME);CHECK(app.page==THINGSOS_UI_WEIDU);
    /* A cached WeRead title opened from Library still belongs to Library. */
    navigate(THINGSOS_UI_LIBRARY);app.weidu_detail=true;app.weidu_detail_return=THINGSOS_UI_LIBRARY;
    navigate(THINGSOS_UI_WEIDU);CHECK(open_book(app.book_count));
    capacitive_key(THINGSOS_KEY_HOME);CHECK(app.page==THINGSOS_UI_LIBRARY);
    navigate(THINGSOS_UI_APPS);navigate(THINGSOS_UI_TRANSFER);navigate(THINGSOS_UI_WIFI);navigate(THINGSOS_UI_PASSWORD);
    strcpy(app.wifi_password,"not-retained");capacitive_key(THINGSOS_KEY_HOME);
    CHECK(app.page==THINGSOS_UI_APPS && app.service_on && !app.wifi_password[0]);
    navigate(THINGSOS_UI_SETTINGS);navigate(THINGSOS_UI_WIFI);capacitive_key(THINGSOS_KEY_HOME);
    CHECK(app.page==THINGSOS_UI_SETTINGS);
    navigate(THINGSOS_UI_DISPLAY);capacitive_key(THINGSOS_KEY_HOME);CHECK(app.page==THINGSOS_UI_SETTINGS);
    navigate(THINGSOS_UI_FILES);CHECK(open_book(app.book_count));navigate(THINGSOS_UI_BOOKMARKS);
    capacitive_key(THINGSOS_KEY_HOME);CHECK(app.page==THINGSOS_UI_SETTINGS);
    weidu_enter();CHECK(open_book(app.book_count));app.wd_cloud_prompt=true;
    capacitive_key(THINGSOS_KEY_HOME);CHECK(app.page==THINGSOS_UI_WEIDU && !app.wd_cloud_prompt);
    /* Automatic notes send once only when online and idle; explicit manual
       mode and failed/uncertain records must never trigger background writes. */
    app.wd_active=false;release_book();host_sd=true;host_network_connected=true;host_weidu.busy=false;host_nvs_available=true;
    app.opened=true;app.page=THINGSOS_UI_READING;app.runtime.location.book_id=77;
    strcpy(app.wd_id,"55");strcpy(app.wd_owner,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    app.annotations=calloc(1,sizeof(*app.annotations));CHECK(app.annotations);
    remove("annotation-auto-test/000000000000004d-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.notes");
    remove("annotation-auto-test/000000000000004d-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.notes.bak");
    CHECK(thingsos_annotations_load(app.annotations,"annotation-auto-test",77,app.wd_owner));app.annotation_loaded=true;
    thingsos_annotation_t pending={0};pending.begin=0;pending.end=4;pending.source_end=4;
    strcpy(pending.uid,"1");strcpy(pending.quote,"Text");strcpy(pending.thought,"automatic");pending.sync=THINGSOS_NOTE_PENDING;
    CHECK(thingsos_annotations_put(app.annotations,&pending));
    app.menu=app.jump=app.footnote_open=app.wd_notes_open=app.wd_thought_open=app.wd_consent=app.wd_cloud_prompt=false;
    app.annotation_open=app.annotation_selecting=app.dirty=false;app.annotation_batch=false;
    app.reader_ready_at=app.activity_at=now();host_clock_us+=4000000;app.annotation_auto_after=0;
    host_note_request_allowed=true;unsigned auto_before=host_note_requests;
    app.annotation_manual=true;reader_annotations_auto_poll();CHECK(host_note_requests==auto_before);
    app.annotation_manual=false;host_network_connected=false;reader_annotations_auto_poll();CHECK(host_note_requests==auto_before);
    host_network_connected=true;reader_annotations_auto_poll();CHECK(host_note_requests==auto_before+1 && host_note_request_id==pending.id);
    CHECK(app.annotations->notes[0].sync==THINGSOS_NOTE_SENDING);
    app.annotation_batch=app.annotation_auto_job=false;host_weidu.busy=false;host_clock_us+=6000000;
    pending.sync=THINGSOS_NOTE_FAILED;CHECK(thingsos_annotations_put(app.annotations,&pending));reader_annotations_auto_poll();CHECK(host_note_requests==auto_before+1);
    pending.sync=THINGSOS_NOTE_UNCERTAIN;CHECK(thingsos_annotations_put(app.annotations,&pending));reader_annotations_auto_poll();CHECK(host_note_requests==auto_before+1);
    host_note_request_allowed=false;app.landscape_enabled=false;host_personal_available=true;
    strcpy(host_personal[0].note.uid,"1");host_personal[0].note.range=true;host_personal[0].note.begin=0;host_personal[0].note.length=4;strcpy(host_personal[0].quote,"Text");
    strcpy(host_personal[0].note.text,"automatic");host_personal[1]=host_personal[0];strcpy(host_personal[1].note.uid,"2");
    strcpy(host_personal[1].quote,"另一段划线原文");strcpy(host_personal[1].note.text,"保存在云端的想法");
    reader_annotations_enter();CHECK(app.annotation_list && app.annotation_cloud_count==1 && app.annotation_cloud_indices[0]==1);
    snapshot("reader-annotations-list");reader_annotations_dismiss();host_personal_available=false;
    navigate(THINGSOS_UI_READER_MORE);snapshot("reader-note-sync-auto");
    tap(500,950);CHECK(app.annotation_manual && host_note_manual==1);snapshot("reader-note-sync-manual");
    tap(590,950);CHECK(!app.annotation_manual && host_note_manual==0);
    app.landscape_enabled=true;reader_canvas_geometry();snapshot("reader-more-cloud-landscape");
    unsigned unchanged_mode=app.refresh_mode;
    tap(1030,430);CHECK(app.annotation_manual && app.refresh_mode==unchanged_mode && app.landscape_enabled);
    tap(1150,430);CHECK(!app.annotation_manual && app.refresh_mode==unchanged_mode && app.landscape_enabled);
    app.landscape_enabled=false;reader_canvas_geometry();
    app.annotation_manual=true;host_nvs_read=true;preferences_load();CHECK(!app.annotation_manual);host_nvs_read=false;
    /* Local notes retain their entry, with no cloud controls or hidden hit targets. */
    app.wd_id[0]=0;app.annotation_manual=true;app.landscape_enabled=false;
    snapshot("reader-more-local");
    unsigned local_commits=host_nvs_commits;
    tap(60,930);CHECK(app.annotation_manual && host_nvs_commits==local_commits);
    app.landscape_enabled=true;reader_canvas_geometry();snapshot("reader-more-local-landscape");
    tap(700,530);CHECK(app.annotation_manual && host_nvs_commits==local_commits);
    app.landscape_enabled=false;reader_canvas_geometry();tap(180,reader_more_note_top()+30);
    CHECK(app.page==THINGSOS_UI_READING && app.annotation_list);
    reader_refresh_config_test();physical_back_test();fast_font_comparison();reader_vertical_test();reader_immersive_test();reader_contact_test();
    reader_annotations_close();app.opened=false;
    /* Approved recording upload flow: explicit send, cancellation, duplicate
       choice and initial connection; no real network/credentials in UI tests. */
    host_sd=true;host_audio=(thingsos_audio_status_t){0};host_network_connected=true;
    audio_refresh_ack_test();launcher_audio_transfer_test();
    navigate(THINGSOS_UI_APPS);snapshot("apps-icons-prototype");
    for(unsigned icon=0;icon<5;++icon){int x=78+(int)(icon%3)*210,y=150+(int)(icon/3)*233;
        CHECK(app.canvas.pixels[(size_t)y*684+x]==255); /* rounded, not square */
        CHECK(app.canvas.pixels[(size_t)(y+1)*684+x+54]<128);
    }
    tap(82,180);CHECK(app.page==THINGSOS_UI_TRANSFER); /* no invisible Back hitbox */
    tap(500,250);CHECK(app.transfer_music);snapshot("transfer-music");
    host_network_busy=true;tap(150,250);CHECK(app.transfer_music);host_network_busy=false;
    tap(150,250);CHECK(!app.transfer_music);snapshot("transfer-books");navigate(THINGSOS_UI_APPS);
    tap(552,220);CHECK(app.page==THINGSOS_UI_FILES);snapshot("files-app-tab");
    CHECK(app.canvas.pixels[1203u*684+410]==0&&app.canvas.pixels[1203u*684+80]==255);
    strlcpy(app.browser.path,"/sdcard/audio",sizeof(app.browser.path));snapshot("files-audio-app-tab");
    CHECK(app.canvas.pixels[1203u*684+410]==0&&app.canvas.pixels[1203u*684+80]==255);
    /* The file-search editor belongs to the same Applications section. */
    app.page=THINGSOS_UI_PASSWORD;app.input_return=THINGSOS_UI_FILES;memset(app.canvas.pixels+1098u*684,255,(1216u-1098)*684);nav();
    CHECK(app.canvas.pixels[1203u*684+410]==0&&app.canvas.pixels[1203u*684+80]==255);
    navigate(THINGSOS_UI_APPS);tap(237,220);CHECK(app.page==THINGSOS_UI_APPS); /* grid gutter */
    /* Approved V2 control sheet: geometry and actions share the real tap path. */
    app.light_mode=app.light_last_mode=3;app.light_level=45;app.light_warmth=50;app.audio_volume=35;app.wifi_enabled=true;
    app_controls_set(true);CHECK(app.controls_refresh);snapshot("control-center-v2");
    /* Second tile row (Bluetooth, vibration) moves portrait sliders by 139. */
    tap(68,936);CHECK(app.audio_volume==30); /* Minus, never a mute toggle. */
    tap(616,936);CHECK(app.audio_volume==35);
    tap(122,936);CHECK(app.audio_volume==0);tap(562,936);CHECK(app.audio_volume==100);
    app.controls_refresh=false;tap(500,1000);CHECK(!app.controls_open&&app.controls_refresh&&app.audio_volume==100); /* no hidden mute row; closing still uses overlay refresh */
    app_controls_set(true);tap(500,250);CHECK(!app.light_mode&&app.light_level==45);
    tap(616,585);CHECK(app.light_level==45);snapshot("control-center-v2-light-off");
    tap(500,250);CHECK(app.light_mode==3&&app.light_level==45);
    tap(616,585);CHECK(app.light_level==46);tap(68,585);CHECK(app.light_level==45);
    tap(616,733);CHECK(app.light_warmth==60);tap(68,733);CHECK(app.light_warmth==50);
    tap(340,250);CHECK(app.controls_open); /* tile gutter */
    tap(340,406);CHECK(app.controls_open); /* second-row gutter */
    /* Vibration tile mirrors the key-vibration setting and buzzes on enable. */
    app.haptic_enabled=false;host_haptic_ms=0;tap(500,406);CHECK(app.haptic_enabled&&host_haptic_ms==50&&app.controls_open);
    host_haptic_ms=0;tap(500,406);CHECK(!app.haptic_enabled&&!host_haptic_ms);app.haptic_enabled=true;
    /* Bluetooth tile: off disconnects BLE and stops auto-reconnect; on
       re-enables it and reconnects the last page turner. */
    thingsos_ble_device_t turner={.device={"A1B2C3D4E5F6","Page Turner"},.connectable=true};
    host_ble=(thingsos_ble_status_t){.automatic=true,.connected=true,.phase=THINGSOS_BLE_CONNECTED,.current=turner,.last=turner};
    host_bt=(thingsos_bt_status_t){.automatic=true};
    snapshot("control-center-v2-bluetooth-on");
    tap(150,406);CHECK(!host_ble.automatic&&!host_ble.connected&&app.controls_open);
    snapshot("control-center-v2-bluetooth-off");
    tap(150,406);CHECK(host_ble.automatic&&host_ble.connected&&!strcmp(host_ble.current.device.address,"A1B2C3D4E5F6"));
    /* A headset carrying audio is never cut from the control center. */
    host_bt.enabled=host_bt.connected=true;host_bt.phase=THINGSOS_BT_CONNECTED;host_audio.state=THINGSOS_AUDIO_PLAYING;
    tap(150,406);CHECK(host_bt.enabled&&host_bt.connected&&host_ble.automatic);
    host_audio.state=THINGSOS_AUDIO_IDLE;host_bt=(thingsos_bt_status_t){.automatic=true};
    /* Long press and the chevron open the Bluetooth page searching; Back returns. */
    CHECK(app_hold(150,406,600));CHECK(app.page==THINGSOS_UI_BLUETOOTH&&!app.controls_open&&host_ble.scanning);
    CHECK(bt_ui_key(THINGSOS_KEY_PREVIOUS));CHECK(app.page==THINGSOS_UI_APPS&&app.controls_open&&!host_ble.scanning);
    tap(307,406);CHECK(app.page==THINGSOS_UI_BLUETOOTH&&!app.controls_open);
    CHECK(bt_ui_key(THINGSOS_KEY_PREVIOUS));CHECK(app.page==THINGSOS_UI_APPS&&app.controls_open);
    host_ble=(thingsos_ble_status_t){0};
    CHECK(app_hold(150,250,600));CHECK(app.page==THINGSOS_UI_WIFI&&!app.controls_open);
    CHECK(app_back());CHECK(app.page==THINGSOS_UI_APPS&&app.controls_open);
    tap(307,250);CHECK(app.page==THINGSOS_UI_WIFI&&!app.controls_open);
    CHECK(app_back());CHECK(app.controls_open);
    CHECK(app_controls_swipe(342,1020,0,-100)&&!app.controls_open);
    CHECK(app_controls_swipe(342,30,0,100)&&app.controls_open);
    app_controls_set(false);app.canvas.width=1216;app.canvas.height=684;
    app_controls_set(true);thingsos_rect(&app.canvas,0,0,1216,684,255);app_controls_draw();canvas_snapshot("control-center-v2-landscape");
    unsigned prior_light=app.light_level;CHECK(app_controls_tap(1148,230));CHECK(app.light_level==prior_light+1);
    app_controls_set(false);reader_canvas_geometry();app.audio_volume=35;
    host_ima=(thingsos_ima_status_t){.configured=true,.count=1,.serial=1};
    host_bt=(thingsos_bt_status_t){.automatic=true};navigate(THINGSOS_UI_SETTINGS);snapshot("bluetooth-settings-entry");
    tap(100,430);CHECK(app.page==THINGSOS_UI_BLUETOOTH);
    tap(100,400);CHECK(host_bt.enabled&&host_bt.phase==THINGSOS_BT_SCANNING&&host_ble.scanning);
    /* BLE arrives first; later audio must occupy the first row without
       disturbing discovery order inside each transport or duplicating rows. */
    thingsos_ble_device_t early_ble={.device={"AABBCCDDEE01","Early BLE"},.connectable=true};
    thingsos_ble_device_t late_audio={.device={"10973779E5C0","10973779E5C0"},.connectable=true};
    bluetooth_ui->found_count=0;CHECK(bt_ui_found_add(&early_ble,true));CHECK(bt_ui_found_add(&late_audio,false));
    CHECK(bluetooth_ui->found_count==2&&!bluetooth_ui->found[0].ble&&bluetooth_ui->found[1].ble);
    CHECK(!bt_ui_found_add(&late_audio,false)&&bluetooth_ui->found_count==2);
    bluetooth_ui->found_count=0;
    host_bt.found_count=1;strcpy(host_bt.found[0].address,"001122334455");strcpy(host_bt.found[0].name,"我的蓝牙耳机");++host_bt.serial;
    host_ble.count=6;for(unsigned i=0;i<6;++i){snprintf(host_ble.devices[i].device.name,96,"BLE Keyboard %u",i+1);snprintf(host_ble.devices[i].device.address,13,"00112233%04X",i);host_ble.devices[i].rssi=-45-(int)i;host_ble.devices[i].connectable=true;}++host_ble.serial;
    /* A dual-mode device can advertise the same MAC on both transports.
       Preserve its typed BLE identity and dispatch each row independently. */
    strcpy(host_ble.devices[0].device.address,host_bt.found[0].address);
    /* Address-only advertisers are not listed as their raw MAC code. */
    host_ble.devices[6]=(thingsos_ble_device_t){.address_type=1};strcpy(host_ble.devices[6].device.address,"7A1B2C3D4E5F");strcpy(host_ble.devices[6].device.name,"7A1B2C3D4E5F");host_ble.count=7;
    bt_ui_poll();CHECK(bt_ui_count()==7&&!bluetooth_ui->view);
    for(unsigned i=0;i<bt_ui_count();++i){const thingsos_ble_device_t* ble;const thingsos_bt_device_t* d=bt_ui_at(i,&ble);CHECK(strcmp(d->name,d->address));}
    /* Discovery repaints use the differential region and are batched: RSSI
       samples never repaint, and row changes wait for the 2 s window. */
    app.message[0]=0;app.message_until=0;app.feedback_refresh=app.force_refresh=app.transition_refresh=app.controls_refresh=false;
    app.lock_wave_pending=false;draw();bt_ui_presented(true);CHECK(bt_ui_region());app.dirty=false;
    host_ble.devices[1].rssi=-80;++host_ble.serial;bt_ui_poll();CHECK(!app.dirty&&!bluetooth_ui->pending);
    strcpy(host_ble.devices[5].device.name,"BLE Keyboard 6 Pro");++host_ble.serial;bt_ui_poll();CHECK(!app.dirty&&bluetooth_ui->pending);
    host_clock_us+=1000000;bt_ui_poll();CHECK(!app.dirty);
    host_clock_us+=1000000;bt_ui_poll();CHECK(app.dirty&&bt_ui_region());
    draw();bt_ui_presented(true);CHECK(!bluetooth_ui->pending);app.dirty=false;
    strcpy(host_ble.devices[5].device.name,"BLE Keyboard 6");++host_ble.serial;bt_ui_poll();host_clock_us+=2000000;bt_ui_poll();
    draw();bt_ui_presented(true);app.dirty=false;
    unsigned ble_scale=app.ui_px;app.ui_px=50;snapshot("bluetooth-unified-discovery-font50");app.ui_px=ble_scale;
    snapshot("bluetooth-unified-discovered");tap(180,505);CHECK(bluetooth_ui->view==1&&!bluetooth_ui->confirm_pair);
    tap(300,520);CHECK(bluetooth_ui->confirm_pair);snapshot("bluetooth-pair");
    tap(300,645);CHECK(host_bt.connected&&host_bt.saved_count==1&&!host_ble.connected);snapshot("bluetooth-connected");
    tap(200,750);CHECK(!host_bt.automatic);tap(560,750);CHECK(host_bt.automatic);
    tap(300,970);CHECK(!host_bt.enabled&&host_bt.saved_count==1);
    tap(300,520);tap(300,645);CHECK(host_bt.enabled&&host_bt.connected); /* direct reconnect from off */
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(!bluetooth_ui->view);
    tap(300,615);CHECK(bluetooth_ui->view==3&&!bluetooth_ui->confirm_pair&&host_bt.connected);
    tap(300,520);CHECK(bluetooth_ui->ble_confirm);snapshot("bluetooth-ble-pair-confirm");
    tap(300,645);CHECK(host_ble.connected&&host_ble.hid_ready&&host_ble.saved_count==1&&host_bt.connected);
    snapshot("bluetooth-ble-connected");
    host_ble.pairing=THINGSOS_BLE_PAIR_DISPLAY;host_ble.passkey=537201;++host_ble.serial;bt_ui_poll();snapshot("bluetooth-ble-passkey-display");
    host_ble.pairing=THINGSOS_BLE_PAIR_CONFIRM;++host_ble.serial;bt_ui_poll();tap(300,680);CHECK(host_ble_pair_accept&&host_ble.pairing==THINGSOS_BLE_PAIR_NONE);
    host_ble.pairing=THINGSOS_BLE_PAIR_INPUT;++host_ble.serial;bt_ui_poll();
    for(unsigned i=0;i<6;++i)tap(100+(int)(i%3)*176,580+(int)(i/3)*86);
    tap(500,980);CHECK(host_ble_pair_passkey==123456&&host_ble_pair_accept);
    tap(300,520);CHECK(!host_ble.connected&&host_bt.connected);
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(!bluetooth_ui->view);
    tap(500,1070);CHECK(bluetooth_ui->list_start==5);tap(100,1070);CHECK(!bluetooth_ui->list_start);
    tap(300,400);CHECK(!host_ble.scanning&&host_bt.connected); /* stopping search preserves an existing audio link */
    tap(300,400);CHECK(host_ble.scanning&&host_bt.phase==THINGSOS_BT_SCANNING);
    tap(300,400);CHECK(!host_ble.scanning&&host_bt.phase==THINGSOS_BT_READY);
    tap(500,400);CHECK(bluetooth_ui->list_saved&&bt_ui_count()==2);snapshot("bluetooth-unified-saved");
    tap(300,505);CHECK(bluetooth_ui->view==1);tap(300,620);CHECK(bluetooth_ui->confirm_forget);
    tap(300,740);CHECK(!bluetooth_ui->confirm_forget&&host_bt.saved_count==1);
    tap(300,620);tap(300,645);CHECK(!host_bt.saved_count&&!bluetooth_ui->view&&host_ble.saved_count==1);
    tap(300,505);CHECK(bluetooth_ui->view==3);tap(300,620);CHECK(bluetooth_ui->ble_confirm&&bluetooth_ui->ble_forget);
    tap(300,645);CHECK(!host_ble.saved_count);
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(!bluetooth_ui->view&&bt_ui_count()==0);
    tap(300,400);CHECK(host_ble.scanning&&host_bt.phase==THINGSOS_BT_SCANNING);
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(app.page==THINGSOS_UI_SETTINGS&&!host_ble.scanning&&host_bt.phase==THINGSOS_BT_READY);
    /* A historical BLE error is not a live Wi-Fi resource verdict on entry.
       Fresh failures remain visible briefly; expiry redraws without clearing
       worker state or changing either radio. */
    host_ble.phase=THINGSOS_BLE_ERROR;strcpy(host_ble.message,"低功耗 DMA 不足，音频搜索仍可用");++host_ble.serial;
    bt_ui_enter();CHECK(!bt_ui_ble_error_visible()&&bluetooth_ui->ble.phase==THINGSOS_BLE_ERROR);
    ++host_ble.serial;bt_ui_poll();CHECK(bt_ui_ble_error_visible());
    draw();canvas_snapshot("bluetooth-ble-dma-notice");
    app.dirty=false;host_clock_us+=8001000;bt_ui_poll();CHECK(!bt_ui_ble_error_visible()&&app.dirty);
    CHECK(host_ble.phase==THINGSOS_BLE_ERROR&&!host_ble.scanning);
    draw();canvas_snapshot("bluetooth-ble-dma-notice-expired");
    host_audio.state=THINGSOS_AUDIO_PLAYING;host_ble_started_before_audio_pause=false;
    bt_ui_search(true);CHECK(!host_ble_started_before_audio_pause&&host_audio.state==THINGSOS_AUDIO_PLAY_PAUSED);
    bt_ui_search(false);host_audio.state=THINGSOS_AUDIO_IDLE;
    navigate(THINGSOS_UI_SETTINGS);
    feature_editor(5);CHECK(app.page==THINGSOS_UI_PASSWORD);CHECK(thingsos_input_open(&app.input,"",96,false));app.input.chinese=false;
    thingsos_hid_key_t ble_key={.page=7,.usage=0x0b,.modifiers=2};ble_key_dispatch(&ble_key);
    ble_key.usage=0x0c;ble_key.modifiers=0;ble_key_dispatch(&ble_key);CHECK(!strcmp(app.input.text,"Hi"));
    ble_key.usage=0x50;ble_key_dispatch(&ble_key);ble_key.usage=0x2a;ble_key_dispatch(&ble_key);CHECK(!strcmp(app.input.text,"i"));
    ble_key.usage=0x4c;ble_key_dispatch(&ble_key);CHECK(!app.input.length);
    ble_key.usage=0x2c;ble_key.modifiers=1;ble_key_dispatch(&ble_key);CHECK(app.input.chinese);
    ble_key.modifiers=0;ble_key.usage=0x11;ble_key_dispatch(&ble_key);ble_key.usage=0x0c;ble_key_dispatch(&ble_key);CHECK(!strcmp(app.input.composition,"ni"));
    ble_key.usage=0x2c;ble_key_dispatch(&ble_key);CHECK(!app.input.composition[0]&&app.input.length);
    ble_key.usage=0x2a;ble_key_dispatch(&ble_key);CHECK(!app.input.length);
    ble_key.usage=4;host_ble_keys[0]=ble_key;host_ble_key_count=1;app.asleep=true;ble_input_poll();CHECK(!app.input.length&&!host_ble_key_count);
    app.asleep=false;host_ble_keys[0]=ble_key;host_ble_key_count=1;ble_input_poll();CHECK(!app.input.length&&!host_ble_key_count);
    ble_key.usage=0x29;ble_key_dispatch(&ble_key);CHECK(app.page==THINGSOS_UI_SETTINGS);
    navigate(THINGSOS_UI_TIME);feature_editor(2);ble_key.usage=0x29;ble_key_dispatch(&ble_key);CHECK(app.page==THINGSOS_UI_TIME);
    bool ble_prior_auto_time=app.auto_time;feature_editor(2);CHECK(thingsos_input_open(&app.input,"2026-11-17 18:30",16,false));
    ble_key.usage=0x28;ble_key_dispatch(&ble_key);CHECK(app.page==THINGSOS_UI_TIME);app.auto_time=ble_prior_auto_time;navigate(THINGSOS_UI_SETTINGS);
    /* AI key settings, background recording, hardware-independent test action. */
    navigate(THINGSOS_UI_SETTINGS);app.controls_open=false;app.opened=false;
    snapshot("settings-ai-entry");
    tap(300,530);CHECK(app.page==THINGSOS_UI_AI_SETTINGS);
    snapshot("ai-settings");
    tap(440,390);CHECK(!app.ai_key_enabled); /* left segment = off */
    tap(560,390);CHECK(app.ai_key_enabled);  /* right segment = recording */
    host_audio=(thingsos_audio_status_t){0};app.audio=host_audio;
    tap(300,590);CHECK(host_audio.state==THINGSOS_AUDIO_RECORDING);
    app_poll();snapshot("ai-settings-recording");
    tap(300,590);CHECK(host_audio.state==THINGSOS_AUDIO_IDLE);
    app_poll();
    tap(440,390);CHECK(!app.ai_key_enabled);
    host_audio=(thingsos_audio_status_t){0};app.audio=host_audio;
    tap(300,590);CHECK(host_audio.state==THINGSOS_AUDIO_IDLE);
    tap(560,390);CHECK(app.ai_key_enabled);
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(app.page==THINGSOS_UI_SETTINGS);
    /* Volume OSD is transient and does not use the bottom message banner. */
    app.audio_volume=55;app.volume_overlay_until=now()+1000;app.volume_overlay_pending=false;
    draw();canvas_snapshot("volume-overlay");
    CHECK(app.volume_overlay_until>now()&&!app.message[0]);
    host_clock_us+=1700000;message_expire_poll();CHECK(!app.volume_overlay_until);
    /* Portrait control-center audio card: pause and ±15 s use real taps. */
    app.page=THINGSOS_UI_APPS;app.controls_open=false;
    host_audio=(thingsos_audio_status_t){.state=THINGSOS_AUDIO_PLAYING,.position_ms=30000,.duration_ms=180000,.volume=35,.serial=10};
    strlcpy(host_audio.path,"/sdcard/music/夜航.mp3",sizeof(host_audio.path));app.audio=host_audio;
    app_controls_set(true);thingsos_rect(&app.canvas,0,0,684,1216,255);app_controls_draw();
    canvas_snapshot("control-center-music-card");
    tap(339,981+139);CHECK(host_audio.state==THINGSOS_AUDIO_PLAY_PAUSED);app.audio=host_audio;
    thingsos_rect(&app.canvas,0,0,684,1216,255);app_controls_draw();canvas_snapshot("control-center-music-card-paused");
    tap(83,983+139);CHECK(host_audio.position_ms==15000);app.audio=host_audio;
    tap(541,983+139);CHECK(host_audio.position_ms==30000);app.audio=host_audio;
    host_audio.state=THINGSOS_AUDIO_RECORDING;host_audio.position_ms=42000;
    strlcpy(host_audio.path,"/sdcard/audio/REC-host.wav",sizeof(host_audio.path));app.audio=host_audio;
    thingsos_rect(&app.canvas,0,0,684,1216,255);app_controls_draw();canvas_snapshot("control-center-recording-card");
    tap(300,990+139);CHECK(host_audio.state==THINGSOS_AUDIO_IDLE);app_poll();
    app_controls_set(false);host_audio=(thingsos_audio_status_t){0};app.audio=host_audio;
    transient_message_test();
    strlcpy(host_ima.kb_name,"测试知识库",sizeof(host_ima.kb_name));
    navigate(THINGSOS_UI_RECORDER);ima_poll();tap(560,140);CHECK(ima_ui->view==1);
    snapshot("ima-settings");tap(300,580);CHECK(ima_ui->view==3);ima_poll();draw();
    snapshot("ima-choose-library");tap(200,370);CHECK(ima_ui->view==1);
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(!ima_ui->view);
    ima_prepare("/sdcard/audio/REC-test.wav");CHECK(ima_ui->view==4&&!host_ima.busy);snapshot("ima-upload-confirm");
    tap(300,800);ima_poll();CHECK(host_ima.busy&&ima_ui->view==4);
    tap(300,820);ima_poll();CHECK(!host_ima.busy&&host_ima.job.phase==IMA_CANCELLED);
    host_ima.job.phase=IMA_DUPLICATE;++host_ima.serial;ima_poll();CHECK(ima_ui->view==6);snapshot("ima-duplicate");
    tap(500,670);ima_poll();CHECK(host_ima_keep&&host_ima.busy);
    host_ima.busy=false;host_ima.job.phase=IMA_UNCERTAIN;strcpy(host_ima.job.media,"m-test");++host_ima.serial;ima_poll();
    tap(300,820);ima_poll();CHECK(host_ima.job.phase==IMA_UPLOADED);snapshot("ima-upload-complete");
    ima_view(0);host_ima.configured=false;ima_prepare("/sdcard/audio/REC-test.wav");CHECK(ima_ui->view==7);
    tap(500,670);ima_poll();CHECK(ima_ui->view==2&&host_ima.pairing);
    /* The pairing page carries the LAN address as a scan target: the QR box
       must be inked and the address line printed under it. Without a network
       the same page must fall back to the "connect WiFi" prompt. */
    bool ima_wifi=host_network_connected;host_network_connected=true;
    host_ima.pairing=true;host_ima.pair_seconds=300;
    strlcpy(host_ima.code,"12345678",sizeof(host_ima.code));
    ima_poll();
    snapshot("ima-phone-pairing");
    CHECK(!region_ink(212,240)); /* Content starts below the header divider. */
    {char link[128];ima_pair_link(link,sizeof(link));CHECK(!strcmp(link,"http://192.168.1.42:8081/"));}
    {   /* QR modules sit strictly inside the 300 px box and above the code. */
        unsigned qr=0,code=0;
        for(int yy=490;yy<520;++yy)for(int xx=190;xx<500;++xx)qr+=app.canvas.pixels[(size_t)yy*684+xx]<128;
        for(int yy=735;yy<780;++yy)for(int xx=36;xx<648;++xx)code+=app.canvas.pixels[(size_t)yy*684+xx]<128;
        CHECK(qr>100 && code>0);
        char address[96];thingsos_network_address(false,address,sizeof(address));
        CHECK(strstr(address,"192.168.1.42"));
    }
    host_network_connected=false;draw();
    {   unsigned qr=0;for(int yy=490;yy<520;++yy)for(int xx=190;xx<500;++xx)qr+=app.canvas.pixels[(size_t)yy*684+xx]<128;
        CHECK(qr==0); }
    /* Reported defect: the QR was drawn whenever the address existed, even
       while the HTTPS server had never started, so the phone hit a closed
       port. No pairing session means no code and no QR. Checked with the
       account unconfigured so the poll does not move to the library view. */
    host_network_connected=ima_wifi;
    host_ima.configured=false;host_ima.busy=false;
    host_ima.pairing=false;host_ima.code[0]=0;host_ima.pair_seconds=0;
    ima_ui->view=2;ima_poll();draw();
    {   unsigned qr=0;for(int yy=490;yy<520;++yy)for(int xx=190;xx<500;++xx)qr+=app.canvas.pixels[(size_t)yy*684+xx]<128;
        CHECK(qr==0); }
    host_ima.pairing=true;strlcpy(host_ima.code,"12345678",sizeof(host_ima.code));
    host_ima.pair_seconds=300;ima_poll();draw();
    {bool asleep=app.asleep;uint64_t activity=app.activity_at;app.asleep=false;app.activity_at=0;
        auto_sleep_poll(900000);CHECK(!app.asleep);app.asleep=asleep;app.activity_at=activity;}
    host_ima.config_phase=THINGSOS_IMA_CONFIG_VERIFYING;host_ima.busy=true;++host_ima.serial;
    strlcpy(host_ima.message,"正在验证 ima 凭证并获取知识库…",sizeof(host_ima.message));ima_poll();
    snapshot("ima-config-verifying");CHECK(region_ink(340,390)>0&&region_ink(430,520)>0);
    host_ima.config_phase=THINGSOS_IMA_CONFIG_FAILED;host_ima.busy=false;++host_ima.serial;
    strlcpy(host_ima.message,"ima 服务返回 HTTP 403",sizeof(host_ima.message));ima_poll();
    snapshot("ima-config-failed");CHECK(region_ink(340,390)>0&&region_ink(430,520)>0);
    tap(300,910);ima_poll();draw();CHECK(ima_ui->pair_retry&&ima_ui->view==2);
    {unsigned qr=0;for(int yy=490;yy<520;++yy)for(int xx=190;xx<500;++xx)qr+=app.canvas.pixels[(size_t)yy*684+xx]<128;CHECK(qr>100);}
    host_ima.pairing=false;ima_poll();tap(300,910);ima_poll();CHECK(host_ima.pairing&&host_ima.config_phase==THINGSOS_IMA_CONFIG_IDLE);
    host_ima.config_phase=THINGSOS_IMA_CONFIG_VERIFYING;host_ima.busy=true;++host_ima.serial;ima_poll();CHECK(!ima_ui->pair_retry);
    host_ima.config_phase=THINGSOS_IMA_CONFIG_READY;
    /* Configuration can become ready one poll before the worker releases busy. */
    host_ima.configured=true;host_ima.busy=true;++host_ima.connection_serial;++host_ima.serial;ima_poll();CHECK(ima_ui->view==2);
    host_ima.busy=false;++host_ima.serial;ima_poll();CHECK(ima_ui->view==3);
    ima_view(2);capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(!host_ima.pairing&&ima_ui->view==1);
    ima_view(0);strcpy(app.audio.path,"/sdcard/audio/REC-new.wav");app.audio.state=THINGSOS_AUDIO_IDLE;
    ima_save_pending();ima_poll();CHECK(ima_ui->view==5&&!strcmp(ima_ui->path,app.audio.path));snapshot("ima-recording-saved");
    tap(100,670);CHECK(!ima_ui->view);free(ima_ui);ima_ui=NULL;memset(&host_ima,0,sizeof(host_ima));
    recording_feedback_test();
    /* Lock/wake full ripple ignores the reader ripple switch/fast mode.
       A failed presentation keeps the transition pending for retry. */
    app.ripple_enabled=false;app.ui_refresh_mode=1;app.asleep=false;app.wd_active=false;
    navigate(THINGSOS_UI_LIBRARY);app.lock_return=THINGSOS_UI_LIBRARY;
    navigate(THINGSOS_UI_LOCK);CHECK(app.lock_wave_pending&&app.lock_wave_forward);
    host_lock_result=ESP_FAIL;CHECK(present_lock_transition()==ESP_FAIL&&app.lock_wave_pending);
    host_lock_result=ESP_OK;CHECK(present_lock_transition()==ESP_OK&&!app.lock_wave_pending&&host_lock_forward);
    unlock_screen();CHECK(app.page==THINGSOS_UI_LIBRARY&&app.lock_wave_pending&&!app.lock_wave_forward);
    CHECK(present_lock_transition()==ESP_OK&&!app.lock_wave_pending&&!host_lock_forward);
    reader_swipe_test();
    lock_pin_controller_test();
    alarm_controller_test();
    online_reader_test();
    readtime_controller_test();
    txt_source_controller_test();
    system_monitor_controller_test();
    cubox_controller_test(fixture_root);
    thingsos_font_close(&app.font);
    thingsos_font_close(&app.ui_font);
    thingsos_thumbnail_clear(&app.thumbnails);
    free(app.canvas.pixels);
    free(state);
    puts(
        "PASS: actual device draw/tap controller, page recovery, cross-chapter previous, filtered "
        "bookmarks, font transactions and no-SD example");
    return 0;
}
