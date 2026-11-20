/* SPDX-License-Identifier: Apache-2.0 */
static const char* const reader_refresh_modes[]={"均衡","快速","清晰"};
static void reader_refresh_segments(const char* const* names,int x,int y,int width,unsigned count,unsigned selected) {
    for(unsigned i=0;i<count;++i) {
        int left=x+(int)i*width/(int)count,right=x+(int)(i+1)*width/(int)count;
        thingsos_rect(&app.canvas,left,y,right-left,64,0);
        thingsos_rect(&app.canvas,left+1,y+1,right-left-2,62,255);
        char interval[24];snprintf(interval,sizeof(interval),"%u次",5+i*5);
        centered_label(names?names[i]:interval,left,y+41,24,right-left);
        if(i==selected)for(int py=y+1;py<y+63;++py)for(int px=left+1;px<right-1;++px)
            app.canvas.pixels[(size_t)py*app.canvas.width+px]=255-app.canvas.pixels[(size_t)py*app.canvas.width+px];
    }
}
static void reader_refresh_update(bool interval,unsigned selected) {
    if(selected>=(interval?4u:3u))return;
    unsigned old_mode=app.refresh_mode,old_every=app.refresh_pages;
    if(interval)app.refresh_pages=5+selected*5;else app.refresh_mode=selected;
    if(app.refresh_mode==old_mode && app.refresh_pages==old_every)return;
    if(!preferences_save()) {
        app.refresh_mode=old_mode;app.refresh_pages=old_every;say("设置保存失败，请重试");return;
    }
    if((old_mode==1)!=(app.refresh_mode==1)) {
        reader_cache_clear();app.pagination_started=app.pagination_valid=app.pagination_failed=false;
        app.history_count=0;app.page_cache_key=0;
    }
    app.dirty=true;
}
