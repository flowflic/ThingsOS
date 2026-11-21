/* SPDX-License-Identifier: Apache-2.0
 * Approved 2026-11-04 UE. Canvas, modal input and animation remain main-task
 * owned. Timer frames draw only the 64x66 spinner region, without relayout.
 */
static bool online_segment(float x,float y,float ax,float ay,float bx,float by,float radius) {
    float dx=bx-ax,dy=by-ay,den=dx*dx+dy*dy;
    float t=den?((x-ax)*dx+(y-ay)*dy)/den:0;
    if(t<0)t=0;
    if(t>1)t=1;
    dx=x-(ax+t*dx);dy=y-(ay+t*dy);return dx*dx+dy*dy<=radius*radius;
}
static bool online_round_inside(float x,float y,float w,float h,float r) {
    if(x<0||y<0||x>=w||y>=h)return false;
    float cx=x<r?r:x>w-r?w-r:x,cy=y<r?r:y>h-r?h-r:y;
    float dx=x-cx,dy=y-cy;return dx*dx+dy*dy<=r*r;
}
typedef struct {int x,y,w,h;} online_card_rect_t;
static online_card_rect_t online_card_rect(void) {
    return (online_card_rect_t){(app.canvas.width-532)/2,(app.canvas.height-430)/2,532,430};
}
static online_card_rect_t online_spinner_rect(void) {
    online_card_rect_t c=online_card_rect();int y=c.y+38;
    return (online_card_rect_t){c.x+234,y&~1,64,66};
}
static void online_modal_progress(void) {
    if(!online_pending())return;
    unsigned count=thingsos_weidu_stream_count(ONLINE_ROOT,online.book.id);
    unsigned first=count && online.target>=count?0:online.target;
    unsigned total=online.initial?3:1,ready=0;
    if(count && total>count-first)total=count-first;
    for(unsigned i=0;i<total;++i)
        ready+=thingsos_weidu_stream_ready(ONLINE_ROOT,online.book.id,first+i);
    if(online.total!=total || online.progress!=ready) {
        online.total=total;online.progress=ready;
        if(online_modal_visible())app.dirty=true;
    }
}
static void online_modal_begin(bool initial) {
    online.initial=initial;online.error=online.painted=false;
    online.frame=online.progress=online.total=online.progress_serial=0;online.animate_at=0;
    online.error_text[0]=0;app.message[0]=0;app.message_until=0;
    online_modal_progress();app.dirty=true;
}
static void online_spinner_draw(void) {
    static const float vectors[12][2]={{0,-1},{.5f,-.8660254f},{.8660254f,-.5f},{1,0},
        {.8660254f,.5f},{.5f,.8660254f},{0,1},{-.5f,.8660254f},{-.8660254f,.5f},{-1,0},{-.8660254f,-.5f},{-.5f,-.8660254f}};
    online_card_rect_t r=online_spinner_rect();thingsos_rect(&app.canvas,r.x,r.y,r.w,r.h,255);
    for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
        unsigned shade=0;
        for(unsigned sy=0;sy<2;++sy)for(unsigned sx=0;sx<2;++sx) {
            float xx=x+(sx+.5f)*.5f,yy=y+(sy+.5f)*.5f;unsigned ink=255;
            for(unsigned i=0;i<12;++i) {
                float vx=vectors[i][0],vy=vectors[i][1];
                if(online_segment(xx,yy,32+20*vx,32+20*vy,32+26*vx,32+26*vy,3)) {
                    ink=(i+12-online.frame)%12<4?0:210;break;
                }
            }
            shade+=ink;
        }
        app.canvas.pixels[(size_t)(r.y+y)*app.canvas.width+r.x+x]=(uint8_t)((shade+2)/4);
    }
}
static void online_card_border(online_card_rect_t c) {
    thingsos_rect(&app.canvas,c.x-9,c.y-9,c.w+18,c.h+18,255);
    for(int y=0;y<c.h;++y)for(int x=0;x<c.w;++x) {
        unsigned shade=0;
        for(unsigned sy=0;sy<2;++sy)for(unsigned sx=0;sx<2;++sx) {
            float xx=x+(sx+.5f)*.5f,yy=y+(sy+.5f)*.5f;
            bool outer=online_round_inside(xx,yy,c.w,c.h,10);
            bool inner=online_round_inside(xx-2,yy-2,c.w-4,c.h-4,8);
            shade+=outer&&!inner?0:255;
        }
        app.canvas.pixels[(size_t)(c.y+y)*app.canvas.width+c.x+x]=(uint8_t)((shade+2)/4);
    }
}
static void online_card_button(const char* text,int x,int y,int w) {
    thingsos_rect(&app.canvas,x,y,w,2,0);thingsos_rect(&app.canvas,x,y+59,w,2,0);
    thingsos_rect(&app.canvas,x,y,2,61,0);thingsos_rect(&app.canvas,x+w-2,y,2,61,0);
    centered_label(text,x+8,y+40,25,w-16);
}
static void online_modal_draw(void) {
    if(!online_modal_visible())return;
    /* Freeze the underlying view; only this one-time composition dims it. */
    for(int y=74;y<app.canvas.height;++y)for(int x=0;x<app.canvas.width;++x) {
        size_t at=(size_t)y*app.canvas.width+x;
        app.canvas.pixels[at]=(uint8_t)((app.canvas.pixels[at]*90u+255u*165u+127u)/255u);
    }
    online_card_rect_t c=online_card_rect();online_card_border(c);
    unsigned scale=app.font.ui_size_percent,weight=app.font.ui_weight_override;
    bool mono=app.font.monochrome;
    app.font.ui_size_percent=100;app.font.ui_weight_override=1;app.font.monochrome=false;
    if(online.error) {
        online_card_rect_t r=online_spinner_rect();
        for(int y=4;y<60;++y)for(int x=4;x<60;++x) {
            int dx=x-32,dy=y-32,d=dx*dx+dy*dy;
            if((d>=23*23 && d<=26*26) || (x>=30 && x<=33 && y>=18 && y<37) ||
               (x>=30 && x<=33 && y>=43 && y<47))thingsos_rect(&app.canvas,r.x+x,r.y+y,1,1,0);
        }
    } else online_spinner_draw();
    char text[160];
    if(online.error)strlcpy(text,"章节暂未加载成功",sizeof(text));
    else if(online.initial)strlcpy(text,"正在准备阅读",sizeof(text));
    else snprintf(text,sizeof(text),"正在加载第 %u 章",online.target+1);
    centered_label(text,c.x+24,c.y+160,online.error?29:32,c.w-48);
    if(online.initial)snprintf(text,sizeof(text),"已准备 %u / %u 章",online.progress,online.total);
    else strlcpy(text,"当前位置已保留",sizeof(text));
    centered_label(text,c.x+24,c.y+210,25,c.w-48);
    const char* hint=online.error?online.error_text:online.initial?"当前章及后两章，完成后自动进入":"加载完成后自动继续阅读";
    centered_label(hint,c.x+24,c.y+254,22,c.w-48);
    if(online.initial && !online.error)for(unsigned i=0;i<online.total;++i) {
        int w=130,x=c.x+(c.w-(int)online.total*130-(int)(online.total-1)*12)/2+(int)i*142;
        thingsos_rect(&app.canvas,x,c.y+278,w,8,i<online.progress?0:170);
        if(i>=online.progress)thingsos_rect(&app.canvas,x+1,c.y+279,w-2,6,255);
    }
    if(online.error) {
        online_card_button("重新加载",c.x+38,c.y+320,220);
        online_card_button("取消加载",c.x+274,c.y+320,220);
    } else online_card_button("取消加载",c.x+64,c.y+320,404);
    centered_label(online.initial?"取消后保留已缓冲章节":"取消后停留在当前阅读页",c.x+16,c.y+413,19,c.w-32);
    app.font.ui_size_percent=scale;app.font.ui_weight_override=weight;app.font.monochrome=mono;
}
static bool online_modal_tap(int x,int y) {
    if(!online_modal_visible())return false;
    app.activity_at=now();thingsos_runtime_activity(&app.runtime,app.activity_at);
    online_card_rect_t c=online_card_rect();
    if(y<c.y+320 || y>=c.y+381)return true;
    if(online.error && x>=c.x+38 && x<c.x+258) {
        if(!thingsos_network_connected()) {
            strlcpy(online.error_text,"请先连接 WiFi 后重试",sizeof(online.error_text));app.dirty=true;return true;
        }
        online.error=false;online.opening=online.initial;online.waiting=!online.initial;
        online.retry_at=0;online.progress_serial=0;app.dirty=true;return true;
    }
    int left=online.error?c.x+274:c.x+64,width=online.error?220:404;
    if(x>=left && x<left+width){online_close();app.dirty=true;}
    return true;
}
static bool online_modal_key(thingsos_key_action_t action) {
    if(!online_modal_visible() || action==THINGSOS_KEY_NONE)return false;
    if(action==THINGSOS_KEY_PREVIOUS || action==THINGSOS_KEY_MENU || action==THINGSOS_KEY_HOME) {
        online_close();app.dirty=true;
        return action!=THINGSOS_KEY_HOME;
    }
    return true;
}
static void online_modal_presented(bool success) {
    if(!success)return;
    online.cleanup=false;
    if(success && online_modal_visible()) {
        online.painted=true;
        if(!online.animate_at)online.animate_at=now()+800;
    } else online.painted=false;
}
