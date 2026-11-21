// SPDX-License-Identifier: Apache-2.0
static void toc_current_shade_test(size_t selected_row) {
    draw();
    for(size_t row=0;row<6;++row)
        CHECK(app.canvas.pixels[(350+row*100+10)*684+40]==(row==selected_row?235:255));
}
/* Called with the ten-entry synthetic spine already reserved by the real
   controller test. Navigation/painting must not decode those fake chapters. */
static void toc_current_ui_test(void) {
    unsigned chapter=app.chapter_index,mode=app.refresh_mode,titles_next=app.epub.titles_next;
    size_t offset=app.start,section_count=app.epub.section_toc_count,page_count=app.page_count;
    thingsos_toc_entry_t* original_sections=app.epub.section_toc;
    thingsos_page_position_t* original_pages=app.pages;
    app.refresh_mode=0;
    app.epub.section_toc=NULL;app.epub.section_toc_count=0;
    app.chapter_index=5;app.start=0;navigate(THINGSOS_UI_CHAPTERS);
    CHECK(app.list_start==2&&reader_toc_current()==5&&app.toc_follow_current);
    CHECK(app.epub.titles_next==titles_next); /* Opening remains metadata-only. */
    toc_current_shade_test(3);canvas_snapshot("toc-current-centered");
    app.chapter_index=0;navigate(THINGSOS_UI_CHAPTERS);CHECK(app.list_start==0);toc_current_shade_test(0);
    app.chapter_index=9;navigate(THINGSOS_UI_CHAPTERS);CHECK(app.list_start==4);toc_current_shade_test(5);
    app.chapter_index=5;navigate(THINGSOS_UI_CHAPTERS);
    tap(650,949);CHECK(app.list_start==4&&!app.toc_follow_current);
    navigate(THINGSOS_UI_CHAPTERS);CHECK(app.list_start==2&&app.toc_follow_current);

    thingsos_toc_entry_t* sections=calloc(20,sizeof(*sections));CHECK(sections);
    size_t used=0;
    for(unsigned ch=0;ch<10;++ch) {
        sections[used].chapter=ch;snprintf(sections[used++].title,160,"章节 %u",ch+1);
        if(ch==5)for(unsigned i=0;i<3;++i) {
            sections[used].chapter=5;sections[used].offset=i==2?200:100;
            sections[used].level=i==1?2:1;snprintf(sections[used++].title,160,"小节 %u",i+1);
        }
    }
    app.epub.section_toc=sections;app.epub.section_toc_count=used;
    app.start=99;CHECK(reader_toc_current()==5);
    app.start=100;CHECK(reader_toc_current()==7); /* Deepest identical anchor. */
    app.start=199;CHECK(reader_toc_current()==7);
    app.start=200;CHECK(reader_toc_current()==8);
    app.start=150;navigate(THINGSOS_UI_CHAPTERS);
    CHECK(app.list_start==4);toc_current_shade_test(3);canvas_snapshot("toc-current-section");

    /* Metadata can grow offscreen, but each entry must still center the
       current chapter. While visible, neither metadata nor page indexing runs. */
    app.page=THINGSOS_UI_READING;
    memmove(sections+2,sections+1,(used-1)*sizeof(*sections));++used;
    sections[1]=(thingsos_toc_entry_t){.chapter=0,.offset=20,.level=1};strcpy(sections[1].title,"新增小节");
    app.epub.section_toc_count=used;navigate(THINGSOS_UI_CHAPTERS);
    CHECK(app.list_start==5&&reader_toc_current()==8);toc_current_shade_test(3);
    tap(650,949);CHECK(app.list_start==8&&!app.toc_follow_current);draw();
    unsigned before_titles=app.epub.titles_next;
    app.dirty=false;app.activity_at=0;app.toc_pending=true;
    CHECK(!reader_background_work_allowed());reader_toc_poll();
    CHECK(app.list_start==8&&!app.dirty&&app.epub.titles_next==before_titles);
    app.page=THINGSOS_UI_READING;
    memmove(sections+2,sections+1,(used-1)*sizeof(*sections));++used;
    sections[1]=(thingsos_toc_entry_t){.chapter=0,.offset=10,.level=1};strcpy(sections[1].title,"更早的小节");
    app.epub.section_toc_count=used;
    navigate(THINGSOS_UI_CHAPTERS);CHECK(app.list_start==6&&app.toc_follow_current);
    toc_current_shade_test(3);canvas_snapshot("toc-current-centered-stable");

    app.refresh_mode=1;draw();
    unsigned dots=0;
    for(int y=660;y<680;++y)for(int x=40;x<80;++x) {
        unsigned shade=app.canvas.pixels[(size_t)y*684+x];CHECK(shade==0||shade==255);dots+=shade==0;
    }
    CHECK(dots>0&&dots<200);canvas_snapshot("toc-current-fast-mode");
    app.refresh_mode=0;app.chapter_index=0;app.start=0;
    app.epub.section_toc_count=1;sections[0].chapter=1;
    navigate(THINGSOS_UI_CHAPTERS);CHECK(reader_toc_current()==SIZE_MAX);toc_current_shade_test(SIZE_MAX);
    app.epub.section_toc=original_sections;app.epub.section_toc_count=section_count;free(sections);

    thingsos_page_position_t* txt_pages=calloc(205,sizeof(*txt_pages));CHECK(txt_pages);
    for(unsigned i=0;i<205;++i)txt_pages[i].offset=i*100;
    app.epub_open=false;app.pages=txt_pages;app.page_count=205;app.chapter_index=0;app.start=10400;
    navigate(THINGSOS_UI_CHAPTERS);CHECK(reader_toc_current()==10&&app.list_start==7);toc_current_shade_test(3);
    canvas_snapshot("toc-current-txt");
    app.start=20400;navigate(THINGSOS_UI_CHAPTERS);CHECK(reader_toc_current()==20&&app.list_start==15);toc_current_shade_test(5);
    app.page_count=0;navigate(THINGSOS_UI_CHAPTERS);CHECK(reader_toc_current()==SIZE_MAX&&app.list_start==0);
    app.epub_open=true;app.pages=original_pages;app.page_count=page_count;free(txt_pages);
    app.chapter_index=chapter;app.start=offset;app.refresh_mode=mode;
    navigate(THINGSOS_UI_CHAPTERS);
}
