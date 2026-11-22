/* SPDX-License-Identifier: Apache-2.0 */
static void txt_source_controller_test(void) {
    release_book();host_sd=true;host_weidu.busy=app.weidu.busy=false;
    book_t saved=app.books[0];size_t saved_count=app.book_count;
    app.book_count=1;memset(&app.books[0],0,sizeof(app.books[0]));
    app.books[0].id=0x74787431;strcpy(app.books[0].name,"GBK TXT test");
    snprintf(app.books[0].path,sizeof(app.books[0].path),"%s/reader-gbk-large.txt",fixture_root);
    FILE* f=fopen(app.books[0].path,"wb");CHECK(f);
    for(unsigned i=0;i<100000;++i)CHECK(fwrite("\xd0\xde\xd5\xe6\r\n",1,6,f)==6);
    CHECK(!fclose(f));CHECK(open_book(0));
    CHECK(app.epub_open&&app.epub.plain_text&&app.epub.text_encoding==2);
    CHECK(!reader_epub_paragraphs()&&app.epub.spine_count>1);
    CHECK(!memcmp(app.chapter.text,"\xe4\xbf\xae\xe7\x9c\x9f",6));
    CHECK(reader_restore_source(3,120)&&app.chapter_index==3&&app.start==120);
    CHECK(book_progress()>50&&book_progress()<100);
    snapshot("txt-gbk-large-middle");release_book();
    f=fopen(app.books[0].path,"wb");CHECK(f);
    for(unsigned i=0;i<50000;++i)CHECK(fputs("    example line\r\n",f)>=0);
    CHECK(!fclose(f));CHECK(open_book(0));
    CHECK(app.epub_open&&app.epub.plain_text&&app.epub.text_encoding==1);
    size_t offset=app.epub.spine[3]+5;
    CHECK(reader_restore_source(0,offset)&&app.chapter_index==3&&app.start==5);
    CHECK(app.chapter.source_start==app.epub.spine[3]);
    /* Backward across a chapter boundary before the page index is complete:
       first turn lands on the previous chapter's last page, the second on its
       second-to-last page (not on the chapter before). */
    {
        CHECK(load_chapter(3)&&app.start==0);app.pagination_valid=false;
        next_page(false);
        size_t last=app.start;
        CHECK(app.chapter_index==2&&last>0&&app.history_count>0);
        next_page(false);
        CHECK(app.chapter_index==2&&app.start<last);
        app.end=page_end(&app.chapter,app.start); /* set by draw() on device */
        next_page(true);
        CHECK(app.chapter_index==2&&app.start==last);
    }
    release_book();
    const char* real=getenv("THINGSOS_TXT_ACCEPTANCE");
    if(real&&*real)for(unsigned i=0;i<2;++i) {
        const char* name=i?"xiuzhen":"xianni";
        snprintf(app.books[0].path,sizeof(app.books[0].path),"%s/%s.txt",real,name);
        app.books[0].id=0x74787440+i;strlcpy(app.books[0].name,name,sizeof(app.books[0].name));
        CHECK(open_book(0)&&app.epub.plain_text&&app.epub.text_encoding==2);
        CHECK(load_chapter((unsigned)(app.epub.spine_count/2)));
        CHECK(book_progress()>45&&book_progress()<55);
        app.start=0;snapshot(i?"txt-xiuzhen-middle":"txt-xianni-middle");
        CHECK(load_chapter((unsigned)app.epub.spine_count-1));
        CHECK(app.chapter.bytes&&app.chapter.bytes<THINGSOS_XML_BYTES);
        release_book();
    }
    app.books[0]=saved;app.book_count=saved_count;
    puts("PASS: actual TXT open controller, GBK segmented reading, UTF-8 absolute restore and bounded middle/end loading");
}
