// SPDX-License-Identifier: Apache-2.0
/* Execute against the currently opened EPUB illustration and its actual draw
   bounds, including rotated/vertical layouts, rather than synthetic hit boxes. */
static void reader_image_hold_layout_test(void) {
    size_t position=app.start;
    for(unsigned vertical=0;vertical<2;++vertical)for(unsigned layout=0;layout<3;++layout) {
        app.vertical_enabled=vertical!=0;app.landscape_enabled=layout==2;app.immersive_enabled=layout==1;
        app.start=position;app.runtime.location.byte_offset=(uint32_t)position;
        app.menu=app.jump=false;reader_cache_clear();draw();CHECK(app.image_hit_count);
        size_t index=app.image_hits[0].index;
        int x=app.image_hits[0].x+app.image_hits[0].w/2;
        int y=app.image_hits[0].y+app.image_hits[0].h/2;
        CHECK(reader_image_hit(x,y)==index&&reader_hold_delay(x,y)==1000);
        tap(x,y);CHECK(!app.image_view.open&&!app.menu&&app.start==position);
        CHECK(!reader_content_hold(x,y,0)&&!reader_content_hold(x,y,649)&&!reader_content_hold(x,y,999));
        CHECK(!app.annotation_selecting&&!app.image_view.open);
        /* Cached regions cannot open an image after the displayed page moved. */
        ++app.text_hits_start;CHECK(!reader_image_hold(x,y,1000));--app.text_hits_start;
        bool* owners[]={&app.menu,&app.jump,&app.jump_pending,&app.controls_open,&app.annotation_open,
            &app.annotation_selecting,&app.footnote_open,&app.wd_notes_open,&app.wd_thought_open,
            &app.wd_cloud_prompt,&app.wd_consent,&app.asleep,&app.pin.required};
        for(unsigned i=0;i<sizeof(owners)/sizeof(owners[0]);++i){
            *owners[i]=true;CHECK(!reader_image_hold(x,y,1000)&&!app.image_view.open);*owners[i]=false;
        }
        size_t cover=app.chapter.cover_image;app.chapter.cover_image=index+1;
        CHECK(!reader_image_hold(x,y,1000));app.chapter.cover_image=cover;
        CHECK(reader_content_hold(x,y,1000)&&app.image_view.open&&app.image_index==index);
        CHECK(!reader_image_hold(x,y,2000)); /* A held contact cannot reopen the viewer. */
        tap(60,130);CHECK(!app.image_view.open&&!app.image_pixels&&app.start==position);
    }
    app.vertical_enabled=app.landscape_enabled=app.immersive_enabled=false;
    reader_cache_clear();draw();
}
