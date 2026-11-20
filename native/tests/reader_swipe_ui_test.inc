// SPDX-License-Identifier: Apache-2.0
/* The production release handler is shared by local and WeRead reading pages. */
static void reader_swipe_test(void) {
    release_book();host_sd=true;app.book_count=1;memset(&app.books[0],0,sizeof(app.books[0]));
    app.books[0].id=0x1aa1cc77;strcpy(app.books[0].name,"上下滑动翻页测试");
    snprintf(app.books[0].path,sizeof(app.books[0].path),"%s/reader-swipe.txt",fixture_root);
    FILE* file=fopen(app.books[0].path,"wb");CHECK(file);
    for(unsigned i=0;i<900;++i)fputs("山川与日月，文字与时光。手指上下滑动，一次只翻一页。\n",file);
    CHECK(!fclose(file));
    app.pin.required=app.asleep=app.controls_open=false;app.wd_power_action=0;
    app.wd_notes_open=app.wd_thought_open=app.wd_sync_panel=app.wd_consent=app.wd_cloud_prompt=false;
    app.confirm_action=0;memset(&online,0,sizeof(online));
    for(unsigned vertical=0;vertical<2;++vertical)for(unsigned layout=0;layout<3;++layout) {
        release_book();app.vertical_enabled=vertical!=0;app.landscape_enabled=layout==2;app.immersive_enabled=layout==1;
        CHECK(open_book(0));reader_annotations_dismiss();app.menu=app.jump=false;
        app.start=0;app.runtime.location.byte_offset=0;
        CHECK(paginate()&&app.page_count>3);draw();CHECK(current_page()==0);
        int x=reader_width()/2,y=reader_height()/2;
        CHECK(!reader_swipe_page(x,y,0,-40)&&current_page()==0);
        CHECK(!reader_swipe_page(x,y,100,-90)&&current_page()==0);
        CHECK(!reader_swipe_page(x,0,0,-120)&&current_page()==0);
        CHECK(!reader_swipe_page(x,reader_height()-1,0,120)&&current_page()==0);
        CHECK(reader_swipe_page(x,y,12,-140)&&current_page()==1);draw();
        CHECK(!app.menu&&app.page==THINGSOS_UI_READING);
        CHECK(reader_swipe_page(x,y,-12,140)&&current_page()==0);draw();
        CHECK(reader_swipe_page(x,y,0,100)&&current_page()==0&&strstr(app.message,"第一页"));
        /* A long swipe remains a single next-page command. */
        CHECK(reader_swipe_page(x,reader_body_top()+reader_body_height()-30,0,-400)&&current_page()==1);draw();
        bool* owners[]={&app.menu,&app.jump,&app.jump_pending,&app.image_view.open,&app.footnote_open,
            &app.annotation_open,&app.annotation_selecting,&app.annotation_dragging,
            &app.wd_notes_open,&app.wd_thought_open,&app.wd_consent,&app.wd_cloud_prompt,&app.wd_sync_panel,
            &app.controls_open,&app.asleep,&app.pin.required};
        for(unsigned i=0;i<sizeof(owners)/sizeof(owners[0]);++i){
            *owners[i]=true;CHECK(!reader_swipe_page(x,y,0,-120)&&current_page()==1);*owners[i]=false;
        }
        app.confirm_action=1;CHECK(!reader_swipe_page(x,y,0,-120));app.confirm_action=0;
        app.wd_power_action=1;CHECK(!reader_swipe_page(x,y,0,-120));app.wd_power_action=0;
        app.page=THINGSOS_UI_CHAPTERS;CHECK(!reader_swipe_page(x,y,0,-120));app.page=THINGSOS_UI_READING;
        if(layout==2){
            /* Raw landscape touch samples rotate into the user's visible up. */
            int sx=683-y,sy=x,ex=683-(y-100),ey=x;
            CHECK(reader_touch_coordinates(&sx,&sy)&&reader_touch_coordinates(&ex,&ey));
            CHECK(sx==x&&sy==y&&ey-sy==-100);
            CHECK(reader_swipe_page(sx,sy,ex-sx,ey-sy)&&current_page()==2);draw();
        }
    }
    release_book();app.vertical_enabled=app.landscape_enabled=app.immersive_enabled=false;
    reader_canvas_geometry();navigate(THINGSOS_UI_LIBRARY);
}
