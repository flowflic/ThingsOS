// SPDX-License-Identifier: Apache-2.0
// Render the actual firmware UI with illustrative data, without device access.
#define THINGSOS_HOST_UI
#include "../firmware/main/main.c"
static const char* output;
static void capture(const char* name) {
    char file[700];snprintf(file,sizeof(file),"%s/%s.pgm",output,name);
    app.message[0]=0;draw();FILE* f=fopen(file,"wb");if(!f)exit(2);
    fprintf(f,"P5\n684 1216\n255\n");fwrite(app.canvas.pixels,1,684u*1216u,f);fclose(f);
}
/* Illustrative content, rendered by the production UI without cloud access. */
static void capture_reading_features(void) {
    const char* quote="读书使我们在别人的思想中，看见自己的生活。";
    const char* text="清晨，山谷里升起一层薄薄的雾。推开窗，远处的树木还安静地站在风中。\n\n读书使我们在别人的思想中，看见自己的生活。\n\n一本书的意义，往往在重读时慢慢显现。相同的文字，经过不同的生活，会唤起不同的感受。\n\n走到湖边，我认出一排落叶松[1]。阳光穿过枝叶，水面映出细碎的光。\n\n我们停下来，记住某个片段，又继续读下去。故事里的每个人，都在寻找属于自己的答案。\n\n阅读不急于给出结论。有些问题值得带着走，让它们在日常里慢慢获得新的含义。\n\n每一次重新打开书，都是一次新的相遇。我们在不同的人生阶段回到这里，也会遇见不同的自己。\n\n翻过这一页，窗外的光线渐渐变了。未曾留意的细节，也开始变得清晰。";
    app.px=36;app.ripple_enabled=false;app.opened=true;app.page=THINGSOS_UI_READING;
    app.reader_from_weidu=true;strlcpy(app.read_title,"山间来信",sizeof(app.read_title));
    app.chapter.text=_strdup(text);app.chapter.bytes=strlen(text);app.runtime.location.book_id=5001;
    size_t begin=(size_t)(strstr(text,quote)-text);
    app.wd_lines[0]=(thingsos_text_underline_t){begin,begin+strlen(quote),false};
    app.chapter.underlines=app.wd_lines;app.chapter.underline_count=app.wd_line_count=1;
    capture("weread-highlights");
    host_thought_cache=true;app.wd_thought_open=true;app.wd_thought_cached=true;app.wd_thought_info.count=4;
    host_thought_authors[0]="山外";host_thought_authors[1]="一页又一页";
    host_thought_texts[0]="有些经历不会真正过去。再次读到这里，才明白记忆并不只是回头看，也会影响我们如何走向下一段生活。";
    host_thought_texts[1]="同样的文字，在不同的时候读，总会看到一点新的东西。这就是我喜欢重读的原因。";
    strlcpy(app.wd_thought_quote,quote,sizeof(app.wd_thought_quote));app.wd_thought_heat=128;
    capture("weread-thoughts");app.wd_thought_open=false;
    const char* marker=strstr(text,"[1]");const char* note="落叶松：松科落叶乔木。它的针叶在秋季变为金黄色，随后脱落，春天再长出新叶。";
    app.chapter.footnotes=calloc(1,sizeof(thingsos_footnote_t));app.chapter.footnote_count=1;
    app.chapter.footnotes[0]=(thingsos_footnote_t){(size_t)(marker-text),(size_t)(marker-text)+3,0,strlen(note)};
    app.chapter.notes=_strdup(note);app.chapter.notes_bytes=strlen(note);app.footnote_open=true;
    capture("footnote-card");app.footnote_open=false;
    app.chapter.underlines=NULL;app.chapter.underline_count=app.wd_line_count=0;
    app.page=THINGSOS_UI_WEIDU;app.wd_sync_panel=true;app.weidu.authenticated=true;
    app.wd_pull_on=app.wd_push_on=true;app.wd_time_on=app.wd_time_confirmed=true;
    app.wd_status.pulled_at=host_wall_time-300;app.wd_status.uploaded_at=host_wall_time-60;
    app.wd_status.pull=app.wd_status.progress=app.wd_status.duration=WD_OK;
    app.wd_status.duration_at=host_wall_time-60;app.wd_status.duration_seconds=1860;
    capture("weread-reading-time");
}
int main(int argc,char** argv) {
    if(argc!=3 && argc!=4)return 2;output=argv[2];
    state=calloc(1,sizeof(*state));if(!state)return 2;
    snprintf(host_font,sizeof(host_font),"%s/default.ttf",argv[1]);
    app.canvas=(thingsos_canvas_t){malloc(684u*1216u),684,1216};
    if(!app.canvas.pixels || !thingsos_board_font(&app.font))return 2;
    app.px=46;app.ui_px=30;app.line_choice=app.margin_choice=1;
    app.refresh_pages=app.ui_refresh_pages=15;app.ripple_enabled=true;
    app.battery=86;app.auto_lock_minutes=5;app.auto_time=true;host_sd=true;
    struct tm date={0};date.tm_year=126;date.tm_mon=9;date.tm_mday=17;date.tm_hour=9;date.tm_min=41;host_wall_time=mktime(&date);
    thingsos_runtime_init(&app.runtime);thingsos_records_init(&app.records);
    host_network_connected=true;
    if(argc==4 && !strcmp(argv[3],"--features-only")){capture_reading_features();return 0;}
    static const char* titles[]={"山间来信","瓦尔登湖","飞鸟集","朝花夕拾","小王子","山川与日月"};
    app.book_count=6;
    for(unsigned i=0;i<6;++i){book_t* b=&app.books[i];b->id=100+i;
        strlcpy(b->name,titles[i],sizeof(b->name));snprintf(b->path,sizeof(b->path),"%s/showcase-%u.txt",output,i);
        FILE* f=fopen(b->path,"wb");if(!f)return 2;
        for(unsigned j=0;j<8;++j)fputs("清晨，山谷里升起一层薄薄的雾。推开窗，远处的树木还安静地站在风中。\n\n沿着小路往前走，鞋底踏过昨夜的落叶，发出轻轻的声响。溪水从石头之间穿过，阳光落下来，一切都显得很慢。\n\n在这样的早晨，读几页书，也像是向远处走了一小段路。不必急着抵达哪里，只要让文字留在心里。\n\n",f);
        fclose(f);char recorded_path[128];snprintf(recorded_path,sizeof(recorded_path),"/sdcard/books/showcase-%u.txt",i);
        thingsos_book_record_t* record=thingsos_records_book(&app.records,b->id,recorded_path,b->name);
        if(!record)return 4;record->order=6-i;
    }
    app.library_valid=true;app.library_card=app.records.card_id;strlcpy(app.library_path,app.records.library_path,sizeof(app.library_path));
    navigate(THINGSOS_UI_LIBRARY);capture("library");
    if(!open_book(0))return 3;app.menu=false;capture("reading");
    app.menu=true;capture("reading-menu");app.menu=false;release_book();
    for(unsigned i=0;i<76;++i){struct tm day=date;day.tm_mon=7;day.tm_mday=1+(int)i;time_t stamp=mktime(&day);struct tm* d=localtime(&stamp);
        unsigned ymd=(unsigned)(d->tm_year+1900)*10000+(unsigned)(d->tm_mon+1)*100+(unsigned)d->tm_mday;
        if(i%6!=2)thingsos_records_time(&app.records,100,ymd,(uint64_t)(8+(i*7)%61)*60000);
    }
    navigate(THINGSOS_UI_STATS);app.stats_period=1;capture("statistics");
    navigate(THINGSOS_UI_WALLPAPER);capture("wallpapers");
    navigate(THINGSOS_UI_DISPLAY);capture("display");
    char alice[640];snprintf(alice,sizeof(alice),"%s/alice.epub",output);
    FILE* book=fopen(alice,"rb");
    if(book){fclose(book);memset(&app.wallpaper_book,0,sizeof(app.wallpaper_book));
        app.wallpaper_book.id=9001;app.wallpaper_book.epub=true;
        strlcpy(app.wallpaper_book.path,alice,sizeof(app.wallpaper_book.path));
        strlcpy(app.wallpaper_book.name,"爱丽丝漫游奇境记",sizeof(app.wallpaper_book.name));
        app.lock_wallpaper=1;navigate(THINGSOS_UI_LOCK);capture("lock-book-cover");}
    host_network_connected=true;host_weidu.authenticated=true;host_weidu.phase=WD_READY;
    host_weidu_epub[0]=0;weidu_enter();app.weidu_count=app.weidu_total=4;
    static const char* shelf_titles[]={"爱丽丝漫游奇境记","瓦尔登湖","飞鸟集","朝花夕拾"};
    static const char* shelf_authors[]={"刘易斯·卡罗尔","亨利·戴维·梭罗","泰戈尔","鲁迅"};
    for(unsigned i=0;i<4;++i){strlcpy(app.weidu_books[i].title,shelf_titles[i],sizeof(app.weidu_books[i].title));
        strlcpy(app.weidu_books[i].author,shelf_authors[i],sizeof(app.weidu_books[i].author));}
    capture("weread-shelf");
    strlcpy(host_weidu_epub,alice,sizeof(host_weidu_epub));app.weidu_detail=true;app.weidu_selected=0;
    capture("weread-offline");
    puts("Actual firmware UI rendered with illustrative books and reading statistics.");return 0;
}
