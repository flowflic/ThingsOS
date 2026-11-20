// SPDX-License-Identifier: Apache-2.0
/* Approved control-center V2 geometry. Main-task canvas and input only. */
#include "control_icons.inc"
/* Tiles: 2x2 (Wi-Fi, front light / Bluetooth, vibration), 297x137, 18 px gaps.
   The second row moves the portrait sliders and audio card down by
   CONTROL_SHIFT (the slider gap below the tiles shrinks from 71 to 55 px so
   the audio card still fits 1216 px); landscape sliders stay on the right. */
#define CONTROL_TILE_H 137
#define CONTROL_TILE_TOP(row) (183+(row)*155)
#define CONTROL_SHIFT 139
/* Landscape audio summary below the tiles (row 2 ends at 475): one text line
   with its baseline at 505, then the 68 px button ending at 587, above the
   close area (bottom-68 = 592) and the handle. */
#define CONTROL_WIDE_AUDIO_Y 505
#define CONTROL_WIDE_AUDIO_END (CONTROL_WIDE_AUDIO_Y+82)
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
static int control_bottom(void){return app.canvas.width==1216?660:(thingsos_audio_active()?1070:918)+CONTROL_SHIFT;}
static int control_left(void){return app.canvas.width==1216?696:36;}
static int control_width(void){return app.canvas.width==1216?484:612;}
static int control_row(unsigned row){static const int portrait[]={446+CONTROL_SHIFT,594+CONTROL_SHIFT,797+CONTROL_SHIFT},wide[]={230,386,561};return app.canvas.width==1216?wide[row]:portrait[row];}
static void control_wifi_details(void){
    if(transfer_blocks_ui()){say("请先结束传书，再修改网络设置");return;}
    app.controls_open=false;navigate(THINGSOS_UI_WIFI);app_controls_wifi_return=true;app.dirty=true;
}
static void control_slider(const char* title,unsigned value,unsigned row,bool enabled){
    int left=control_left(),w=control_width(),cy=control_row(row),track=left+86,len=w-172;
    char text[48];label(title,left,cy-55,29,w-150);
    if(row==1){if(value==50)strlcpy(text,"均衡",sizeof(text));else if(!value)strlcpy(text,"纯冷光",sizeof(text));else if(value==100)strlcpy(text,"纯暖光",sizeof(text));else snprintf(text,sizeof(text),"偏%s %u%%",value<50?"冷":"暖",value<50?100-value:value);}
    else snprintf(text,sizeof(text),"%u%%",value);
    thingsos_font_size(ui_font(),row==1?29:33);thingsos_label_right(ui_font(),&app.canvas,text,left+w-220,cy-55,220);
    unsigned edge=enabled?170:220,ink=enabled?34:200;
    control_round(left,cy-32,64,64,32,2,edge,255);control_round(left+w-64,cy-32,64,64,32,2,edge,255);
    if(row==1){control_glyph(control_sun,29,left+17,cy-14,ink);control_glyph(control_moon,29,left+w-47,cy-14,ink);}
    else {thingsos_rect(&app.canvas,left+23,cy-1,18,2,ink);thingsos_rect(&app.canvas,left+w-41,cy-1,18,2,ink);thingsos_rect(&app.canvas,left+w-33,cy-9,2,18,ink);}
    control_round(track,cy-7,len,14,7,2,enabled?119:220,255);
    if(value)control_round(track,cy-7,len*(int)value/100,14,7,0,0,ink);
    int knob=track+len*(int)value/100;control_round(knob-20,cy-20,40,40,20,2,ink,255);
    if(row==1){label("冷光",track,cy+62,22,100);thingsos_font_size(ui_font(),22);thingsos_label_right(ui_font(),&app.canvas,"暖光",track+len-100,cy+62,100);}
}
/* Portrait audio card. Play/pause is a white circle with a black border and
   a standard glyph; ±15 s seek controls keep the footer consistent with the
   approved UE. Recording gets one clear "end and save" action. */
static void control_play_glyph(int cx,int cy,bool playing){
    if(playing){
        thingsos_rect(&app.canvas,cx-11,cy-12,7,24,0);
        thingsos_rect(&app.canvas,cx+4,cy-12,7,24,0);
    } else {
        for(int i=0;i<20;++i)thingsos_rect(&app.canvas,cx-8+i,cy-10+i/2,2,20-i,0);
    }
}
static void control_audio_card(void){
    int x=36,y=836+CONTROL_SHIFT,w=612,h=190;const int o=CONTROL_SHIFT;
    control_round(x,y,w,h,18,2,34,255);
    bool recording=app.audio.state==THINGSOS_AUDIO_RECORDING||app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED;
    if(recording){
        thingsos_rect(&app.canvas,58,854+o,52,52,0);
        thingsos_rect(&app.canvas,78,864+o,8,16,255);
        thingsos_rect(&app.canvas,76,868+o,2,10,255);thingsos_rect(&app.canvas,90,868+o,2,10,255);
        thingsos_rect(&app.canvas,78,880+o,10,2,255);thingsos_rect(&app.canvas,82,882+o,2,4,255);
        thingsos_rect(&app.canvas,76,886+o,14,2,255);
        label("正在录音",128,872+o,28,300);
        char time[24];app_time(time,sizeof(time),app.audio.position_ms);
        label(time,128,908+o,24,220);
        label("结束后自动保存",128,938+o,20,300);
        reader_control("结束并保存",58,958+o,568,true);
        return;
    }
    thingsos_rect(&app.canvas,58,854+o,52,52,0);
    thingsos_rect(&app.canvas,78,864+o,3,22,255);
    thingsos_rect(&app.canvas,81,864+o,10,3,255);
    thingsos_rect(&app.canvas,72,880+o,10,8,255);
    char title[192];
    strlcpy(title,app.audio.path[0]?app_basename(app.audio.path):"当前音频",sizeof(title));
    label(title,128,872+o,28,390);
    char total[16];app_time(total,sizeof(total),app.audio.duration_ms);
    thingsos_font_size(ui_font(),20);thingsos_label_right(ui_font(),&app.canvas,total,560,872+o,88);
    label(app.audio.state==THINGSOS_AUDIO_PLAYING?"正在播放":"已暂停",128,902+o,20,300);
    unsigned progress=app.audio.duration_ms?(unsigned)((uint64_t)app.audio.position_ms*100/app.audio.duration_ms):0;
    if(progress>100)progress=100;
    thingsos_rect(&app.canvas,58,924+o,568,4,210);
    if(progress)thingsos_rect(&app.canvas,58,924+o,568*(int)progress/100,4,34);
    char time[16];app_time(time,sizeof(time),app.audio.position_ms);
    label(time,58,948+o,18,100);
    uint32_t remain=app.audio.duration_ms>app.audio.position_ms?app.audio.duration_ms-app.audio.position_ms:0;
    char left[24];app_time(left,sizeof(left),remain);snprintf(title,sizeof(title),"-%s",left);
    thingsos_label_right(ui_font(),&app.canvas,title,540,948+o,108);
    control_round(58,958+o,50,50,25,2,34,255);centered_label("-15",58,992+o,18,50);
    control_round(310,952+o,58,58,29,2,34,255);
    control_play_glyph(339,981+o,app.audio.state==THINGSOS_AUDIO_PLAYING);
    control_round(516,958+o,50,50,25,2,34,255);centered_label("+15",516,992+o,18,50);
}
static void app_controls_draw(void){
    int width=app.canvas.width,bottom=control_bottom();bool wide=width==1216,has_audio=thingsos_audio_active();
    thingsos_rect(&app.canvas,0,0,width,bottom-22,255);control_round(0,bottom-44,width,44,22,0,0,255);
    app.font.ui_size_percent=100;app.font.ui_weight_override=app.ui_weight+1;app.font.dither=true;
    char text[80];time_t clock=wall_time();struct tm date;
    if(localtime_r(&clock,&date))strftime(text,sizeof(text),"%H:%M",&date);else strlcpy(text,"--:--",sizeof(text));
    label(text,36,50,23,160);
    if(app.audio.state==THINGSOS_AUDIO_RECORDING||app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED)
        status_mic_draw(width/2-150);
    centered_label(app.wifi_enabled?(thingsos_network_connected()?"Wi-Fi 已连接":"Wi-Fi 未连接"):"Wi-Fi 已关闭",width/2-125,50,21,250);
    snprintf(text,sizeof(text),"%u%%",app.battery);thingsos_font_size(ui_font(),23);thingsos_label_right(ui_font(),&app.canvas,text,width-210,50,116);
    control_round(width-76,32,39,20,0,2,34,255);thingsos_rect(&app.canvas,width-72,36,(int)(31*app.battery/100),12,34);thingsos_rect(&app.canvas,width-36,38,3,8,34);
    label("控制中心",36,139,37,width-150);control_round(width-102,90,66,66,33,2,187,255);control_glyph(control_up,29,width-83,109,34);
    bool bt_connected=false,bt_on=bt_cc_state(&bt_connected);
    for(unsigned i=0;i<4;++i){
        /* Odd tiles (front light, vibration) carry the switch; even tiles open details. */
        int x=36+(int)(i&1)*315,top=CONTROL_TILE_TOP(i/2);bool sw=i&1;
        bool on=i==0?app.wifi_enabled:i==1?app.light_mode!=0:i==2?bt_on:app.haptic_enabled;
        const uint8_t* glyph=i==0?control_wifi:i==1?control_sun:i==2?control_bluetooth:control_vibrate;
        unsigned g=sw?29:33;
        control_round(x,top,297,CONTROL_TILE_H,17,2,170,255);
        int size=sw?51:62,cx=x+20,cy=top+68-size/2;control_round(cx,cy,size,size,size/2,2,on?34:119,on?34:255);
        control_glyph(glyph,g,cx+(size-(int)g)/2,cy+(size-(int)g)/2,on?255:34);
        int tx=x+(sw?84:97);label(i==0?"Wi-Fi":i==1?"前光":i==2?"蓝牙":"振动",tx,top+59,28,sw?100:140);
        const char* state=!on?"已关闭":i==0?(thingsos_network_connected()?"已连接":"未连接"):i==2&&bt_connected?"已连接":"已开启";
        label(state,tx,top+94,21,130);
        if(!sw)control_glyph(control_chevron,24,x+261,top+56,85);
        else {control_round(x+221,top+52,57,33,16,2,on?34:119,on?34:255);control_round(on?x+249:x+225,top+56,22,22,11,0,0,on?255:85);}
    }
    bool light_on=app.light_mode!=0;
    if(light_on){control_slider("亮度",app.light_level,0,true);control_slider("冷暖光",app.light_warmth,1,true);}
    if(light_on)thingsos_rect(&app.canvas,control_left(),wide?449:684+CONTROL_SHIFT,control_width(),2,170);
    control_slider("音量",app.audio_volume,light_on?2:0,true);
    if(has_audio){
        if(!wide)control_audio_card();
        else {int y=CONTROL_WIDE_AUDIO_Y;char time[24];app_time(time,sizeof(time),app.audio.position_ms);
            bool rec=app.audio.state==THINGSOS_AUDIO_RECORDING||app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED;
            label(rec?"正在录音":app_basename(app.audio.path),36,y,24,480);label(time,530,y,22,118);
            reader_control(rec?"返回录音":"返回音频",36,y+14,612,false);
        }
    }
    control_round(width/2-42,bottom-36,84,6,3,0,0,136);
    thingsos_rect(&app.canvas,22,bottom-2,width-44,2,34);
    if(app.controls_wifi_confirm){int x=(width-600)/2;thingsos_rect(&app.canvas,x,300,600,330,0);thingsos_rect(&app.canvas,x+2,302,596,326,255);
        label("关闭 WiFi？",x+30,363,34,540);label("传书将被中断，未完成的文件会清理。",x+30,426,25,540);
        reader_control("取消",x+30,520,250,false);reader_control("关闭 WiFi",x+320,520,250,true);
    }
}
