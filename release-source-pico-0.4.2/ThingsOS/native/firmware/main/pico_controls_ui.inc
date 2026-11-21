/* SPDX-License-Identifier: Apache-2.0 */
/* Geometry and antialiased controls from the E-Ink4 Plus control center. */
#include "control_icons.inc"
static bool control_inside(int x,int y,int w,int h,int r){
    if(x<0||y<0||x>=w*4||y>=h*4)return false;
    int cx=x<r*4?r*4:x>(w-r)*4?(w-r)*4:x;
    int cy=y<r*4?r*4:y>(h-r)*4?(h-r)*4:y;
    return (x-cx)*(x-cx)+(y-cy)*(y-cy)<=r*r*16;
}
static void control_round(int left,int top,int w,int h,int radius,int stroke,unsigned edge,unsigned fill){
    if(w<=0||h<=0)return;
    if(radius>w/2)radius=w/2;
    if(radius>h/2)radius=h/2;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){int dx=left+x,dy=top+y;
        if(dx<0||dy<0||dx>=app.canvas.width||dy>=app.canvas.height)continue;
        size_t at=(size_t)dy*app.canvas.width+dx;unsigned sum=0;
        for(int sy=1;sy<=3;sy+=2)for(int sx=1;sx<=3;sx+=2){
            int px=x*4+sx,py=y*4+sy;
            sum+=!control_inside(px,py,w,h,radius)?app.canvas.pixels[at]:
                stroke&&!control_inside(px-stroke*4,py-stroke*4,w-stroke*2,h-stroke*2,radius>stroke?radius-stroke:0)?edge:fill;
        }
        app.canvas.pixels[at]=(uint8_t)((sum+2)/4);
    }
}
static void control_glyph(const uint8_t* data,unsigned size,int x,int y,unsigned ink){
    for(unsigned yy=0;yy<size;++yy)for(unsigned xx=0;xx<size;++xx){size_t at=(size_t)yy*size+xx;
        unsigned white=((data[at/2]>>((at&1)?0:4))&15)*17;
        int dx=x+(int)xx,dy=y+(int)yy;if(white==255||dx<0||dy<0||dx>=app.canvas.width||dy>=app.canvas.height)continue;
        size_t pixel=(size_t)dy*app.canvas.width+dx;
        app.canvas.pixels[pixel]=(uint8_t)((ink*(255-white)+app.canvas.pixels[pixel]*white+127)/255);
    }
}

enum { CONTROL_TILE_TOP=183,CONTROL_TILE_HEIGHT=137,CONTROL_BOTTOM=397 };

static int control_left(void) { return (app.canvas.width-684)/2+36; }
static bool pico_controls_allowed(void) {
    return !app.asleep && !app.pin.required && app.page!=THINGSOS_UI_LOCK &&
        app.page!=THINGSOS_UI_LOCK_PIN && app.page!=THINGSOS_UI_USB &&
        app.page!=THINGSOS_UI_PASSWORD && !app.confirm_action && !app.time_edit &&
        !app.wd_consent && !app.wd_power_action && !app.jump_pending && !online_modal_visible();
}

static bool pico_control_bt_connected(void) {
#ifndef THINGSOS_HOST_UI
    return thingsos_ble_connected();
#else
    return false;
#endif
}

static void pico_controls_set(bool open) {
    if(app.controls_open==open || (open && !pico_controls_allowed()))return;
    app.controls_open=open;
    app.controls_refresh=app.dirty=true;
    app.ripple_pending=false;
    app.reader_background_refresh=false;
    app.message[0]=0;
    thingsos_tilt_reset(&app.tilt);
    thingsos_board_tilt_active(false);
    thingsos_runtime_overlay(&app.runtime,open || app.menu || app.image_view.open || app.footnote_open ||
        app.annotation_open || app.annotation_selecting || app.wd_notes_open || app.wd_thought_open ||
        app.wd_cloud_prompt || app.page!=THINGSOS_UI_READING,now());
}

static bool pico_controls_swipe(int y,int dx,int dy) {
    if(!app.controls_open && y>=0 && y<74 && dy>72 && dy>abs(dx) && pico_controls_allowed()) {
        pico_controls_set(true);
        return true;
    }
    if(app.controls_open && (y<164 || y>=CONTROL_BOTTOM-68) && dy< -72 && -dy>abs(dx)) {
        pico_controls_set(false);
        return true;
    }
    return false;
}

static void pico_controls_detail(thingsos_ui_page_t page) {
    thingsos_ui_page_t from=app.page;
    if(page==THINGSOS_UI_WIFI && thingsos_network_start(false)!=ESP_OK) {
        say("联网启动失败");
        return;
    }
    if(page==from) {
        pico_controls_set(false);
        return;
    }
#ifndef THINGSOS_HOST_UI
    if(page==THINGSOS_UI_BLE)pico_ble_open();
    else
#endif
        navigate(page);
    if(app.page==page) {
        app.controls_origin=from;
        app.controls_detail=page;
    }
}

static bool pico_controls_return(thingsos_ui_page_t page) {
    if(app.controls_detail!=page || (page!=THINGSOS_UI_WIFI && page!=THINGSOS_UI_BLE))return false;
    thingsos_ui_page_t origin=app.controls_origin;
    app.controls_detail=THINGSOS_UI_COUNT;
    navigate(origin);
    pico_controls_set(true);
    return true;
}

static void pico_controls_change_wifi(void) {
    if(thingsos_network_busy()) {
        say("网络任务进行中，请稍后重试");
        return;
    }
    if(thingsos_network_connected()) {
        if(thingsos_network_disconnect()!=ESP_OK)say("Wi-Fi 断开失败");
    } else {
        const char* saved=thingsos_network_saved_name();
        if(!saved[0]) {pico_controls_detail(THINGSOS_UI_WIFI);return;}
        if(thingsos_network_start(false)!=ESP_OK || thingsos_network_reconnect(saved)!=ESP_OK)
            say("连接失败，请重新选择网络");
        else app.wifi_requested=1;
    }
    app.dirty=true;
}

static void pico_controls_change_ble(void) {
    app.ble_enabled=!app.ble_enabled;
    if(!preferences_save()) {
        app.ble_enabled=!app.ble_enabled;
        say("蓝牙设置保存失败");
        return;
    }
#ifndef THINGSOS_HOST_UI
    if(app.ble_enabled)ble_resume_later();
    else thingsos_ble_stop();
#endif
    app.dirty=true;
}

static bool pico_controls_hold(int x,int y,uint64_t elapsed) {
    if(!app.controls_open || elapsed<580 || y<CONTROL_TILE_TOP ||
       y>=CONTROL_TILE_TOP+CONTROL_TILE_HEIGHT)return false;
    int tile=x-control_left();
    if(tile>=0 && tile<297)pico_controls_detail(THINGSOS_UI_WIFI);
    else if(tile>=315 && tile<612)pico_controls_detail(THINGSOS_UI_BLE);
    else return false;
    return true;
}

static bool pico_controls_tap(int x,int y) {
    if(!app.controls_open)return false;
    int width=app.canvas.width,left=control_left();
    if((x>=width-102 && x<width-36 && y>=90 && y<156) || y>=CONTROL_BOTTOM-68) {
        pico_controls_set(false);
        return true;
    }
    if(y<CONTROL_TILE_TOP || y>=CONTROL_TILE_TOP+CONTROL_TILE_HEIGHT)return true;
    int tile=x-left;
    if(tile>=0 && tile<297) {
        if(tile>=249)pico_controls_detail(THINGSOS_UI_WIFI);
        else pico_controls_change_wifi();
    } else if(tile>=315 && tile<612) {
        if(tile>=564)pico_controls_detail(THINGSOS_UI_BLE);
        else pico_controls_change_ble();
    }
    return true;
}

static void pico_controls_draw(void) {
    int width=app.canvas.width,left=control_left();
    thingsos_rect(&app.canvas,0,0,width,CONTROL_BOTTOM-22,255);
    control_round(0,CONTROL_BOTTOM-44,width,44,22,0,0,255);
    app.font.ui_size_percent=100;app.font.ui_weight_override=app.ui_weight+1;
    char text[48];time_t clock=wall_time();struct tm date;
    if(localtime_r(&clock,&date))strftime(text,sizeof(text),"%H:%M",&date);
    else strlcpy(text,"--:--",sizeof(text));
    label(text,36,50,23,160);
    bool wifi=thingsos_network_connected(),ble=pico_control_bt_connected();
    centered_label(wifi?"Wi-Fi 已连接":app.wifi_requested?"Wi-Fi 连接中":"Wi-Fi 未连接",
                   width/2-125,50,21,250);
    snprintf(text,sizeof(text),"%u%%",app.battery);
    thingsos_font_size(ui_font(),23);
    thingsos_label_right(ui_font(),&app.canvas,text,width-210,50,116);
    control_round(width-76,32,39,20,0,2,34,255);
    thingsos_rect(&app.canvas,width-72,36,(int)(31*app.battery/100),12,34);
    thingsos_rect(&app.canvas,width-36,38,3,8,34);
    label("控制中心",36,139,37,width-150);
    control_round(width-102,90,66,66,33,2,187,255);
    control_glyph(control_up,29,width-83,109,34);
    for(unsigned i=0;i<2;++i) {
        int x=left+(int)i*315,top=CONTROL_TILE_TOP;
        bool on=i?app.ble_enabled:wifi;
        control_round(x,top,297,CONTROL_TILE_HEIGHT,17,2,170,255);
        control_round(x+20,top+37,62,62,31,2,on?34:119,on?34:255);
        control_glyph(i?control_bluetooth:control_wifi,33,x+34,top+51,on?255:34);
        label(i?"蓝牙":"Wi-Fi",x+97,top+59,28,140);
        label(i?(ble?"已连接":on?"已开启":"已关闭"):wifi?"已连接":app.wifi_requested?"连接中":"未连接",
              x+97,top+94,21,140);
        control_glyph(control_chevron,24,x+261,top+56,85);
    }
    if(app.message[0])centered_label(app.message,left,348,21,612);
    control_round(width/2-42,CONTROL_BOTTOM-36,84,6,3,0,0,136);
    thingsos_rect(&app.canvas,22,CONTROL_BOTTOM-2,width-44,2,34);
}

#ifndef THINGSOS_HOST_UI
static void pico_controls_poll(void) {
    static bool was_open, wifi, ble;
    if(!app.controls_open){was_open=false;return;}
    bool link=thingsos_network_connected(), input=thingsos_ble_connected();
    if(was_open && (wifi!=link || ble!=input))app.dirty=true;
    was_open=true;wifi=link;ble=input;
}
#endif
