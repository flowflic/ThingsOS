// SPDX-License-Identifier: Apache-2.0
/* Shared geometry keeps navigation controls and their touch targets aligned. */
static bool reader_control_hit(int x,int y,int left,int top,int width) {
    return x>=left && x<left+width && y>=top && y<top+68;
}
static void reader_control(const char* text,int x,int y,int width,bool selected) {
    thingsos_rect(&app.canvas,x,y,width,68,0);
    thingsos_rect(&app.canvas,x+2,y+2,width-4,64,255);
    if(!strcmp(text,"-") || !strcmp(text,"+")) {
        thingsos_rect(&app.canvas,x+width/2-10,y+33,21,3,0);
        if(text[0]=='+')thingsos_rect(&app.canvas,x+width/2-1,y+24,3,21,0);
    } else centered_label(text,x+8,y+44,24,width-16);
    if(selected)for(int yy=y;yy<y+68;++yy)for(int xx=x;xx<x+width;++xx)
        app.canvas.pixels[(size_t)yy*app.canvas.width+xx]=255-app.canvas.pixels[(size_t)yy*app.canvas.width+xx];
}
typedef struct {int top,step,track,preview,input,footer,width;} reader_jump_layout_t;
static reader_jump_layout_t reader_jump_layout(void) {
    return app.landscape_enabled?(reader_jump_layout_t){74,216,320,395,420,570,1144}:
        (reader_jump_layout_t){638,788,900,980,1016,1110,612};
}
static bool reader_jump_track_hit(int x,int y) {
    reader_jump_layout_t g=reader_jump_layout();
    return x>=36 && x<36+g.width && y>=g.track && y<g.track+60;
}
static void reader_jump_track_update(int x) {
    reader_jump_layout_t g=reader_jump_layout();
    app.jump_percent=(unsigned)((x<36?0:x>36+g.width?g.width:x-36)*100/g.width);
}
static void reader_jump_pending_tap(int x,int y) {
    reader_jump_layout_t g=reader_jump_layout();int half=(g.width-28)/2;
    /* Repeated confirmation/drag events stay in this overlay. Only an
       explicit cancel/back dismisses it; they never fall through to text. */
    if(y<g.top || reader_control_hit(x,y,36,g.footer,half))reader_jump_cancel();
}
static void reader_jump_draw(void) {
    reader_jump_layout_t g=reader_jump_layout();char value[96];
    for(int y=74;y<g.top;++y)for(int x=0;x<reader_width();++x) {
        size_t at=(size_t)y*reader_width()+x;
        app.canvas.pixels[at]=(unsigned char)(128+app.canvas.pixels[at]/2);
    }
    thingsos_rect(&app.canvas,0,g.top,reader_width(),reader_height()-g.top,255);
    thingsos_rect(&app.canvas,0,g.top,reader_width(),2,0);
    centered_label("跳转",96,g.top+56,28,reader_width()-192);
    centered_label("阅读进度 · 全书",36,g.top+100,22,g.width);
    reader_control("-",36,g.step,137,false);
    reader_control("+",36+g.width-137,g.step,137,false);
    snprintf(value,sizeof(value),"%u%%",app.jump_percent);
    centered_label(value,185,g.step+45,32,g.width-298);
    thingsos_rect(&app.canvas,36,g.track+25,g.width,6,220);
    thingsos_rect(&app.canvas,36,g.track+25,(int)(g.width*app.jump_percent/100),6,0);
    int knob=36+(g.width-12)*(int)app.jump_percent/100;
    thingsos_rect(&app.canvas,knob,g.track+18,12,20,0);
    if(app.jump_pending)snprintf(value,sizeof(value),"分页 %u/%u 章 · 已排 %u 页",
        app.pagination_chapter,app.epub_open?(unsigned)app.epub.spine_count:1,(unsigned)app.page_count);
    else if(!app.pagination_valid)snprintf(value,sizeof(value),"全书分页尚未完成，确认后准备跳转");
    else snprintf(value,sizeof(value),"预览第 %u / %u 页",
        (unsigned)thingsos_page_percent(app.jump_percent,app.page_count)+1,(unsigned)app.page_count);
    centered_label(value,36,g.preview,24,g.width);
    reader_control("输入页码",36,g.input,g.width,false);
    int half=(g.width-28)/2;
    reader_control("取消",36,g.footer,half,false);
    reader_control(app.jump_pending?"准备中":"确认跳转",36+half+28,g.footer,half,true);
}
