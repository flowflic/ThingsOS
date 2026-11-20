// SPDX-License-Identifier: Apache-2.0
static void reader_vertical_test(void) {
    release_book();host_sd=true;app.book_count=1;memset(&app.books[0],0,sizeof(app.books[0]));
    app.books[0].id=0x1aa1cc66;strcpy(app.books[0].name,"竖向排版测试");
    snprintf(app.books[0].path,sizeof(app.books[0].path),"%s/vertical.txt",fixture_root);
    FILE* text=fopen(app.books[0].path,"wb");CHECK(text);
    for(unsigned i=0;i<700;++i)fputs("山川与日月，文字与时光。",text);
    CHECK(!fclose(text));CHECK(open_book(0));
    reader_annotations_dismiss();app.menu=app.jump=app.footnote_open=app.image_view.open=false;app.wd_id[0]=0;
    host_nvs_available=true;host_nvs_commit_fail=false;host_nvs_read=true;
    host_vertical_present=false;app.vertical_enabled=true;preferences_load();CHECK(!app.vertical_enabled);
    host_vertical_present=true;host_saved_vertical=99;preferences_load();CHECK(!app.vertical_enabled);
    host_saved_vertical=0;host_nvs_read=false;
    for(unsigned wide=0;wide<2;++wide) {
        app.landscape_enabled=wide!=0;app.immersive_enabled=false;app.vertical_enabled=false;reader_canvas_geometry();
        navigate(THINGSOS_UI_READING);CHECK(pagination_prepare());uint64_t horizontal_key=app.page_cache_key;
        size_t position=app.start;
        navigate(THINGSOS_UI_READER_MORE);
        int x=reader_more_row_left(4)+reader_more_row_width()-40,y=reader_more_row_top(4)+30;
        CHECK(reader_more_toggle_hit(x,y)==4);
        host_nvs_commit_fail=true;tap(x,y);CHECK(!app.vertical_enabled&&!host_saved_vertical);host_nvs_commit_fail=false;
        tap(x,y);CHECK(app.vertical_enabled&&host_saved_vertical==1&&app.start==position&&app.landscape_enabled==(wide!=0));
        CHECK(!app.pagination_started&&!app.page_cache_key&&!app.history_count&&!app.text_hits.glyph_count);
        snapshot(wide?"reader-more-vertical-landscape":"reader-more-vertical");
        CHECK(reader_more_row_top(4)+98<=reader_more_note_top());
        strcpy(app.wd_id,"vertical-cloud-layout");
        snapshot(wide?"reader-more-vertical-cloud-landscape":"reader-more-vertical-cloud");
        if(!wide)CHECK(reader_more_row_top(4)+98<reader_more_sync_top()&&reader_more_sync_top()+98<reader_more_note_top());
        app.wd_id[0]=0;
        host_nvs_read=true;app.vertical_enabled=false;preferences_load();host_nvs_read=false;CHECK(app.vertical_enabled);
        reader_more_back();app.menu=false;draw();
        CHECK(pagination_prepare()&&app.page_cache_key!=horizontal_key&&app.start==position);
        CHECK(app.text_hits.glyph_count>2&&app.text_hits.glyphs[0].x==app.text_hits.glyphs[1].x&&
              app.text_hits.glyphs[0].y<app.text_hits.glyphs[1].y);
        CHECK(page_end(&app.chapter,app.start)==app.end);
        snapshot(wide?"reading-vertical-landscape":"reading-vertical");
        thingsos_glyph_hit_t glyph=app.text_hits.glyphs[0];
        CHECK(reader_annotations_hold(glyph.x+glyph.w/2,glyph.y+glyph.h/2));
        CHECK(app.annotation_begin==glyph.offset);reader_annotations_dismiss();
        reader_cache_invalidate();CHECK(reader_cache_build(0,(thingsos_page_position_t){app.chapter_index,app.start}));
        CHECK(reader_frames[0].valid);
        memset(app.canvas.pixels,37,READER_CACHE_BYTES);CHECK(reader_cache_restore());
        CHECK(app.text_hits.glyph_count==reader_frames[0].hits->glyph_count);
        size_t begin=(size_t)reader_body_top()*reader_width(),bytes=(size_t)reader_body_height()*reader_width();
        CHECK(!memcmp(app.canvas.pixels+begin,reader_frames[0].pixels+begin,bytes));
        CHECK(app.canvas.pixels[begin-1]==37&&app.canvas.pixels[begin+bytes]==37);
        while(!app.pagination_valid)CHECK(pagination_step());CHECK(go_page(1));position=app.start;
        navigate(THINGSOS_UI_READER_MORE);
        x=reader_more_row_left(4)+reader_more_row_width()-140;y=reader_more_row_top(4)+30;
        tap(x,y);CHECK(!app.vertical_enabled&&!host_saved_vertical&&app.start==position);
        CHECK(pagination_prepare()&&app.page_cache_key==horizontal_key);
    }
    app.vertical_enabled=app.landscape_enabled=app.immersive_enabled=false;reader_canvas_geometry();
    CHECK(preferences_save());release_book();
}
