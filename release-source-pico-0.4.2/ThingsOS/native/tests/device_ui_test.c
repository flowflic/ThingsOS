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
static void snapshot(const char*);
static void charging_status_test(void) {
    unsigned saved_battery=app.battery;
    bool saved_connected=app.charge_connected;
    app.power_sampled=0;app.power_status_pending=false;
    app.battery=50;app.charge_connected=false;
    host_battery_percent=50;host_charge_connected=false;host_power_valid=true;
    unsigned samples=host_power_samples;
    CHECK(!power_status_poll(1000) && host_power_samples==samples+1);
    host_charge_connected=true;
    CHECK(!power_status_poll(1999) && !app.charge_connected && host_power_samples==samples+1);
    CHECK(power_status_poll(2000) && app.charge_connected && app.power_status_pending);
    app.power_status_pending=false;
    CHECK(!power_status_poll(3000) && !app.power_status_pending);
    host_power_valid=false;host_battery_percent=0;host_charge_connected=false;
    CHECK(!power_status_poll(4000) && app.battery==50 && app.charge_connected && !app.power_status_pending);
    host_power_valid=true;host_battery_percent=100;host_charge_connected=true;
    CHECK(power_status_poll(5000) && app.battery==100 && app.charge_connected);
    app.power_status_pending=false;host_charge_connected=false;
    CHECK(power_status_poll(6000) && !app.charge_connected && app.power_status_pending);
    app.power_status_pending=false;CHECK(!power_status_poll(7000) && !app.power_status_pending);

    /* Compare actual status pixels: the marker stays inside the battery and
       remains visible at both ends of the gauge and across its fill edge. */
    const unsigned levels[]={0,1,40,50,99,100};
    unsigned char* unplugged=malloc(684u*74u);CHECK(unplugged);
    for(unsigned i=0;i<sizeof(levels)/sizeof(levels[0]);++i) {
        app.battery=levels[i];app.charge_connected=false;draw_status();
        memcpy(unplugged,app.canvas.pixels,684u*74u);
        app.charge_connected=true;draw_status();unsigned changed=0,black=0;
        for(unsigned y=0;y<74;++y)for(unsigned x=0;x<684;++x) {
            if(app.canvas.pixels[y*684+x]!=unplugged[y*684+x]) {
                CHECK(x>=630 && x<=643 && y>=29 && y<=45);++changed;
            }
            if(x>=630 && x<=643 && y>=29 && y<=45 && app.canvas.pixels[y*684+x]<128)++black;
        }
        CHECK(changed>45 && black>45);
        app.charge_connected=false;draw_status();CHECK(!memcmp(unplugged,app.canvas.pixels,684u*74u));
    }
    free(unplugged);
    app.battery=50;app.charge_connected=true;snapshot("status-charging-half");
    app.battery=100;snapshot("status-charger-full");
    app.charge_connected=false;snapshot("status-unplugged-full");
    app.charge_connected=true;draw_status();
    unsigned char portrait_battery[54*23];
    for(unsigned y=0;y<23;++y)
        memcpy(portrait_battery+y*54,app.canvas.pixels+(26+y)*684+612,54);
    app.landscape_enabled=true;reader_canvas_geometry();app.charge_connected=true;
    snapshot("status-charging-landscape");
    for(unsigned y=0;y<23;++y)
        CHECK(!memcmp(portrait_battery+y*54,app.canvas.pixels+(26+y)*app.canvas.width+app.canvas.width-72,54));
    app.landscape_enabled=false;reader_canvas_geometry();
    app.battery=saved_battery;app.charge_connected=saved_connected;
    app.power_sampled=0;app.power_status_pending=false;
    host_battery_percent=100;host_charge_connected=false;
}
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
    CHECK(online_open(&book));online_poll();CHECK(online.running);
    app.page=THINGSOS_UI_LIBRARY;host_weidu.busy=false;host_weidu.phase=WD_READY;
    online_poll();CHECK(!online.opening && app.page==THINGSOS_UI_LIBRARY);
    memset(&online,0,sizeof(online));host_weidu.busy=false;
    strcpy(book.id,"1234567890");app.page=THINGSOS_UI_WEIDU;
    CHECK(online_open(&book) && online.opening && !app.opened);
    CHECK(online_modal_visible() && online.progress==2 && online.total==3);
    draw();snapshot("online-loading-card-initial");online_modal_presented(true);
    online_card_rect_t card=online_card_rect(),ring=online_spinner_rect();
    CHECK(card.x==76 && card.y==393 && card.w==532 && card.h==430);
    tap(10,10);CHECK(online.opening && app.page==THINGSOS_UI_WEIDU);
    online_poll();CHECK(online.running && host_weidu_chapter==0 && host_weidu_chapters==3);
    CHECK(!app.opened); /* Current chapter alone must not bypass warm-up. */
    host_weidu.busy=false;host_weidu.phase=WD_ERROR;++host_weidu.serial;
    online_poll();CHECK(online.error && online_modal_visible() && !app.opened);
    draw();snapshot("online-loading-card-error");
    host_network_connected=false;tap(card.x+100,card.y+350);CHECK(online.error && !host_weidu.busy);
    host_network_connected=true;tap(card.x+100,card.y+350);CHECK(!online.error && online.opening);
    online_poll();CHECK(online.running);
    online_arrive(2);host_weidu.busy=false;host_weidu.phase=WD_READY;
    online_poll();CHECK(app.opened && app.chapter_index==0 && !online_pending());
    CHECK(load_chapter(1) && load_chapter(2));
    app.activity_at=now();online_poll();CHECK(online.running && host_weidu_chapter==3 && host_weidu_chapters==2);
    unsigned calls=host_weidu_calls;
    online_need(4);online_poll();CHECK(online.waiting && host_weidu_calls==calls && host_weidu.busy);
    online_need(5);online_poll();CHECK(online.waiting && host_weidu_chapter==5 && host_weidu_chapters==3);
    online_arrive(5);online_poll();CHECK(app.chapter_index==5 && !online.waiting && host_weidu.busy);
    host_weidu.busy=false;host_weidu.phase=WD_READY;online_poll();
    CHECK(online.retry_at==UINT64_MAX); /* Last chapter has no future work. */
    CHECK(load_chapter(3));app.start=2;online_need(4);CHECK(online_modal_visible());
    draw();snapshot("online-loading-card-chapter");
    app.landscape_enabled=true;draw();snapshot("online-loading-card-landscape");
    card=online_card_rect();CHECK(app.canvas.width==1216 && app.canvas.height==684 && card.x==342 && card.y==127);
    app.landscape_enabled=false;draw();
    unsigned index=app.chapter_index;capacitive_key(THINGSOS_KEY_NEXT);CHECK(app.chapter_index==index && online.waiting);
    card=online_card_rect();tap(card.x+250,card.y+350);CHECK(!online_pending() && app.chapter_index==index && app.start==2);
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
    for(unsigned mono=0;mono<2;++mono)for(unsigned wide=0;wide<2;++wide) {
        memset(full,255,READER_CACHE_BYTES);memset(sliced,255,READER_CACHE_BYTES);
        memset(a,0,sizeof(*a));memset(b,0,sizeof(*b));
        thingsos_canvas_t ca={full,wide?1216:684,wide?684:1216},cb={sliced,ca.width,ca.height};
        app.font.monochrome=mono!=0;thingsos_font_size(&app.font,app.px);
        size_t end=thingsos_text_page_layout_flow(&app.font,&ca,&app.chapter,app.start,24,90,
            ca.width-48,ca.height-176,145,app.px*2,app.epub_open,NULL,NULL,a,false,16);
        thingsos_text_cursor_t cursor={0};bool completed=false;size_t got=app.start;
        unsigned slices=0;
        while(!completed) {
            size_t before=got;unsigned rows=2;
            got=thingsos_text_page_resume_flow(&app.font,&cb,&app.chapter,app.start,24,90,
                cb.width-48,cb.height-176,145,app.px*2,app.epub_open,NULL,NULL,b,
                resume_rows,&rows,&completed,&cursor,false,16);
            CHECK(++slices<100 && (completed || (cursor.active && got>before)));
        }
        CHECK(slices>1 && got==end && !cursor.active);
        CHECK(!memcmp(full,sliced,READER_CACHE_BYTES));
        CHECK(!memcmp(a,b,sizeof(*a)));
    }
    app.font.monochrome=app.refresh_mode==1;
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
    CHECK(reading_menu_press(550,1150) && app.page==THINGSOS_UI_READER_MORE);
    unsigned old=app.refresh_mode;
    CHECK(reader_more_press(490,250) && app.refresh_mode==1);
    CHECK(reader_more_press(600,250) && app.refresh_mode==2);
    CHECK(reader_more_press(350,250) && app.refresh_mode==0);
    CHECK(reader_more_press(580,500) && app.tilt_enabled);
    CHECK(reader_more_press(500,500) && !app.tilt_enabled);
    app.confirm_action=1;CHECK(!reader_more_press(580,500));app.confirm_action=0;
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
    CHECK(reading_menu_press(100,1160) && app.page==THINGSOS_UI_FONTS && app.font_from_reader);
    draw();
    for(int y=1100;y<1216;++y)for(int x=0;x<684;++x)CHECK(app.canvas.pixels[(size_t)y*684+x]==255);
    canvas_snapshot("reader-font-parent");
    for(unsigned tab=0;tab<4;++tab){tap((int)tab*171+85,1150);CHECK(app.page==THINGSOS_UI_FONTS);}
    CHECK(reader_font_press(60,150) && app.page==THINGSOS_UI_READING && app.menu && !app.jump);
    CHECK(app.start==position && app.chapter_index==chapter && app.font.face==active_font);
    CHECK(reading_menu_press(100,1160) && app.font_from_reader);
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
    CHECK(fwrite(app.canvas.pixels, 1, 684u * 1216u, file) == 684u * 1216u);
    fclose(file);
}
static void snapshot(const char* name) {
    app.message[0] = 0;
    draw();
    canvas_snapshot(name);
}
static unsigned region_ink(int top,int bottom) {
    unsigned count=0;
    for(int y=top;y<bottom;++y) for(int x=36;x<648;++x)
        count+=app.canvas.pixels[(size_t)y*684+x]<128;
    return count;
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
static void usb_controls_test(void) {
    navigate(THINGSOS_UI_SETTINGS);tap(100,815);
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
    tap(100,915);CHECK(app.page==THINGSOS_UI_UPGRADE);
    navigate(THINGSOS_UI_SETTINGS);tap(100,1015);CHECK(app.page==THINGSOS_UI_RECOVERY);
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
static void online_transfer_test(void) {
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
        for(unsigned y=746;y<1076;++y)for(unsigned x=28;x<656;++x) {
            unsigned char pixel=app.canvas.pixels[(size_t)y*684+x];
            CHECK(pixel==0 || pixel==255);
        }
        input_key_t keys[40];size_t count=input_keyboard_keys(keys);CHECK(count<=40);
        for(size_t i=0;i<count;++i) {
            CHECK(wifi_key_hit(keys[i].x+keys[i].w/2,keys[i].y+34)==keys[i].code);
            CHECK(keys[i].x>=28 && keys[i].x+keys[i].w<=656 && keys[i].y+68<=1076);
            if(keys[i].code>=100)printable[keys[i].code-100]=true;
            for(size_t j=0;j<i;++j)CHECK(keys[i].x>=keys[j].x+keys[j].w || keys[j].x>=keys[i].x+keys[i].w || keys[i].y>=keys[j].y+68 || keys[j].y>=keys[i].y+68);
        }
    }
    for(unsigned c=32;c<=126;++c)CHECK(printable[c]); /* WiFi passwords retain all printable ASCII. */
    CHECK(wifi_key_hit(130,1030)==-1 && wifi_key_hit(94,780)==-1);
    CHECK(wifi_key_hit(60,1085)==41 && wifi_key_hit(60,1090)==-1);
    CHECK(wifi_key_hit(480,1085)==49);
    app.input.mode=THINGSOS_KEYS_LOWER;app.wifi_symbols_more=false;
    CHECK(wifi_key_hit(93,780)==100+'q' && wifi_key_hit(94,780)==100+'w');
    CHECK(wifi_key_hit(44,895)==100+'a' && wifi_key_hit(35,895)==-1);
    CHECK(wifi_key_hit(100,940)==40); /* Shift keeps its own target. */
    tap(100,650);CHECK(!app.input.chinese); /* Former language control is inert. */
    CHECK(keyboard_mode_press(480,1030) && app.input.chinese && app.keyboard_refresh);
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
    host_wd_readtime=(thingsos_rt_device_status_t){.today_ready=true,.today_online=169,.today_native=71};
    app.wd_status.diagnostic=(thingsos_wd_diagnostic_t){.stage="POST",.error=WD_NETWORK,
        .action=WD_DEVICE_UPLOAD,.phase=5,.http_status=429};
    char sync_text[128];
    CHECK(wd_sync_diagnostic_text(sync_text,sizeof(sync_text)) && strstr(sync_text,"/POST P5") &&
          strstr(sync_text,"H429"));
    CHECK(strstr(wd_sync_failure_hint(&app.wd_status.diagnostic),"频繁"));
    wd_rt_uploaded_text(sync_text,sizeof(sync_text),&host_wd_readtime);
    CHECK(strstr(sync_text,"04 分 00 秒"));
    app.wd_sync_panel=true;app.page=THINGSOS_UI_WEIDU;snapshot("weread-sync-panel");
    tap(300,845);CHECK(app.wd_consent);
    tap(500,875);CHECK(app.wd_time_on&&app.wd_rt_on&&app.wd_time_confirmed);
    tap(300,845);CHECK(!app.wd_time_on&&!app.wd_rt_on);
    tap(300,845);CHECK(app.wd_time_on&&app.wd_rt_on);
    memset(&host_wd_readtime,0,sizeof(host_wd_readtime));
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
    snapshot("weread-highlight-thoughts-large");CHECK(app.font.size==24);tap(500,1020);CHECK(app.wd_thought_row==2);
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
    app.dirty=false;app.wd_enter_pending=true;app.wd_init_pending=false;app.wd_pull_pending=false;
    app.wd_was_online=true; /* This case exercises an established connection. */
    app.wd_mapped_chapter=app.chapter_index;host_wd_reading.serial=app.wd_serial;
    app.reader_ready_at=now();app.activity_at=now();unsigned quiet_requests=host_weidu_calls;
    wd_poll();CHECK(app.wd_enter_pending && host_weidu_calls==quiet_requests);
    host_clock_us+=2100000;
    wd_poll();CHECK(host_weidu_action==WD_DEVICE_ENTER && !host_wd_upload.report && !host_wd_upload.seconds);
    host_weidu.busy=false;
    app.wd_status.session_ready=true;strcpy(app.wd_status.book,app.wd_id);
    app.wd_accounted=app.runtime.reading_ms;app.runtime.reading_ms+=31000;app.wd_report_after=0;app.dirty=false;
    wd_poll();CHECK(host_weidu_action==WD_DEVICE_REPORT && host_wd_upload.report && host_wd_upload.seconds==31);
    CHECK(app.wd_accounted==app.runtime.reading_ms);host_weidu.busy=false;app.runtime.reading_ms+=17000;
    wd_finish();CHECK(host_weidu_action==WD_DEVICE_UPLOAD&&host_wd_upload.seconds==17&&host_wd_upload.report);
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
    for(unsigned mono=0;mono<2;++mono) {
        memset(canvas.pixels,255,684u*1216u);font.monochrome=mono!=0;
        thingsos_font_size(&font,24);thingsos_font_weight(&font,0);
        thingsos_label(&font,&canvas,mono?"Native mono hinting":"Gray glyph + threshold 128",28,48,628);
        for(unsigned size=0;size<3;++size)for(unsigned weight=0;weight<3;++weight) {
            int y=110+(int)(size*3+weight)*120;char info[64];
            snprintf(info,sizeof(info),"%u px / weight %u",24+size*4,weight);
            thingsos_font_size(&font,18);thingsos_font_weight(&font,0);thingsos_label(&font,&canvas,info,28,y-26,628);
            thingsos_font_size(&font,24+size*4);thingsos_font_weight(&font,weight);
            thingsos_label(&font,&canvas,"人入八水永然，细笔画、斜线与标点。",28,y,628);
            thingsos_label(&font,&canvas,"ABC abc 123 / \\ () ,.;: 混排边缘",28,y+38,628);
        }
        CHECK(!font.errors);
        if(mono)for(size_t i=0;i<684u*1216u;++i)CHECK(canvas.pixels[i]==0 || canvas.pixels[i]==255);
        else for(size_t i=0;i<684u*1216u;++i)canvas.pixels[i]=canvas.pixels[i]<128?0:255;
        app.canvas=canvas;canvas_snapshot(mono?"fast-font-native-mono":"fast-font-threshold");app.canvas=original;
    }
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
    snapshot("reader-refresh-fast");CHECK(app.font.monochrome);
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
    navigate(THINGSOS_UI_DISPLAY);tap(438,610);snapshot("display-fast-native");
    for(unsigned y=322;y<356;++y)for(unsigned x=48;x<180;++x) {
        unsigned char p=app.canvas.pixels[(size_t)y*684+x];CHECK(p==0 || p==255);
    }
    navigate(THINGSOS_UI_DISPLAY);tap(298,610);
    CHECK(!app.ui_refresh_mode && app.refresh_mode==1);snapshot("display-independent-refresh");
}
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

#include "txt_source_ui_test.inc"
static void migration_render_and_pin_test(void) {
    const unsigned char gif[]={
        'G','I','F','8','9','a',1,0,1,0,0x80,0,0,0,0,0,255,255,255,
        0x2c,0,0,0,0,1,0,1,0,0,2,2,0x44,0x01,0,0x3b
    };
    unsigned char* pixels=NULL;unsigned width=0,height=0;
    CHECK(thingsos_decode_reader_image(gif,sizeof(gif),684,1216,&pixels,&width,&height));
    CHECK(pixels && width==1 && height==1 && pixels[0]==0);
    free(pixels);
    thingsos_chapter_t text={.text="ABCD",.bytes=4};
    memset(&app.text_hits,0,sizeof(app.text_hits));
    thingsos_font_size(&app.font,30);
    CHECK(thingsos_text_page_layout_flow(&app.font,NULL,&text,0,36,75,540,540,175,0,
        false,NULL,NULL,&app.text_hits,true,0)==4);
    CHECK(app.text_hits.glyph_count==4 && app.text_hits.glyphs[1].y>app.text_hits.glyphs[0].y);

    host_nvs_available=host_nvs_read=true;
    app.page=THINGSOS_UI_POWER;
    pin_boot();CHECK(!app.pin.enabled);
    snapshot("power-pin-switch");
    CHECK(!power_pin_press(20,600) && app.page==THINGSOS_UI_POWER);
    CHECK(power_pin_press(585,600) && app.pin.purpose==PIN_SETUP);
    strlcpy(app.pin.entry,"123456",sizeof(app.pin.entry));app.pin.length=6;pin_submit();
    CHECK(app.pin.stage==PIN_CONFIRM);
    strlcpy(app.pin.entry,"123456",sizeof(app.pin.entry));app.pin.length=6;pin_submit();
    CHECK(app.pin.enabled && host_pin_record_bytes==sizeof(lock_pin_record_t));
    pin_boot();CHECK(app.pin.required && app.page==THINGSOS_UI_LOCK_PIN);
    strlcpy(app.pin.entry,"123457",sizeof(app.pin.entry));app.pin.length=6;pin_submit();
    CHECK(app.pin.required && !app.pin.length);
    strlcpy(app.pin.entry,"123456",sizeof(app.pin.entry));app.pin.length=6;pin_submit();
    CHECK(!app.pin.required && app.page==THINGSOS_UI_LIBRARY);
}
static void weidu_deferral_test(void) {
    thingsos_weidu_status_t saved_wd=host_weidu;
    thingsos_weidu_action_t saved_action=app.weidu_action;
    bool saved_busy=host_weidu.busy;
    bool saved_online=online.running;
    bool saved_deferred=app.weidu_deferred;
    navigate(THINGSOS_UI_WEIDU);
    app.dirty=false;app.weidu_detail=false;app.mp_mode=false;app.weidu_count=1;
    host_weidu.busy=false;host_weidu.serial=app.weidu.serial;
    host_clock_us+=1000000;app.activity_at=0;
    unsigned before=host_weidu_cover_calls;
    app.weidu_covers_pending=true;online.running=true;
    CHECK(weidu_foreground_active());
    weidu_poll();CHECK(host_weidu_cover_calls==before && app.weidu_covers_pending);
    online.running=false;app.weidu_deferred=true;
    CHECK(weidu_foreground_active());
    weidu_poll();CHECK(host_weidu_cover_calls==before && app.weidu_covers_pending);
    app.weidu_deferred=false;host_weidu.busy=true;app.weidu_action=WD_DEVICE_DOWNLOAD;
    CHECK(weidu_foreground_active());
    weidu_poll();CHECK(host_weidu_cover_calls==before && app.weidu_covers_pending);
    host_weidu.busy=false;app.weidu_action=WD_DEVICE_SYNC;
    app.weidu.serial=host_weidu.serial;
    CHECK(!weidu_foreground_active());
    weidu_poll();CHECK(host_weidu_cover_calls==before+1 && !app.weidu_covers_pending);
    app.opened=true;app.page=THINGSOS_UI_READING;app.controls_open=false;app.dirty=false;
    online.running=true;CHECK(weidu_foreground_active());online.running=false;
    host_weidu.busy=true;app.weidu_action=WD_DEVICE_CHAPTER;CHECK(weidu_foreground_active());
    host_weidu.busy=false;app.weidu_action=WD_DEVICE_RESTORE;CHECK(!weidu_foreground_active());
    app.opened=false;host_weidu=saved_wd;host_weidu.busy=saved_busy;app.weidu=host_weidu;
    app.weidu_action=saved_action;online.running=saved_online;app.weidu_deferred=saved_deferred;
    app.weidu_covers_pending=false;navigate(THINGSOS_UI_LIBRARY);
}
int main(int argc, char** argv) {
    CHECK(argc == 2 || argc == 3);
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
    if(argc==3) {
        host_sd=true;app.book_count=1;app.books[0].id=778899;app.books[0].epub=true;
        strlcpy(app.books[0].path,argv[2],sizeof(app.books[0].path));
        CHECK(open_book(0));
        printf("Actual controller opened EPUB: chapters=%zu chapter=%u bytes=%zu images=%zu\n",
            app.epub.spine_count,app.chapter_index,app.chapter.bytes,app.chapter.image_count);
        release_book();thingsos_font_close(&app.font);free(app.canvas.pixels);free(state);return 0;
    }
    font_recovery_test();
    reader_annotation_test();
    reader_background_test();
    shared_qwerty_test();
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
    weidu_scroll(-120);CHECK(app.weidu_start==9 && app.weidu_count==3);snapshot("weidu-cards-last");
    unsigned cleanups=host_cleanup_requests;
    app.force_refresh=app.transition_refresh=false;
    navigate(THINGSOS_UI_SETTINGS);
    CHECK(host_cleanup_requests==cleanups+1 && !app.force_refresh && !app.transition_refresh);
    CHECK(host_cleanup_y==90 && host_cleanup_h==1008 && host_cleanup_x==0 && host_cleanup_w==684);
    navigate(THINGSOS_UI_TRANSFER);CHECK(host_cleanup_requests==cleanups+2);
    navigate(THINGSOS_UI_WEIDU);
    weidu_scroll(-120);CHECK(app.weidu_start==9);
    tap(610,260);CHECK(!app.weidu_cards && app.weidu_start==0 && app.weidu_count==4);
    /* Search covers the full shelf, including books beyond the visible page. */
    tap(545,260);CHECK(app.page==THINGSOS_UI_PASSWORD && app.editing==5);
    snapshot("weidu-search-keyboard");
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
    navigate(THINGSOS_UI_TRANSFER);CHECK(!app.transition_refresh && !app.force_refresh);
    app.transition_refresh=false;navigate(THINGSOS_UI_SETTINGS);CHECK(!app.transition_refresh && !app.force_refresh);
    const thingsos_ui_page_t home_pages[]={THINGSOS_UI_LIBRARY,THINGSOS_UI_WEIDU,THINGSOS_UI_APPS,THINGSOS_UI_SETTINGS};
    for(unsigned from=0;from<4;++from)for(unsigned to=0;to<4;++to) {
        app.page=home_pages[from];app.transition_refresh=false;navigate(home_pages[to]);CHECK(!app.transition_refresh);
    }
    const thingsos_ui_page_t subpages[]={THINGSOS_UI_DISPLAY,THINGSOS_UI_TIME,THINGSOS_UI_POWER,
        THINGSOS_UI_WIFI,THINGSOS_UI_PASSWORD,THINGSOS_UI_UPGRADE,THINGSOS_UI_RECOVERY,
        THINGSOS_UI_USB,THINGSOS_UI_FILES,THINGSOS_UI_FONTS,THINGSOS_UI_WALLPAPER,
        THINGSOS_UI_TRANSFER,THINGSOS_UI_SYSTEM,THINGSOS_UI_LOGS};
    for(unsigned root=2;root<4;++root)for(unsigned sub=0;sub<sizeof(subpages)/sizeof(subpages[0]);++sub) {
        app.page=home_pages[root];app.transition_refresh=false;
        unsigned before=host_cleanup_requests;
        navigate(subpages[sub]);CHECK(!app.transition_refresh && host_cleanup_requests==before+1);
        CHECK(host_cleanup_y==74 && host_cleanup_h==1024);
        navigate(home_pages[root]);CHECK(!app.transition_refresh && host_cleanup_requests==before+2);
    }
    tap(420,1150);CHECK(app.page==THINGSOS_UI_APPS && !host_http_requested);
    snapshot("plus-app-launcher");
    tap(235,210);CHECK(app.page==THINGSOS_UI_APPS); /* Gap between icons. */
    tap(125,210);CHECK(app.page==THINGSOS_UI_TRANSFER && host_http_requested && app.hotspot);
    tap(60,145);CHECK(app.page==THINGSOS_UI_APPS && !host_http_requested && !app.hotspot);
    tap(135,405);CHECK(app.page==THINGSOS_UI_SYSTEM);
    tap(150,255);CHECK(!pico_system_details);snapshot("plus-system-overview");
    tap(510,255);CHECK(pico_system_details);snapshot("plus-system-details");
    tap(500,970);CHECK(app.page==THINGSOS_UI_LOGS);
    tap(340,620);CHECK(app.page==THINGSOS_UI_TRANSFER && host_http_requested);
    tap(60,145);CHECK(app.page==THINGSOS_UI_LOGS && !host_http_requested);
    tap(60,145);CHECK(app.page==THINGSOS_UI_SYSTEM);
    tap(60,145);CHECK(app.page==THINGSOS_UI_APPS);
    tap(335,215);CHECK(app.page==THINGSOS_UI_FILES);
    tap(60,145);CHECK(app.page==THINGSOS_UI_APPS);
    tap(335,405);CHECK(app.page==THINGSOS_UI_WALLPAPER);
    tap(60,145);CHECK(app.page==THINGSOS_UI_APPS);
    tap(540,405);CHECK(app.page==THINGSOS_UI_WIFI && !host_http_requested);
    tap(60,145);CHECK(app.page==THINGSOS_UI_APPS);
    CHECK(!pico_controls_swipe(90,0,120) && !pico_controls_swipe(20,130,100));
    CHECK(pico_controls_swipe(20,0,120) && app.controls_open && app.page==THINGSOS_UI_APPS);
    snapshot("plus-control-center");
    tap(304,244);CHECK(app.page==THINGSOS_UI_WIFI && !app.controls_open);
    tap(60,145);CHECK(app.controls_open && app.page==THINGSOS_UI_APPS);
    tap(618,244);CHECK(app.page==THINGSOS_UI_BLE && !app.controls_open);
    capacitive_key(THINGSOS_KEY_HOME);
    CHECK(app.controls_open && app.page==THINGSOS_UI_APPS);
    CHECK(pico_controls_swipe(CONTROL_BOTTOM-35,0,-95) && !app.controls_open && app.page==THINGSOS_UI_APPS);
    navigate(THINGSOS_UI_SETTINGS);
    tap(540,120);CHECK(!app.controls_open && app.page==THINGSOS_UI_SETTINGS);
    CHECK(pico_controls_swipe(20,0,120) && app.controls_open && app.page==THINGSOS_UI_SETTINGS);
    CHECK(pico_controls_swipe(CONTROL_BOTTOM-35,0,-95) && !app.controls_open);
    navigate(THINGSOS_UI_RECOVERY);tap(340,917);CHECK(app.page==THINGSOS_UI_LOGS);
    tap(60,145);CHECK(app.page==THINGSOS_UI_RECOVERY);
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
    host_network_busy=true;auto_sleep_poll(310002);CHECK(!app.asleep);
    host_network_busy=false;
    unsigned auto_suspends=host_suspend;
    auto_sleep_poll(310002);CHECK(app.asleep && host_suspend==auto_suspends+1 && !app.hotspot);
    auto_sleep_poll(310003);CHECK(host_suspend==auto_suspends+1);
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
    power_action(1);
    CHECK(app.asleep && app.page==THINGSOS_UI_LOCK && host_sleep_body_ink>500 && !region_ink(0,74) && !region_ink(1098,1216));
    canvas_snapshot("sleep-from-lock");
    unsigned unlock_requests=host_unlock_refresh;
    power_action(1);
    CHECK(!app.asleep && app.page==THINGSOS_UI_LIBRARY && host_unlock_refresh==unlock_requests+1);
    snapshot("lock-after-wake");
    uint64_t stale_time = now();
    CHECK(app.page == THINGSOS_UI_LIBRARY && app.dirty && app.force_refresh);
    CHECK(!lock_due(stale_time));
    snapshot("unlock-library");
    app.lock_return = THINGSOS_UI_LOCK;
    navigate(THINGSOS_UI_LOCK);
    unlock_requests=host_unlock_refresh;
    capacitive_key(THINGSOS_KEY_HOME);
    CHECK(app.page == THINGSOS_UI_LIBRARY && app.force_refresh && host_unlock_refresh==unlock_requests+1);
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
    tap(140, 320);
    CHECK(app.page == THINGSOS_UI_DISPLAY);
    snapshot("display");
    unsigned reader_px=app.px,reader_weight=app.weight_choice;
    bool reader_ripple=app.ripple_enabled;
    tap(550,735);CHECK(app.ui_weight==2 && app.weight_choice==reader_weight);
    snapshot("display-global-weight");
    CHECK(!app.font.ui_size_percent && !app.font.ui_weight_override);
    for (int i = 0; i < 20; ++i) tap(550, 340);
    CHECK(app.ui_px==50 && app.px==reader_px);
    for(int i=0;i<20;++i)tap(270,340);
    CHECK(app.ui_px==26 && app.px==reader_px);
    tap(550,340);tap(550,340);CHECK(app.ui_px==30);
    tap(438,480);CHECK(app.ui_refresh_pages==15 && app.refresh_pages==15);
    for(unsigned i=0;i<4;++i) {
        tap(240+(int)i*105,480);CHECK(app.ui_refresh_pages==5+i*5);
        tap(240+(int)i*105,870);CHECK(app.refresh_pages==15);
    }
    tap(438,870);CHECK(app.refresh_pages==15);
    tap(438,610);CHECK(app.ui_refresh_mode==1 && app.refresh_mode==0);
    tap(560,610);CHECK(app.ui_refresh_mode==2 && app.refresh_mode==0);
    tap(298,610);CHECK(app.ui_refresh_mode==0);
    app.force_refresh=false;tap(300,980);CHECK(app.force_refresh);
    tap(250,735);CHECK(app.ui_weight==0 && app.ripple_enabled==reader_ripple);
    CHECK(!strcmp(font_display_name("FZSongYJW.TTF"),"方正宋圆"));
    CHECK(!strcmp(font_display_name("fzs3jw.ttf"),"方正宋三"));
    CHECK(!strcmp(font_display_name("FZPingXYSJW.TTF"),"方正屏显雅宋"));
    CHECK(!strcmp(font_display_name("xiawuxinzhisong.ttf"),"霞鹜新致雅宋"));
    CHECK(!strcmp(font_display_name("落霞文楷.ttf"),"落霞文楷.ttf"));
    CHECK(!strcmp(font_display_name("Custom.ttf"),"Custom.ttf"));
    tap(100,1060);CHECK(app.page==THINGSOS_UI_DISPLAY);
    capacitive_key(THINGSOS_KEY_PREVIOUS);CHECK(app.page==THINGSOS_UI_SETTINGS);
    tap(100, 620);
    CHECK(app.page == THINGSOS_UI_POWER);
    CHECK(app.lock_wallpaper==0);tap(420,1150);tap(335,405);CHECK(app.page==THINGSOS_UI_WALLPAPER);
    snapshot("wallpaper-sources");
    tap(260,500);CHECK(app.wallpaper_step==3 && wallpaper_view_only);
    tap(340,1110);CHECK(app.wallpaper_step==0 && !wallpaper_view_only);
    tap(120,700);CHECK(app.wallpaper_step==3 && app.lock_wallpaper==0);
    snapshot("wallpaper-stats-preview");host_nvs_available=true;
    tap(500,1110);CHECK(app.page==THINGSOS_UI_APPS && app.lock_wallpaper==0);host_nvs_available=false;
    navigate(THINGSOS_UI_SETTINGS);tap(100,620);
    snapshot("power");
    tap(120, 720);
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
    release_book();
    host_sd=true;
    char reading_cover[700];
    for(unsigned variant=0;variant<2;++variant) {
        snprintf(reading_cover,sizeof(reading_cover),"%s/pack-limit/微读-%u.epub",fixture_root,variant?56:55);
        CHECK(thingsos_epub_open(&app.epub,reading_cover));thingsos_epub_navigation(&app.epub);app.epub_open=true;
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
    CHECK(load_chapter(1) && !app.chapter.cover_image);app.start=0;draw();
    CHECK(app.end==app.chapter.images[0].offset); /* Whole figure starts on the following page. */
    app.start=app.end;snapshot("reading-cover-ordinary-figure");CHECK(app.image_hit_count==1 && app.image_hits[0].w==reader_body_width() && app.image_hits[0].h>294 && app.image_hits[0].h<=reader_body_height());
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
    charging_status_test();
    unsigned battery_saved=app.battery;
    unsigned ui_px_saved=app.ui_px;
    thingsos_ui_page_t status_page_saved=app.page;
    const unsigned pcts[]={1,9,50,99,100};
    for(unsigned scale_idx=0;scale_idx<2;++scale_idx) {
        if(scale_idx){navigate(THINGSOS_UI_LIBRARY);app.ui_px=38;}
        for(unsigned idx=0;idx<sizeof(pcts)/sizeof(pcts[0]);++idx) {
            app.battery=pcts[idx];
            if(scale_idx)draw();else draw_status();
            int bat_y0=74,bat_y1=-1;
            for(int y=10;y<64;++y)for(int x=612;x<666;++x) {
                if(app.canvas.pixels[(size_t)y*684+x]<224) {
                    if(y<bat_y0)bat_y0=y;
                    if(y>bat_y1)bat_y1=y;
                }
            }
            CHECK(bat_y0==26 && bat_y1==48);
            bool col_ink[684]={0};
            for(int x=456;x<612;++x)
                for(int y=10;y<64;++y)
                    if(app.canvas.pixels[(size_t)y*684+x]<224){col_ink[x]=true;break;}
            int num_x1=611;while(num_x1>=456 && !col_ink[num_x1])--num_x1;
            CHECK(num_x1>456);
            int num_x0=num_x1, gap=0;
            for(int x=num_x1-1;x>=456;--x) {
                if(col_ink[x]){num_x0=x;gap=0;}
                else if(++gap>7)break;
            }
            int num_y0=74,num_y1=-1;
            for(int y=10;y<64;++y)for(int x=num_x0;x<=num_x1;++x) {
                if(app.canvas.pixels[(size_t)y*684+x]<224) {
                    if(y<num_y0)num_y0=y;
                    if(y>num_y1)num_y1=y;
                }
            }
            int wifi_x1=num_x0-1;while(wifi_x1>=456 && !col_ink[wifi_x1])--wifi_x1;
            CHECK(wifi_x1>456);
            int wifi_x0=wifi_x1;gap=0;
            for(int x=wifi_x1-1;x>=456;--x) {
                if(col_ink[x]){wifi_x0=x;gap=0;}
                else if(++gap>7)break;
            }
            int wifi_y0=74,wifi_y1=-1;
            for(int y=10;y<64;++y)for(int x=wifi_x0;x<=wifi_x1;++x) {
                if(app.canvas.pixels[(size_t)y*684+x]<224) {
                    if(y<wifi_y0)wifi_y0=y;
                    if(y>wifi_y1)wifi_y1=y;
                }
            }
            int num_bat_gap=612-1-num_x1;
            int wifi_num_gap=num_x0-1-wifi_x1;
            int v_diff=(num_y0+num_y1)-(bat_y0+bat_y1);
            if(v_diff<0)v_diff=-v_diff;
            int w_diff=(wifi_y0+wifi_y1)-(bat_y0+bat_y1);
            if(w_diff<0)w_diff=-w_diff;
            CHECK(num_bat_gap==8 && wifi_num_gap==12);
            CHECK(v_diff<=(scale_idx?2:1) && w_diff<=2);
        }
    }
    app.ui_px=ui_px_saved;app.page=status_page_saved;app.font.ui_size_percent=0;
    app.battery=100;
    bool wifi_saved=host_network_connected,hotspot_saved=app.hotspot;
    host_network_connected=true;snapshot("status-wifi-connected");
    unsigned char* online_status=malloc(684u*74u);CHECK(online_status);
    memcpy(online_status,app.canvas.pixels,684u*74u);
    host_network_connected=false;snapshot("status-wifi-disconnected");
    unsigned changed=0;
    for(unsigned y=0;y<74;++y)for(unsigned x=0;x<684;++x) {
        bool different=online_status[y*684+x]!=app.canvas.pixels[y*684+x];
        if(different) {CHECK(x>=450 && x<560 && y>=26 && y<50);++changed;}
    }
    CHECK(changed>40);free(online_status);
    app.hotspot=true;draw_status();
    CHECK(!status_wifi_ink(5.5f,3.5f,true) && status_wifi_ink(5.5f,3.5f,false));
    host_network_connected=wifi_saved;app.hotspot=hotspot_saved;
    app.battery=battery_saved;draw();
    CHECK(app.canvas.pixels[26u*684+612]==0 && app.canvas.pixels[32u*684+662]==0);
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
    snapshot("reader-after-wake");
    host_suspend_failure=1;
    power_action(1);
    CHECK(!app.asleep && app.dirty && app.page==THINGSOS_UI_READING && app.start==saved_start);
    host_suspend_failure=0;
    snapshot("reader-after-sleep-failure");
    tap(30, 35);
    CHECK(app.page == THINGSOS_UI_READING && !app.menu);
    tap(342, 500);
    CHECK(!app.image_view.open && !app.menu);
    CHECK(reader_hold_delay(342,500)==1000);
    CHECK(!reader_content_hold(342,500,999) && !app.image_view.open);
    CHECK(reader_content_hold(342,500,1000) && app.image_view.open);
    CHECK(!reader_swipe_page(342,500,0,-95)); /* The image overlay owns the gesture. */
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
    tap(100, 790); CHECK(app.page == THINGSOS_UI_WIFI);
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
    host_network_busy=true;tap(180,380);CHECK(host_network_connected && host_wifi_disconnects==disconnects);
    host_network_busy=false;host_wifi_disconnect_error=ESP_ERR_INVALID_STATE;tap(180,380);
    CHECK(host_network_connected && app.wifi_requested);host_wifi_disconnect_error=0;
    tap(180,380);CHECK(!host_network_connected && !app.wifi_requested && host_wifi_disconnects==disconnects+2);
    CHECK(thingsos_network_saved_name()[0]);snapshot("wifi-disconnected-saved");
    tap(180,380);CHECK(host_wifi_connects==reconnects+1 && app.wifi_requested);
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
    tap(650, 775); CHECK(app.list_start == 4);
    tap(100, 700); CHECK(app.wifi_requested && host_wifi_connects == 1 && !app.hotspot);
    app.list_start = 0; app.wifi_requested = 0;
    tap(100, 350); CHECK(app.page == THINGSOS_UI_PASSWORD && app.editing == 1 && app.wifi_join_after);
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
    tap(100,390);CHECK(app.page==THINGSOS_UI_WIFI && host_wifi_connects==connects_before+1);
    tap(100,530);CHECK(app.page==THINGSOS_UI_WIFI && host_wifi_connects==connects_before+2);
    host_wifi_connects=connects_before;host_network_connected=true;
    snapshot("wifi-connected");
    tap(650, 831); CHECK(app.list_start == 5);
    tap(500, 380); CHECK(app.confirm_action == 7);
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
    /* Wait for a completed real row, independent of Windows timer granularity. */
    CHECK(WaitForSingleObject(reader_cache_test_progressed,10000)==WAIT_OBJECT_0);
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
    CHECK(WaitForSingleObject(reader_cache_test_progressed,10000)==WAIT_OBJECT_0);
    host_scan_barrier();reader_cache_output_end();reader_cache_test_slow=false;
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
    CHECK(thingsos_reader_menu_hit(104,1205,NULL)==THINGSOS_MENU_FONTS);
    CHECK(thingsos_reader_menu_hit_weidu(500,1215,NULL)==THINGSOS_MENU_MORE);
    CHECK(thingsos_reader_menu_hit_weidu(235,1215,NULL)==THINGSOS_MENU_NONE);
    CHECK(thingsos_reader_menu_hit(104,1216,NULL)==THINGSOS_MENU_NONE);
    CHECK(thingsos_reader_menu_hit_weidu(300,1150,NULL)==THINGSOS_MENU_NOTES);
    CHECK(thingsos_reader_menu_hit(342,1150,NULL)==THINGSOS_MENU_NONE);
    CHECK(thingsos_reader_menu_hit_weidu(235,1150,NULL)==THINGSOS_MENU_NONE);
    CHECK(!app.tilt_enabled);
    tap(550,1150);CHECK(app.page==THINGSOS_UI_READER_MORE && !app.menu);
    snapshot("reader-more-off");
    CHECK(reader_more_toggle_hit(580,500)==0 && reader_more_toggle_hit(580,740)==1);
    CHECK(reader_more_toggle_hit(300,500)==-1);
    host_nvs_available=true;tap(580,500);CHECK(app.tilt_enabled && host_saved_tilt==1);
    snapshot("reader-more-on");
    CHECK(app.tilt_sensitivity==1 && reader_more_toggle_hit(550,620)==2);
    tap(490,620);CHECK(app.tilt_sensitivity==0 && host_saved_tilt_sensitivity==0);
    snapshot("reader-more-low");
    tap(610,620);CHECK(app.tilt_sensitivity==2 && host_saved_tilt_sensitivity==2);
    host_nvs_commit_fail=true;tap(550,620);CHECK(app.tilt_sensitivity==2);host_nvs_commit_fail=false;
    tap(550,620);CHECK(app.tilt_sensitivity==1 && host_saved_tilt_sensitivity==1);
    host_nvs_read=true;host_saved_tilt_sensitivity=2;preferences_load();CHECK(app.tilt_sensitivity==2);
    host_saved_tilt_sensitivity=99;preferences_load();CHECK(app.tilt_sensitivity==1);
    host_tilt_sensitivity_present=false;preferences_load();CHECK(app.tilt_sensitivity==1);
    host_tilt_sensitivity_present=true;host_saved_tilt_sensitivity=1;host_nvs_read=false;

    tap(580,500);CHECK(app.tilt_enabled && host_saved_tilt==1); /* Right On remains On. */
    tap(30,120);CHECK(app.page==THINGSOS_UI_READING && app.menu);
    app.tilt_enabled=false;host_nvs_read=true;preferences_load();CHECK(app.tilt_enabled);
    host_saved_tilt=99;preferences_load();CHECK(!app.tilt_enabled);
    tap(550,1150);CHECK(app.page==THINGSOS_UI_READER_MORE);
    host_nvs_commit_fail=true;tap(580,500);CHECK(!app.tilt_enabled);host_nvs_commit_fail=false;
    tap(580,500);CHECK(app.tilt_enabled);
    tap(500,500);CHECK(!app.tilt_enabled && !host_saved_tilt);
    capacitive_key(THINGSOS_KEY_MENU);CHECK(app.page==THINGSOS_UI_READING && app.menu);
    reader_tilt_poll(false);CHECK(!host_tilt_active);
    host_nvs_read=false;
    /* Landscape persistence, source position, repagination, and real input mapping. */
    CHECK(!app.landscape_enabled);CHECK(pagination_prepare());while(!app.pagination_valid)CHECK(pagination_step());
    CHECK(go_page(1));draw();size_t landscape_start=app.start;uint64_t portrait_key=app.page_cache_key;
    app.menu=true;tap(550,1150);CHECK(app.page==THINGSOS_UI_READER_MORE);
    host_nvs_commit_fail=true;tap(580,740);CHECK(!app.landscape_enabled);host_nvs_commit_fail=false;
    tap(580,740);CHECK(app.landscape_enabled && host_saved_landscape && app.canvas.width==1216);
    CHECK(app.start==landscape_start && !app.pagination_started);snapshot("landscape-more");
    CHECK(reader_more_toggle_hit(1150,330)==2);
    tap(1150,330);CHECK(app.tilt_sensitivity==2);
    tap(1100,330);CHECK(app.tilt_sensitivity==1);

    tap(800,640);CHECK(app.page==THINGSOS_UI_READING && app.menu);snapshot("landscape-menu");
    CHECK(thingsos_reader_menu_hit_landscape(1000,620,NULL,false)==THINGSOS_MENU_MORE);
    CHECK(thingsos_reader_menu_hit_landscape(1000,683,NULL,false)==THINGSOS_MENU_MORE);
    CHECK(thingsos_reader_menu_hit_landscape(1000,684,NULL,false)==THINGSOS_MENU_NONE);
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
    app.ui_px=34;app.ui_weight=1;app.ui_refresh_pages=20;app.ui_refresh_mode=2;app.lock_wallpaper=1;
    wallpaper_book_set(&app.wallpaper_book,"/sdcard/books/pinned.epub","固定的封面");
    host_nvs_available=true;preferences_save();
    app.weight_choice=0;app.ripple_enabled=false;app.indent_chars=0;host_nvs_read=true;
    app.ui_px=30;app.ui_weight=app.ui_refresh_mode=0;app.ui_refresh_pages=15;app.lock_wallpaper=0;
    memset(&app.wallpaper_book,0,sizeof(app.wallpaper_book));
    preferences_load();CHECK(app.weight_choice==2 && app.ripple_enabled);
    CHECK(app.indent_chars==THINGSOS_READER_INDENT_CHARS);
    CHECK(app.ui_px==34 && app.ui_weight==1 && app.ui_refresh_pages==20 && app.ui_refresh_mode==2 && app.lock_wallpaper==1);
    CHECK(!strcmp(app.wallpaper_book.path,"/sdcard/books/pinned.epub") && !strcmp(app.wallpaper_book.name,"固定的封面"));
    navigate(THINGSOS_UI_LIBRARY);draw();
    CHECK(app.font.weight==1 && !app.font.ui_size_percent && !app.font.ui_weight_override);
    navigate(THINGSOS_UI_READING);draw();CHECK(app.font.weight==0);
    host_saved_weight=99;host_saved_ripple=99;app.weight_choice=1;app.ripple_enabled=false;
    preferences_load();CHECK(app.weight_choice==1 && !app.ripple_enabled);
    host_nvs_available=host_nvs_read=false;
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
    CHECK(reader_lines[1]==145 && reader_margins[1]==24 && reader_lines[0]<145 && reader_margins[0]<24);
    release_book();
    /* Explicit cover selection: cancel and commit failure preserve the old
       source; reading another book cannot silently replace the chosen cover. */
    host_sd=host_nvs_available=true;app.book_count=1;app.lock_wallpaper=0;
    snprintf(app.books[0].path,sizeof(app.books[0].path),"%s/lock-cover.epub",fixture_root);
    strcpy(app.books[0].name,"固定封面");app.books[0].epub=true;app.books[0].id=881;
    app.library_valid=true;app.library_card=app.records.card_id;
    strlcpy(app.library_path,app.records.library_path,sizeof(app.library_path));
    navigate(THINGSOS_UI_APPS);tap(335,405);tap(300,820);
    CHECK(app.wallpaper_step==1);snapshot("wallpaper-book-list");
    tap(130,400);CHECK(app.wallpaper_step==3 && app.lock_wallpaper==0);
    snapshot("wallpaper-book-preview");CHECK(app.wallpaper_preview_ok);
    tap(120,1110);CHECK(app.wallpaper_step==1 && app.lock_wallpaper==0);
    tap(130,400);draw();host_nvs_commit_fail=true;tap(500,1110);
    CHECK(app.page==THINGSOS_UI_WALLPAPER && app.lock_wallpaper==0 && strstr(app.message,"保存失败"));
    host_nvs_commit_fail=false;tap(500,1110);
    CHECK(app.page==THINGSOS_UI_APPS && app.lock_wallpaper==1 && app.wallpaper_book.id==881);
    app.recent_count=1;app.recent_ids[0]=999;strcpy(app.books[0].path,"missing-other-book.epub");
    lock_draw(0);
    CHECK(app.wallpaper_book.id==881 && app.canvas.pixels[300u*684]==0 && app.canvas.pixels[300u*684+1]==255);
    CHECK(wallpaper_image_path("/sdcard/wallpapers/a.JPG"));
    CHECK(wallpaper_image_path("/sdcard/wallpapers/a.webp"));
    CHECK(!wallpaper_image_path("/sdcard/wallpapers-else/a.png"));
    CHECK(!wallpaper_image_path("/sdcard/wallpapers/../books/a.png"));
    CHECK(!wallpaper_image_path("/sdcard/wallpapers/a.txt"));
    navigate(THINGSOS_UI_WALLPAPER);app.wallpaper_step=2;app.wallpaper_start=0;
    strlcpy(app.browser.path,"/sdcard/wallpapers",sizeof(app.browser.path));
    app.browser.count=2;
    strlcpy(app.browser.entries[0].name,"a.webp",sizeof(app.browser.entries[0].name));
    strlcpy(app.browser.entries[1].name,"folder",sizeof(app.browser.entries[1].name));
    app.browser.entries[0].directory=false;app.browser.entries[1].directory=true;
    CHECK(!wallpaper_hold(110,400,649) && !wallpaper_hold(236,400,650) &&
          !wallpaper_hold(340,400,650) && !wallpaper_hold(550,400,650));
    CHECK(wallpaper_hold(110,400,650) && strstr(wallpaper_delete_path,"a.webp"));
    tap(340,700);CHECK(!wallpaper_delete_path[0] && app.browser.count==2);
    app.browser.count=0;
    navigate(THINGSOS_UI_WALLPAPER);app.wallpaper_step=3;app.wallpaper_kind=2;
    strcpy(app.wallpaper_candidate_image,"/sdcard/wallpapers/missing.jpg");
    snapshot("wallpaper-preview-unavailable");CHECK(!app.wallpaper_preview_ok);
    tap(500,1110);CHECK(app.lock_wallpaper==1 && app.page==THINGSOS_UI_WALLPAPER);
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
    navigate(THINGSOS_UI_TRANSFER);navigate(THINGSOS_UI_WIFI);app.input_return=THINGSOS_UI_WIFI;
    navigate(THINGSOS_UI_PASSWORD);
    strcpy(app.wifi_password,"not-retained");capacitive_key(THINGSOS_KEY_HOME);
    CHECK(app.page==THINGSOS_UI_TRANSFER && app.service_on && !app.wifi_password[0]);
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
    tap(500,860);CHECK(app.annotation_manual && host_note_manual==1);snapshot("reader-note-sync-manual");
    tap(590,860);CHECK(!app.annotation_manual && host_note_manual==0);
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
    app.landscape_enabled=false;reader_canvas_geometry();tap(300,980);
    CHECK(app.page==THINGSOS_UI_READING && app.annotation_list);
    reader_refresh_config_test();physical_back_test();fast_font_comparison();reader_immersive_test();reader_contact_test();
    online_reader_test();
    weidu_deferral_test();
    txt_source_controller_test();
    migration_render_and_pin_test();
    reader_annotations_close();app.opened=false;
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
