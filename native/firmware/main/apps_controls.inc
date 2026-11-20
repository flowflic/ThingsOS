// SPDX-License-Identifier: Apache-2.0
/* Metalio apps and control sheet, based on the user-approved local UE.
   All canvas/input state remains on the UI task. Audio only publishes state. */
#define APP_AUDIO_LIMIT 64
typedef struct {char name[192];uint64_t bytes,modified;} app_audio_file_t;
typedef struct {
    app_audio_file_t *music,*records;
    size_t music_count,record_count,music_start,record_start,selected;
    bool preview,playlist,loaded_music,loaded_records;
    unsigned play_mode,serial,shown_seconds;
    uint64_t tick_at,play_requested_at;
    bool play_start_pending,painted,painted_preview,painted_controls;
    /* Plan A: one deferred playback slot. pending_auto retries a play request
       refused while WeRead/transfer owns the network; pending_resume holds the
       position of a paused song that WeRead stopped, for the next play key. */
    char pending_path[512];
    uint32_t pending_ms;
    bool pending_auto,pending_resume,pending_preview;
    uint64_t pending_until,pending_retry_at;
    thingsos_audio_state_t painted_audio;
    thingsos_ui_page_t painted_page;
} app_media_ui_t;
static app_media_ui_t app_media;
static bool app_controls_wifi_return;
/* Bluetooth page opened from the control center (bluetooth_ui.inc). */
static bool app_controls_bt_return;
static thingsos_ui_page_t app_controls_bt_page;
static bool bt_cc_state(bool* connected);
static void bt_cc_toggle(void);
static void bt_cc_open(void);
static bool app_audio_surface(void) {
    return app_media.preview || app.page==THINGSOS_UI_MUSIC || app.page==THINGSOS_UI_RECORDER;
}
static bool app_audio_region(void) {
    return (app_audio_surface() || app_media.painted_preview) && app_media.painted && app_media.painted_page==app.page &&
        !app.controls_refresh && !app.feedback_refresh && !app.force_refresh &&
        !app.transition_refresh && !app.lock_wave_pending && !app.message[0];
}
static void app_audio_presented(bool ok) {
    app_media.painted=ok;app_media.painted_page=app.page;app_media.painted_preview=ok && app_media.preview;
    app_media.painted_controls=app.controls_open;app_media.painted_audio=app.audio.state;
}
static esp_err_t app_audio_present(void) {
    int top=-1,center=342;
    if(!app.controls_open && !app_media.painted_controls && app.canvas.width==684) {
        bool toggled=(app.audio.state==THINGSOS_AUDIO_PLAYING)!=(app_media.painted_audio==THINGSOS_AUDIO_PLAYING);
        if(app_media.preview && (!app_media.painted_preview || toggled))top=620;
        else if(!app_media.preview && !app_media.painted_preview && app.page==THINGSOS_UI_MUSIC && toggled)top=595;
        else if(!app_media.preview && app.page==THINGSOS_UI_RECORDER &&
            (app.audio.state==THINGSOS_AUDIO_RECORDING||app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED) &&
            (app_media.painted_audio==THINGSOS_AUDIO_RECORDING||app_media.painted_audio==THINGSOS_AUDIO_RECORD_PAUSED) &&
            app.audio.state!=app_media.painted_audio){top=540;center=202;}
    }
    /* Include a small halo below the old/new glyphs, never the whole button.
       The board cleans this label first, so the subsequent diff holds it. */
    if(top>=0)return thingsos_board_present_audio_diff_clean(&app.canvas,center-56,top+8,112,56);
    return thingsos_board_present_audio_diff(&app.canvas);
}

#ifdef THINGSOS_HOST_UI
static char host_media_root[512];
#endif
static void app_outline(int x,int y,int w,int h){
    thingsos_rect(&app.canvas,x,y,w,2,0);thingsos_rect(&app.canvas,x,y+h-2,w,2,0);
    thingsos_rect(&app.canvas,x,y,2,h,0);thingsos_rect(&app.canvas,x+w-2,y,2,h,0);
}
static void app_circle(int x,int y,int radius,int thickness){
    for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx)
        if(dx*dx+dy*dy<=radius*radius&&dx*dx+dy*dy>=(radius-thickness)*(radius-thickness))thingsos_rect(&app.canvas,x+dx,y+dy,1,1,0);
}
#include "app_icons.inc"
static void app_icon_bitmap(const uint8_t* packed,unsigned size,int left,int top){
    for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){size_t at=(size_t)y*size+x;
        unsigned gray=(packed[at/2]>>((at&1)?0:4))&15;
        int dx=left+(int)x,dy=top+(int)y;
        if(gray<15&&dx>=0&&dy>=0&&dx<app.canvas.width&&dy<app.canvas.height)
            app.canvas.pixels[(size_t)dy*app.canvas.width+dx]=(uint8_t)(gray*17);
    }
}
static void app_icon(unsigned type,int left,int top){
    if(type==9){
        /* Isometric cube outline representing Cubox. */
        int cx=left+54,cy=top+54;
        for(int i=0;i<=20;++i){
            int dy=(i*11)/20;
            thingsos_rect(&app.canvas,cx-i,cy-22+dy,3,3,0);
            thingsos_rect(&app.canvas,cx+i,cy-22+dy,3,3,0);
            thingsos_rect(&app.canvas,cx-i,cy+dy,3,3,0);
            thingsos_rect(&app.canvas,cx+i,cy+dy,3,3,0);
            thingsos_rect(&app.canvas,cx-20+i,cy+11+dy,3,3,0);
            thingsos_rect(&app.canvas,cx+20-i,cy+11+dy,3,3,0);
        }
        thingsos_rect(&app.canvas,cx-20,cy-11,3,23,0);
        thingsos_rect(&app.canvas,cx,cy-1,3,23,0);
        thingsos_rect(&app.canvas,cx+20,cy-11,3,23,0);
        return;
    }
    if(type==8){
        app_outline(left+28,top+31,52,46);app_circle(left+65,top+43,5,5);
        for(int i=0;i<16;++i){thingsos_rect(&app.canvas,left+32+i,top+70-i,3,3,0);thingsos_rect(&app.canvas,left+47+i,top+55+i,3,3,0);}
        for(int i=0;i<9;++i){thingsos_rect(&app.canvas,left+60+i,top+68-i,3,3,0);thingsos_rect(&app.canvas,left+68+i,top+60+i,3,3,0);}
        return;
    }
    if(type==7){
        app_outline(left+35,top+35,38,38);app_outline(left+44,top+44,20,20);
        for(int i=0;i<3;++i){int p=43+i*11;
            thingsos_rect(&app.canvas,left+p,top+25,3,10,0);thingsos_rect(&app.canvas,left+p,top+73,3,10,0);
            thingsos_rect(&app.canvas,left+25,top+p,10,3,0);thingsos_rect(&app.canvas,left+73,top+p,10,3,0);}
        return;
    }
    if(type==6){
        app_circle(left+54,top+55,24,3);
        thingsos_rect(&app.canvas,left+53,top+36,3,21,0);thingsos_rect(&app.canvas,left+54,top+54,16,3,0);
        thingsos_rect(&app.canvas,left+27,top+24,14,3,0);thingsos_rect(&app.canvas,left+67,top+24,14,3,0);return;
    }
    if(type<APP_ICON_COUNT)app_icon_bitmap(app_icon_glyphs[type],APP_ICON_GLYPH_SIZE,left+25,top+25);
}
static void app_icon_tile(unsigned type,int left,int top){app_icon_bitmap(app_icon_border,APP_ICON_TILE_SIZE,left,top);app_icon(type,left,top);}
static bool app_back(void);
#include "system_ui.inc"
static void app_time(char* text,size_t capacity,uint32_t ms){snprintf(text,capacity,"%02u:%02u",(unsigned)(ms/60000),(unsigned)(ms/1000%60));}
static bool app_audio_path(const char* path){const char* e=strrchr(path,'.');return e&&(!strcasecmp(e,".wav")||!strcasecmp(e,".mp3"));}
static const char* app_basename(const char* path){const char* slash=strrchr(path,'/');return slash?slash+1:path;}
static void app_storage_path(char* path,size_t bytes,const char* folder){
#ifdef THINGSOS_HOST_UI
    if(host_media_root[0]){snprintf(path,bytes,"%s/%s",host_media_root,folder);return;}
#endif
    snprintf(path,bytes,"/sdcard/%s",folder);
}
static void app_audio_scan(bool recordings){
    app_audio_file_t** list=recordings?&app_media.records:&app_media.music;
    size_t* count=recordings?&app_media.record_count:&app_media.music_count;
    *count=0;if(!media())return;
    if(!*list)*list=thingsos_psram_calloc(APP_AUDIO_LIMIT,sizeof(**list));
    if(!*list)return;
    char folder[600];app_storage_path(folder,sizeof(folder),recordings?"audio":"music");
    if(mkdir(folder,0775)&&errno!=EEXIST)return;
    DIR* dir=opendir(folder);if(!dir)return;struct dirent* entry;
    while((entry=readdir(dir))){
        if(entry->d_name[0]=='.'||strlen(entry->d_name)>=sizeof((*list)[0].name)||!app_audio_path(entry->d_name))continue;
        char path[800];snprintf(path,sizeof(path),"%s/%.191s",folder,entry->d_name);struct stat info;
        if(stat(path,&info)||!S_ISREG(info.st_mode))continue;
        app_audio_file_t item={.bytes=(uint64_t)info.st_size,.modified=(uint64_t)info.st_mtime};strlcpy(item.name,entry->d_name,sizeof(item.name));
        size_t at=0;while(at<*count&&(recordings?(*list)[at].modified>=item.modified:strcasecmp((*list)[at].name,item.name)<0))++at;
        if(at>=APP_AUDIO_LIMIT)continue;
        if(*count<APP_AUDIO_LIMIT)++*count;
        memmove(*list+at+1,*list+at,(*count-at-1)*sizeof(**list));(*list)[at]=item;
    }
    closedir(dir);
    if(recordings)app_media.loaded_records=true;else app_media.loaded_music=true;
}
static void app_light_apply(void){thingsos_board_frontlight_mix(app.light_mode!=0,app.light_level,app.light_warmth);}
static void app_volume_set(unsigned value){
    app.audio_volume=value>100?100:value;
    thingsos_audio_volume(app.audio_volume);
    preferences_save();
    app.volume_overlay_until=now()+VOLUME_OVERLAY_HOLD_MS;
    app.volume_overlay_pending=!app.controls_open && app.page!=THINGSOS_UI_LOCK;
    feedback_redraw();
}
static void app_files_open(const char* folder){
    if(media()){
        char path[600];app_storage_path(path,sizeof(path),"audio");mkdir(path,0775);app_storage_path(path,sizeof(path),"music");mkdir(path,0775);
        if(!thingsos_browser_scan(&app.browser,"/sdcard",folder))say("SD 卡目录无法打开");
    }
    app.file_query[0]=0;navigate(THINGSOS_UI_FILES);
}
static void app_audio_pending_clear(void){
    app_media.pending_auto=app_media.pending_resume=false;app_media.pending_path[0]=0;
}
static bool app_audio_begin(const char* path,uint32_t offset,bool preview){
    if(!thingsos_audio_play(path,offset))return false;
    app_media.preview=preview;app_media.playlist=!preview;
    /* Coalesce a quick STARTING -> PLAYING into the acknowledged frame. */
    app_media.play_start_pending=true;app_media.play_requested_at=now();
    return true;
}
#define APP_AUDIO_QUEUE_MS 45000
static void app_audio_queue(const char* path,uint32_t offset,bool preview){
    if(path!=app_media.pending_path)strlcpy(app_media.pending_path,path,sizeof(app_media.pending_path));
    app_media.pending_ms=offset;app_media.pending_preview=preview;
    app_media.pending_resume=false;app_media.pending_auto=true;
    app_media.pending_until=now()+APP_AUDIO_QUEUE_MS;app_media.pending_retry_at=now()+500;
    ESP_LOGI("ThingsOS_audio_ui","Play queued weidu=%d net=%d offset=%u",thingsos_weidu_busy(),thingsos_network_busy(),(unsigned)offset);
    say(thingsos_weidu_busy()?"微读同步中，完成后自动播放":"音频准备中，稍后自动播放");
}
static void app_audio_start_or_queue(const char* path,uint32_t offset,bool preview){
    /* The play command copies the path, so clearing the slot is safe even
       when path points into it. */
    if(app_audio_begin(path,offset,preview)){app_audio_pending_clear();return;}
    app_audio_queue(path,offset,preview);
}
static void app_audio_play_file(const char* path,bool preview){
    if(app.audio.state==THINGSOS_AUDIO_RECORDING||app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED){say("请先结束并保存录音");return;}
    app_audio_start_or_queue(path,0,preview);
}
/* Called on the UI task with thingsos_io_lock held when a WeRead chapter job or
   a Cubox job was refused the network claim. Only a paused song yields: it is
   stopped and its position kept, so the next play key resumes it. Playing audio
   and recordings are untouched. */
static bool app_audio_yield_paused(const char* owner){
    thingsos_audio_status_t s;thingsos_audio_status(&s);
    if(s.state!=THINGSOS_AUDIO_PLAY_PAUSED||!s.path[0])return false;
    bool remember=!app_media.preview;
    if(remember)strlcpy(app_media.pending_path,s.path,sizeof(app_media.pending_path));
    if(!thingsos_audio_quiesce_locked()||thingsos_audio_active()){
        if(remember)app_audio_pending_clear();
        thingsos_audio_status(&app.audio);return false;
    }
    thingsos_audio_status(&app.audio);
    app_media.pending_auto=false;app_media.pending_resume=remember;app_media.pending_ms=s.position_ms;
    app_media.pending_preview=false;app_media.preview=false;
    ESP_LOGI("ThingsOS_audio_ui","Paused audio parked for %s at %u ms",owner,(unsigned)s.position_ms);
    return true;
}
static bool app_audio_yield_for_weidu(void){return app_audio_yield_paused("WeRead");}
/* Cubox hook: park only when the paused song is the sole claimant, so an
   upload, OTA or USB job keeps its refusal without stopping the music. */
static bool app_audio_yield_for_cubox(void){
    return !thingsos_network_wifi_busy()&&app_audio_yield_paused("Cubox");
}
static void app_music_select(size_t index){
    if(index>=app_media.music_count)return;
    app_media.selected=index;char path[512];snprintf(path,sizeof(path),"/sdcard/music/%s",app_media.music[index].name);
    app_audio_play_file(path,false);
}
static void app_music_step(int direction){
    if(!app_media.music_count)return;
    size_t index=app_media.selected;
    index=direction>0?(index+1)%app_media.music_count:(index?index-1:app_media.music_count-1);
    app_music_select(index);
}
/* One physical AI key action shared by the BOOT key and the settings test
   button. Recording is intentionally page-independent; the audio service
   owns the file and auto-finalizes it on stop. */
static bool app_ai_record_toggle(void){
    if(app.pin.required || app.page==THINGSOS_UI_LOCK_PIN)return false;
    if(alarm_active())return false;
    thingsos_audio_status_t current;thingsos_audio_status(&current);
    record_feedback_poll(&current);app.audio=current;
    if(app.record_start_pending||app.record_save_pending||
       app.audio.state==THINGSOS_AUDIO_STARTING||app.audio.state==THINGSOS_AUDIO_STOPPING)return false;
    if(app.audio.state==THINGSOS_AUDIO_RECORDING||app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED){
        if(!thingsos_audio_stop()){record_feedback_show(RECORD_NOTICE_SAVE_ERROR,"无法结束录音，请稍后再试");return false;}
        thingsos_audio_status(&app.audio);
        app.record_save_pending=true;
        app_media.loaded_records=false;
        record_feedback_show(RECORD_NOTICE_SAVING,NULL);
        return true;
    }
    if(app.audio.state==THINGSOS_AUDIO_PLAYING||app.audio.state==THINGSOS_AUDIO_PLAY_PAUSED)
        thingsos_audio_stop();
    app_audio_pending_clear();
    if(!thingsos_audio_record()){
        if(thingsos_bluetooth_connected())record_feedback_show(RECORD_NOTICE_START_ERROR,"请先断开蓝牙耳机再录音");
        else if(thingsos_bluetooth_enabled())record_feedback_show(RECORD_NOTICE_START_ERROR,"蓝牙正在忙，请稍后再录音");
        else record_feedback_show(RECORD_NOTICE_START_ERROR,"请检查 SD 卡或稍后重试");
        return false;
    }
    app_media.loaded_records=false;
    app.record_start_pending=true;record_feedback_show(RECORD_NOTICE_PREPARING,NULL);
    return true;
}
static void app_audio_toggle(void){
    if(app.audio.state==THINGSOS_AUDIO_STARTING||app.audio.state==THINGSOS_AUDIO_STOPPING)return;
    if(app.audio.state==THINGSOS_AUDIO_PLAYING)thingsos_audio_pause(true);
    else if(app.audio.state==THINGSOS_AUDIO_PLAY_PAUSED)thingsos_audio_pause(false);
    else if(app_media.pending_auto){app_audio_pending_clear();say("已取消等待播放");}
    else if(app_media.pending_resume&&app_media.pending_path[0])app_audio_start_or_queue(app_media.pending_path,app_media.pending_ms,false);
    else if(app_media.music_count)app_music_select(app_media.selected<app_media.music_count?app_media.selected:0);
    else say("请将 WAV / MP3 放入 SD 卡 music 目录");
    /* The worker acknowledgement redraws the changed controls. Painting the
       old state here causes a second full scan immediately afterwards. */
}
#include "alarm_ui.inc"
static void app_controls_set(bool open){
    if(app.pin.required || app.page==THINGSOS_UI_LOCK_PIN)return;
    if(alarm_active())return;
    if(app.asleep||app.page==THINGSOS_UI_LOCK)return;
    app.controls_open=open;app.controls_refresh=true;app.controls_wifi_confirm=false;app.audio_progress=false;app.ripple_pending=false;
    app.transition_refresh=true;app.dirty=true;thingsos_tilt_reset(&app.tilt);thingsos_board_tilt_active(false);
    thingsos_runtime_overlay(&app.runtime,open||app.menu||app.page!=THINGSOS_UI_READING,now());
}
static void app_wifi_toggle(void){
    if(app.wifi_enabled){
        thingsos_transfer_state_t transfer={0};thingsos_network_transfer(&transfer);
        if(transfer.active&&!transfer.ota){app.controls_wifi_confirm=true;app.dirty=true;return;}
    }
    bool enable=!app.wifi_enabled;
    if(thingsos_network_radio(enable)!=ESP_OK){say("网络任务处理中，请稍后重试");return;}
    app.wifi_enabled=enable;if(!enable){app.hotspot=false;app.service_on=app.service_pinned=false;app.wifi_requested=0;}
    else {app.wifi_scan_valid=false;wifi_scan(true);}
    preferences_save();app.dirty=true;
}
static void app_enter(thingsos_ui_page_t page){
    if(page==THINGSOS_UI_TRANSFER&&thingsos_audio_active()){
        thingsos_audio_status(&app.audio);
        if(app.audio.state==THINGSOS_AUDIO_PLAY_PAUSED){
            /* Pause keeps the file and audio claim for resume. Release both
               before handing storage ownership to file transfer. */
            if(!thingsos_audio_quiesce_locked()){
                say("结束播放失败，请稍后重试");return;
            }
            thingsos_audio_status(&app.audio);app.dirty=true;
        }else{
            say(app.audio.state==THINGSOS_AUDIO_PLAYING?"请先暂停播放，再开启传输":
                app.audio.state==THINGSOS_AUDIO_RECORDING||app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED?
                "请先结束并保存录音，再开启传输":"音频处理中，请稍后重试");return;
        }
    }
    if(page==THINGSOS_UI_FILES){app_files_open("/sdcard");return;}
    if(page==THINGSOS_UI_CUBOX){cubox_enter();return;}
    if(page==THINGSOS_UI_RECORDER)app_audio_scan(true);
    if(page==THINGSOS_UI_MUSIC)app_audio_scan(false);
    if(page==THINGSOS_UI_WALLPAPER){app.wallpaper_step=0;app.wallpaper_start=0;}
    if(page==THINGSOS_UI_ALARM){alarm_edit=-1;alarm_picking=false;}
    if(page==THINGSOS_UI_TRANSFER){
        if(!app.wifi_enabled){say("请先在控制菜单开启 WiFi");return;}
        if(thingsos_network_start(false)!=ESP_OK){say("网络启动失败");return;}
    }
    navigate(page);
}
static bool app_back(void){
    if(alarm_back())return true;
    if(app.controls_open){app_controls_set(false);return true;}
    if(app_media.preview){app_media.preview=false;thingsos_audio_stop();app.dirty=true;return true;}
    if(app.page==THINGSOS_UI_APPS)return true;
    if(app.page==THINGSOS_UI_WIFI){bool controls=app_controls_wifi_return;navigate(app.network_return<THINGSOS_UI_COUNT?app.network_return:THINGSOS_UI_APPS);if(controls)app_controls_set(true);return true;}
    if(app.page==THINGSOS_UI_RECORDER||app.page==THINGSOS_UI_MUSIC||app.page==THINGSOS_UI_TRANSFER||app.page==THINGSOS_UI_SYSTEM){navigate(app.app_return<THINGSOS_UI_COUNT?app.app_return:THINGSOS_UI_APPS);return true;}
    if(app.page==THINGSOS_UI_FILES&&app.app_return==THINGSOS_UI_APPS&&!app.file_action_open&&!app.file_more){
        if(strcmp(app.browser.path,"/sdcard")){thingsos_browser_parent(&app.browser,"/sdcard");app.list_start=0;app.dirty=true;}
        else navigate(THINGSOS_UI_APPS);
        return true;
    }
    return false;
}
#include "control_center.inc"
static bool app_hold(int x,int y,uint64_t elapsed){
    if(!app.controls_open && wallpaper_hold(x,y,elapsed))return true;
    if(!app.controls_open||app.controls_wifi_confirm||elapsed<580||x<36||x>=333)return false;
    if(y>=CONTROL_TILE_TOP(1)&&y<CONTROL_TILE_TOP(1)+CONTROL_TILE_H){bt_cc_open();return true;}
    if(y<CONTROL_TILE_TOP(0)||y>=CONTROL_TILE_TOP(0)+CONTROL_TILE_H)return false;
    if(transfer_blocks_ui()){say("请先结束传书，再修改网络设置");return true;}
    control_wifi_details();return true;
}
static bool app_controls_swipe(int x,int y,int dx,int dy){
    if(app.pin.required || app.page==THINGSOS_UI_LOCK_PIN)return false;
    if(app.asleep||app.page==THINGSOS_UI_LOCK||app.page==THINGSOS_UI_USB)return false;
    if(!app.controls_open&&y<74&&dy>72&&dy>abs(dx)){app_controls_set(true);return true;}
    if(app.controls_open&&((y<164)||y>=control_bottom()-68)&&dy< -72&&-dy>abs(dx)){app_controls_set(false);return true;}
    return false;
}
static void app_audio_list_draw(bool recordings,int top,unsigned rows){
    size_t count=recordings?app_media.record_count:app_media.music_count,start=recordings?app_media.record_start:app_media.music_start;
    app_audio_file_t* files=recordings?app_media.records:app_media.music;
    if(!count){label(recordings?"还没有录音":"将 WAV / MP3 放入 /sdcard/music",36,top+55,26,612);return;}
    for(size_t i=0;i<rows&&start+i<count;++i){int y=top+(int)i*96;app_audio_file_t* file=files+start+i;
        label(file->name,48,y+34,27,recordings?470:550);char text[80];char upload_status[24]="未上传";
        if(recordings){char path[512];snprintf(path,sizeof(path),"/sdcard/audio/%s",file->name);thingsos_ima_file_status(path,upload_status,sizeof(upload_status));}
        snprintf(text,sizeof(text),recordings?"%.1f MB · %s":"%.1f MB",(double)file->bytes/1048576.0,upload_status);label(text,48,y+70,21,480);
        if(recordings){app_outline(535,y+18,108,55);centered_label("上传",535,y+56,23,108);}
        else label(">",608,y+50,26,28);
        thingsos_rect(&app.canvas,36,y+95,612,1,0);}
    if(count>rows){reader_control("上一页",36,1005,180,false);reader_control("下一页",468,1005,180,false);}
}
static void ai_key_draw(void){
    header("AI 按键");
    label("按下设备顶部的 AI 键，开始或结束后台录音。",36,258,26,612);
    label("录音不打断当前阅读，结束后自动保存。",36,298,24,612);
    label("文件保存在 /sdcard/audio。",36,330,23,612);
    label("AI 键功能",36,400,28,300);
    static const char* const names[]={"关闭","录音"};
    reader_refresh_segments(names,420,368,176,2,app.ai_key_enabled?1:0);
    label("录音文件按时间命名，结束后自动出现在录音页。",36,480,23,612);
    bool active=app.audio.state==THINGSOS_AUDIO_RECORDING||app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED;
    reader_control(active?"结束录音并保存":app.ai_key_enabled?"按 AI 键试录":"AI 键录音已关闭",36,560,612,true);
    label("AI 键与 BOOT 共用：开机时按住进入下载模式。",36,680,21,612);
    label("系统运行中短按切换录音。",36,710,21,612);
}
static bool app_launcher_audio_visible(void){
    /* A paused song can be resumed from Music; it is not background playback.
       Paused recording stays visible because its file still needs saving. */
    return thingsos_audio_active()&&(app.audio.state==THINGSOS_AUDIO_PLAYING||
        app.audio.state==THINGSOS_AUDIO_RECORDING||app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED);
}
static bool app_draw(void){
    if(alarm_draw())return true;
    if(app_system_draw())return true;
    if(app.page==THINGSOS_UI_AI_SETTINGS){ai_key_draw();return true;}
    if(bt_ui_draw())return true;
    if(ima_draw())return true;
    if(app.page==THINGSOS_UI_APPS){
        /* User refinement: launcher has no large heading, swipe hint or SD size. */
        const char* names[]={"传输","网络","文件管理","录音","音乐","闹钟","系统","壁纸","Cubox"};
        const unsigned icons[]={0,1,2,3,4,6,7,8,9};
        for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i){int x=78+(int)(i%3)*210,y=150+(int)(i/3)*233;app_icon_tile(icons[i],x,y);centered_label(names[i],36+(int)(i%3)*210,y+161,29,192);}
        if(app_launcher_audio_visible()){
            thingsos_rect(&app.canvas,36,957,612,1,0);label(app.audio.state==THINGSOS_AUDIO_RECORDING||app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED?"录音进行中":"音频播放",48,1002,24,550);
            label(app_basename(app.audio.path),48,1047,25,550);
        }
        nav();return true;
    }
    if(app.page!=THINGSOS_UI_RECORDER&&app.page!=THINGSOS_UI_MUSIC)return false;
    bool record=app.page==THINGSOS_UI_RECORDER;app.font.dither=true;
    header(record?"录音":"音乐");
    if(record){thingsos_font_size(ui_font(),24);thingsos_label_right(ui_font(),&app.canvas,"ima 设置",490,150,158);}
    if(!media()){centered_label("请插入 SD 卡",36,410,36,612);centered_label(record?"录音保存到 /audio":"音乐文件放入 /music",36,475,25,612);nav();return true;}
    char text[96];app_time(text,sizeof(text),app.audio.position_ms);
    if(record){
        bool active=app.audio.state==THINGSOS_AUDIO_RECORDING||app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED;
        centered_label(app.audio.state==THINGSOS_AUDIO_RECORDING?"录音中":app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED?"已暂停":app.audio.state==THINGSOS_AUDIO_STARTING?"准备音频…":"准备录音",36,297,28,612);
        centered_label(active?text:"00:00",36,420,78,612);centered_label("保存到 /sdcard/audio",36,480,23,612);
        if(active){reader_control(app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED?"继续":"暂停",92,540,220,false);reader_control("结束并保存",372,540,220,true);}
        else reader_control("开始录音",182,540,320,true);
        label("最近录音",36,705,28,370);label("全部文件 >",480,705,23,168);app_audio_list_draw(true,738,2);
    }else{
        char title[192];strlcpy(title,app.audio.path[0]?app_basename(app.audio.path):app_media.music_count?app_media.music[0].name:"音乐文件",sizeof(title));
        app_outline(36,250,612,210);app_icon(4,64,307);label(title,210,334,31,410);label(app.audio.state==THINGSOS_AUDIO_PLAYING?"播放中":app.audio.state==THINGSOS_AUDIO_STARTING?"准备音频…":"已暂停",210,399,25,410);
        unsigned progress=app.audio.duration_ms?(unsigned)((uint64_t)app.audio.position_ms*100/app.audio.duration_ms):0;if(progress>100)progress=100;
        thingsos_rect(&app.canvas,48,510,588,3,0);app_circle(48+(int)(588*progress/100),511,13,3);
        label(text,36,562,23,220);app_time(text,sizeof(text),app.audio.duration_ms);thingsos_font_size(ui_font(),23);thingsos_label_right(ui_font(),&app.canvas,text,430,562,218);
        reader_control("上一首",36,595,180,false);reader_control(app.audio.state==THINGSOS_AUDIO_PLAYING?"暂停":"播放",252,595,180,true);reader_control("下一首",468,595,180,false);
        label(app_media.play_mode==2?"单曲循环":app_media.play_mode==1?"列表循环":"顺序播放",36,735,24,320);snprintf(text,sizeof(text),"音量 %u%% >",app.audio_volume);label(text,464,735,24,184);
        app_audio_list_draw(false,770,2);
    }
    nav();return true;
}
static bool app_controls_tap(int x,int y){
    if(!app.controls_open)return false;
    int width=app.canvas.width;bool wide=width==1216;
    if(app.controls_wifi_confirm){int left=(width-600)/2;
        if(y>=520&&y<588){if(x<left+300)app.controls_wifi_confirm=false;else{app.controls_wifi_confirm=false;app.wifi_pending_off=true;thingsos_network_cancel_upload();say("正在结束传书并关闭 WiFi…");}app.dirty=true;}return true;}
    int bottom=control_bottom();
    bool audio_card=thingsos_audio_active()&&(wide?(x<648&&y<CONTROL_WIDE_AUDIO_END):y<1026+CONTROL_SHIFT);
    if((x>=width-102&&x<width-36&&y>=90&&y<156)||
       (y>=bottom-68&&!audio_card)){app_controls_set(false);return true;}
    for(unsigned row=0;row<2;++row){int top=CONTROL_TILE_TOP(row);
        if(y<top||y>=top+CONTROL_TILE_H||x>=648)continue;
        if(x>=36&&x<333){
            /* Chevron opens details; the rest of the tile toggles. */
            if(!row){if(x>=285)control_wifi_details();else app_wifi_toggle();}
            else {if(x>=285)bt_cc_open();else bt_cc_toggle();}
        }
        else if(x>=351&&!row){if(app.light_mode){app.light_last_mode=app.light_mode;app.light_mode=0;}else app.light_mode=app.light_last_mode?app.light_last_mode:3;app_light_apply();preferences_save();app.dirty=true;}
        else if(x>=351){app.haptic_enabled=!app.haptic_enabled;preferences_save();if(app.haptic_enabled)thingsos_board_haptic(50);app.dirty=true;}
        return true;
    }
    int left=control_left(),w=control_width();
    bool light_on=app.light_mode!=0;unsigned volume_row=light_on?2:0;
    for(unsigned row=0;row<3;++row){int cy=control_row(row);if(y<cy-35||y>=cy+35||x<left||x>=left+w)continue;
        if(!light_on&&row!=volume_row)return true;
        unsigned* value=row==volume_row?&app.audio_volume:row==0?&app.light_level:&app.light_warmth;
        {int track=left+86,length=w-172;
            unsigned step=value==&app.light_level?1u:value==&app.light_warmth?10u:5u;
            int snap=value==&app.light_level?1:5;
            if(x<left+64)*value=*value>=step?*value-step:0;
            else if(x>=left+w-64)*value=*value<=(unsigned)(100-step)?*value+step:100;
            else if(x>=track-20&&x<=track+length+20){int percent=(x-track)*100/length;percent=percent<0?0:percent>100?100:percent;*value=(unsigned)((percent+snap/2)/snap*snap);}
            else return true;
        }
        if(row==1&&light_on)app.light_mode=app.light_last_mode=3;
        if(row==volume_row)thingsos_audio_volume(app.audio_volume);else app_light_apply();
        preferences_save();app.dirty=true;return true;
    }
    if(!wide&&thingsos_audio_active()&&y>=836+CONTROL_SHIFT&&y<1026+CONTROL_SHIFT){
        bool recording=app.audio.state==THINGSOS_AUDIO_RECORDING||app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED;
        if(recording){
            if(y>=958+CONTROL_SHIFT){app_ai_record_toggle();return true;}
            app.controls_open=false;app_enter(THINGSOS_UI_RECORDER);return true;
        }
        if(y>=952+CONTROL_SHIFT&&y<1018+CONTROL_SHIFT){
            if(x>=58&&x<108)thingsos_audio_seek(app.audio.position_ms>15000?app.audio.position_ms-15000:0);
            else if(x>=310&&x<368)thingsos_audio_pause(app.audio.state==THINGSOS_AUDIO_PLAYING);
            else if(x>=516&&x<566)thingsos_audio_seek(app.audio.position_ms+15000);
            else return true;
            return true; /* Wait for acknowledged playback state/position. */
        }
        app.controls_open=false;app_enter(THINGSOS_UI_MUSIC);return true;
    }
    if(wide&&thingsos_audio_active()&&x<648&&y>=CONTROL_WIDE_AUDIO_Y-30&&y<CONTROL_WIDE_AUDIO_END){
        app.controls_open=false;app_enter(app.audio.state==THINGSOS_AUDIO_RECORDING||app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED?THINGSOS_UI_RECORDER:THINGSOS_UI_MUSIC);return true;
    }
    return true;
}
static bool app_tap(int x,int y){
    if(alarm_tap(x,y))return true;
    if(bt_ui_tap(x,y))return true;
    if(ima_tap(x,y))return true;
    if(app_controls_tap(x,y))return true;
    if(app_media.preview){
        if(y>=620&&y<688){app_audio_toggle();}
        else {app_media.preview=false;thingsos_audio_stop();app.dirty=true;}return true;
    }
    if(app.page==THINGSOS_UI_WIFI&&y>=235&&y<310){app_wifi_toggle();return true;}
    if(app_system_tap(x,y))return true;
    if(app.page!=THINGSOS_UI_APPS&&x<90&&y>=74&&y<200&&app_back())return true;
    if(y>=1098)return false;
    if(app.page==THINGSOS_UI_APPS){
        if(y>=150&&y<796&&x>=36&&x<648){unsigned column=(unsigned)(x-36)/210,row=(unsigned)(y-150)/233,index=row*3+column;
            const thingsos_ui_page_t pages[]={THINGSOS_UI_TRANSFER,THINGSOS_UI_WIFI,THINGSOS_UI_FILES,THINGSOS_UI_RECORDER,THINGSOS_UI_MUSIC,THINGSOS_UI_ALARM,THINGSOS_UI_SYSTEM,THINGSOS_UI_WALLPAPER,THINGSOS_UI_CUBOX};
            if(index<sizeof(pages)/sizeof(pages[0])&&x<36+(int)column*210+192&&y<150+(int)row*233+180)app_enter(pages[index]);}
        else if(y>=950&&app_launcher_audio_visible())app_enter(app.audio.state==THINGSOS_AUDIO_RECORDING||app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED?THINGSOS_UI_RECORDER:THINGSOS_UI_MUSIC);
        return true;
    }
    if(app.page!=THINGSOS_UI_RECORDER&&app.page!=THINGSOS_UI_MUSIC)return false;
    if(!media())return true;
    bool record=app.page==THINGSOS_UI_RECORDER;
    if(record){
        if(y>=540&&y<608){
            if(app.audio.state==THINGSOS_AUDIO_RECORDING||app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED){
                if(x<342)thingsos_audio_pause(app.audio.state!=THINGSOS_AUDIO_RECORD_PAUSED);else {ima_save_pending();thingsos_audio_stop();}
            }else if(!thingsos_audio_record()){
                if(thingsos_bluetooth_connected())say("蓝牙音频已连接，请先断开蓝牙耳机再录音");
                else if(thingsos_bluetooth_enabled())say("蓝牙正在忙，请稍后再录音");
                else say("无法开始录音，请稍后重试");
            }
            app_media.preview=false;app.dirty=true;return true;
        }
        if(y>=666&&y<722&&x>=460){app_files_open("/sdcard/audio");return true;}
        if(y>=738&&y<930){size_t at=app_media.record_start+(size_t)(y-738)/96;
            if(x>=530){ima_recording_upload(at);return true;}
            if(at<app_media.record_count){char path[512];snprintf(path,sizeof(path),"/sdcard/audio/%s",app_media.records[at].name);app_audio_play_file(path,true);}return true;}
    }else{
        if(y>=470&&y<580&&app.audio.duration_ms){unsigned percent=x<48?0:x>636?100:(unsigned)(x-48)*100/588;thingsos_audio_seek((uint32_t)((uint64_t)app.audio.duration_ms*percent/100));app.dirty=true;return true;}
        if(y>=595&&y<663){if(x<216)app_music_step(-1);else if(x>=468)app_music_step(1);else app_audio_toggle();return true;}
        if(y>=700&&y<754){if(x>=440)app_controls_set(true);else{app_media.play_mode=(app_media.play_mode+1)%3;app.dirty=true;}return true;}
        if(y>=770&&y<962){size_t at=app_media.music_start+(size_t)(y-770)/96;if(at<app_media.music_count)app_music_select(at);return true;}
    }
    if(y>=1005&&y<1073){size_t* start=record?&app_media.record_start:&app_media.music_start,count=record?app_media.record_count:app_media.music_count;
        if(x<216)*start=*start>=2?*start-2:0;else if(x>=468&&*start+2<count)*start+=2;app.dirty=true;}
    return true;
}
static void app_preview_draw(void){
    ima_overlay();
    if(!app_media.preview)return;
    thingsos_rect(&app.canvas,36,420,612,380,0);thingsos_rect(&app.canvas,38,422,608,376,255);
    centered_label("录音试听",60,484,32,564);centered_label(app_basename(app.audio.path),60,548,25,564);
    char time[24];app_time(time,sizeof(time),app.audio.position_ms);centered_label(time,60,590,24,564);
    reader_control(app.audio.state==THINGSOS_AUDIO_PLAYING?"暂停":"播放",60,620,564,true);
    reader_control("关闭",60,715,564,false);
}
static void app_poll(void){
    thingsos_audio_status_t current;thingsos_audio_status(&current);
    bool changed=current.serial!=app_media.serial;
    if(current.state!=app.audio.state || current.error!=app.audio.error)
        thingsos_diag_event(DIAG_TASK,3,current.state,current.serial,current.error,0);
    bool visual_changed=current.state!=app.audio.state || current.duration_ms!=app.audio.duration_ms ||
        strcmp(current.path,app.audio.path) || strcmp(current.message,app.audio.message);
    bool media_visible=app.page==THINGSOS_UI_RECORDER||app.page==THINGSOS_UI_MUSIC||
        app.page==THINGSOS_UI_APPS||app.controls_open||app_media.preview;
    bool starting_delay=app_media.play_start_pending && current.state==THINGSOS_AUDIO_STARTING &&
        now()-app_media.play_requested_at<2000;
    bool starting_due=app_media.play_start_pending && current.state==THINGSOS_AUDIO_STARTING && !starting_delay;
    if(current.state!=THINGSOS_AUDIO_STARTING || starting_due)app_media.play_start_pending=false;
    bool was_recording=app.audio.state==THINGSOS_AUDIO_RECORDING||app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED;
    bool recording=current.state==THINGSOS_AUDIO_RECORDING||current.state==THINGSOS_AUDIO_RECORD_PAUSED;
    bool recording_failure=current.state==THINGSOS_AUDIO_ERROR&&
        (app.record_start_pending||app.record_save_pending||was_recording);
    record_feedback_poll(&current);
    app.audio=current;
    if(was_recording!=recording)feedback_redraw();
    if(changed){
        app_media.serial=current.serial;app.audio_progress=false;
        if(current.state==THINGSOS_AUDIO_ERROR){
            if(!recording_failure)say(current.message);
            app_media.preview=false;
        }
        if(current.state==THINGSOS_AUDIO_IDLE){app_audio_scan(true);if(app_media.preview)app_media.preview=false;
            if(current.ended&&app_media.playlist&&app_media.music_count){
                if(app_media.play_mode==2)app_music_select(app_media.selected);
                else if(app_media.selected+1<app_media.music_count||app_media.play_mode==1)app_music_step(1);
            }}
        if(media_visible && visual_changed && !starting_delay)app.dirty=true;
    }
    if(starting_due && media_visible)app.dirty=true; /* Slow start still gets bounded feedback. */
    if(app_media.pending_auto && now()>=app_media.pending_retry_at){
        if(recording||(current.state!=THINGSOS_AUDIO_IDLE&&current.state!=THINGSOS_AUDIO_ERROR))app_audio_pending_clear();
        else if(app_audio_begin(app_media.pending_path,app_media.pending_ms,app_media.pending_preview)){
            ESP_LOGI("ThingsOS_audio_ui","Queued play started offset=%u",(unsigned)app_media.pending_ms);
            app_audio_pending_clear();
        }
        else if(now()>=app_media.pending_until){app_audio_pending_clear();say("音频繁忙，或蓝牙正在启动，请稍后重试");}
        else app_media.pending_retry_at=now()+500;
    }
    unsigned seconds=current.position_ms/1000;
    /* Each progress update is a ~0.45 s panel scan whose line preparation
       competes with MP3 decode for PSRAM. Playback updates every 2 s (user
       choice 2026-11-19); the recorder keeps its 1 s timer. */
    unsigned tick_ms=current.state==THINGSOS_AUDIO_PLAYING?2000:1000;
    bool tick=seconds!=app_media.shown_seconds&&now()-app_media.tick_at>=tick_ms;
    if(!alarm_active()&&!changed&&tick&&!starting_delay&&
       (current.state==THINGSOS_AUDIO_PLAYING||current.state==THINGSOS_AUDIO_RECORDING)&&
       (app.page==THINGSOS_UI_RECORDER||app.page==THINGSOS_UI_MUSIC)&&!app.controls_open&&!app_media.preview&&!app.dirty){
        app.audio_progress=true;app.dirty=true;
    }
    if(changed||tick){app_media.shown_seconds=seconds;app_media.tick_at=now();}
    if(app.wifi_pending_off){thingsos_transfer_state_t transfer={0};thingsos_network_transfer(&transfer);
        if(!transfer.active){app.wifi_pending_off=false;if(app.wifi_enabled)app_wifi_toggle();}}
    ima_poll();
    cubox_poll();
    bt_ui_poll();
}
