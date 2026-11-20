// SPDX-License-Identifier: Apache-2.0
/* UI-task-only alarm controller. Uses the existing music decoder/output route.
   No display, RTC, power or charger work is performed from another task. */
#include "thingsos/alarm.h"
static thingsos_alarm_t alarms[THINGSOS_ALARM_LIMIT],alarm_draft;
static int alarm_edit=-1,alarm_ringing=-1;
static bool alarm_picking,alarm_audio_owned;
static bool alarm_schedule_open;
static unsigned alarm_date_field;
static uint32_t alarm_date_draft;
static size_t alarm_music_start;
static time_t alarm_checked;
static uint64_t alarm_pending[THINGSOS_ALARM_LIMIT],alarm_snooze[THINGSOS_ALARM_LIMIT];
static uint64_t alarm_until,alarm_retry;
static char alarm_message[128];
static void power_action(unsigned action);
static void unlock_screen(void);
static bool alarm_active(void){return alarm_ringing>=0;}

static bool alarm_store(const thingsos_alarm_t* values){
    struct {uint32_t magic;thingsos_alarm_t items[THINGSOS_ALARM_LIMIT];} blob={.magic=0x414c0002};
    memcpy(blob.items,values,sizeof(blob.items));
    nvs_handle_t h;if(nvs_open("thingsos-ui",NVS_READWRITE,&h)!=ESP_OK)return false;
    bool ok=nvs_set_blob(h,"alarms-v2",&blob,sizeof(blob))==ESP_OK && nvs_commit(h)==ESP_OK;
    nvs_close(h);return ok;
}
static void alarm_load(void){
    memset(alarms,0,sizeof(alarms));
    struct {uint32_t magic;thingsos_alarm_t items[THINGSOS_ALARM_LIMIT];} blob={0};
    nvs_handle_t h;if(nvs_open("thingsos-ui",NVS_READONLY,&h)!=ESP_OK)return;
    size_t size=sizeof(blob);esp_err_t e=nvs_get_blob(h,"alarms-v2",&blob,&size);
    if(e==ESP_ERR_NVS_NOT_FOUND){
        struct {uint32_t magic;thingsos_alarm_v1_t items[THINGSOS_ALARM_LIMIT];} old={0};
        size_t old_size=sizeof(old);
        e=nvs_get_blob(h,"alarms-v1",&old,&old_size);
        if(e==ESP_OK && old_size==sizeof(old) && old.magic==0x414c0001){
            for(unsigned i=0;i<THINGSOS_ALARM_LIMIT;++i)
                if(!thingsos_alarm_from_v1(old.items+i,blob.items+i)){e=ESP_FAIL;break;}
            if(e==ESP_OK){size=sizeof(blob);blob.magic=0x414c0002;}
        }else e=ESP_FAIL;
    }
    nvs_close(h);
    if(e!=ESP_OK || size!=sizeof(blob) || blob.magic!=0x414c0002)return;
    for(unsigned i=0;i<THINGSOS_ALARM_LIMIT;++i){
        if(!thingsos_alarm_valid(blob.items+i))return;
        if(blob.items[i].mode==THINGSOS_ALARM_ONCE && blob.items[i].last_day>=blob.items[i].start_day)
            blob.items[i].enabled=0;
    }
    memcpy(alarms,blob.items,sizeof(alarms));
}
static void alarm_days_text(unsigned days,char* text,size_t size){
    const char* preset=days==127?"每天":days==62?"工作日 · 周一至周五":days==65?"周末":NULL;
    if(preset){strlcpy(text,preset,size);return;}
    static const char* const names[]={"日","一","二","三","四","五","六"};
    strlcpy(text,"每周",size);
    for(unsigned i=1;i<=7;++i)if(days&(1u<<(i%7))){size_t used=strlen(text);if(used<size)snprintf(text+used,size-used,"%s",names[i%7]);}
}
static bool alarm_file(const thingsos_alarm_t* value,char* path,size_t size){
    char folder[600];app_storage_path(folder,sizeof(folder),"music");
    snprintf(path,size,"%s/%s",folder,value->music);
    struct stat info;return media() && !stat(path,&info) && S_ISREG(info.st_mode) && info.st_size>0;
}
static void alarm_redraw(void){
    app.audio_progress=false;app.ripple_pending=false;app.reader_background_refresh=false;
    app.feedback_refresh=true;app.dirty=true;
}
static void alarm_finish(bool snooze){
    if(!alarm_active())return;
    unsigned index=(unsigned)alarm_ringing;
    if(alarm_audio_owned)thingsos_audio_stop();
    thingsos_audio_volume(app.audio_volume);
    if(snooze)alarm_snooze[index]=now()+300000;
    else if(alarms[index].mode==THINGSOS_ALARM_ONCE){
        alarms[index].enabled=0;
        if(!alarm_store(alarms))say("单次闹钟关闭记录保存失败");
    }
    alarm_ringing=-1;alarm_audio_owned=false;
    app.activity_at=now();
    thingsos_runtime_overlay(&app.runtime,app.controls_open||app.menu||app.page!=THINGSOS_UI_READING||app.image_view.open,now());
    alarm_redraw();
}
static bool alarm_back(void){
    if(app.page!=THINGSOS_UI_ALARM)return false;
    if(alarm_date_field)alarm_date_field=0;
    else if(alarm_schedule_open)alarm_schedule_open=false;
    else if(alarm_picking)alarm_picking=false;
    else if(alarm_edit>=0)alarm_edit=-1;
    else navigate(THINGSOS_UI_APPS);
    app.dirty=true;return true;
}
static void alarm_begin_edit(unsigned i){
    alarm_edit=(int)i;alarm_draft=alarms[i];alarm_picking=alarm_schedule_open=false;alarm_date_field=0;
    if(!thingsos_alarm_used(&alarm_draft)){
        memset(&alarm_draft,0,sizeof(alarm_draft));alarm_draft.enabled=1;
        alarm_draft.hour=7;alarm_draft.days=THINGSOS_ALARM_DAILY;alarm_draft.volume=60;
        app_audio_scan(false);
        if(app_media.music_count)strlcpy(alarm_draft.music,app_media.music[0].name,sizeof(alarm_draft.music));
    }
    app.dirty=true;
}
static bool alarm_replace(unsigned i,const thingsos_alarm_t* value){
    thingsos_alarm_t saved=alarms[i];alarms[i]=*value;
    if(!alarm_store(alarms)){alarms[i]=saved;say("闹钟保存失败，请重试");return false;}
    alarm_pending[i]=alarm_snooze[i]=0;app.dirty=true;return true;
}
static uint32_t alarm_relative_day(int delta){
    time_t clock=wall_time();struct tm date;
    if(clock<1577836800||!localtime_r(&clock,&date)||date.tm_year<120||date.tm_year>199)return 0;
    date.tm_hour=12;date.tm_min=date.tm_sec=0;date.tm_mday+=delta;date.tm_isdst=-1;
    if(mktime(&date)==(time_t)-1)return 0;
    uint32_t value=(uint32_t)(date.tm_year+1900)*10000+(date.tm_mon+1)*100+date.tm_mday;
    return thingsos_date_valid(value)?value:0;
}
static void alarm_date_text(uint32_t day,const char* empty,char* text,size_t size){
    if(!day){strlcpy(text,empty,size);return;}
    snprintf(text,size,"%04u-%02u-%02u",(unsigned)(day/10000),(unsigned)(day/100%100),(unsigned)(day%100));
}
static void alarm_range_text(const thingsos_alarm_t* value,char* text,size_t size){
    char start[24],end[24];
    alarm_date_text(value->start_day,"不限开始",start,sizeof(start));
    alarm_date_text(value->end_day,"不限截止",end,sizeof(end));
    if(value->mode==THINGSOS_ALARM_ONCE)strlcpy(text,start,size);
    else if(!value->start_day&&!value->end_day)strlcpy(text,"长期有效",size);
    else snprintf(text,size,"%s 至 %s",start,end);
}
static bool alarm_once_future(const thingsos_alarm_t* value){
    time_t clock=wall_time();struct tm date;uint32_t today=alarm_relative_day(0);
    if(!today||!localtime_r(&clock,&date))return false;
    return value->start_day>today || (value->start_day==today &&
        value->hour*60+value->minute>date.tm_hour*60+date.tm_min);
}
static void alarm_set_once(int delta){
    uint32_t day=alarm_relative_day(delta);
    if(!day){say("请先在设置中校准日期和时间");return;}
    alarm_draft.mode=THINGSOS_ALARM_ONCE;alarm_draft.days=0;
    alarm_draft.start_day=alarm_draft.end_day=day;
}
static void alarm_switch_draw(int x,int y,bool on){
    /* E-ink counterpart of the supplied pill switch: position plus contrast,
       with a white circular thumb. No colour is needed to identify its state. */
    const int w=118,h=60,r=30;
    for(int row=0;row<h;++row){
        int dy=row-r,span=r;while(span*span+dy*dy>r*r)--span;
        int inset=r-span;thingsos_rect(&app.canvas,x+inset,y+row,w-2*inset,1,on?0:180);
        /* Fast UI thresholds gray to white; retain the off-state track outline. */
        if(!on){
            if(row<2||row>=h-2)thingsos_rect(&app.canvas,x+inset,y+row,w-2*inset,1,0);
            else{thingsos_rect(&app.canvas,x+inset,y+row,2,1,0);
                 thingsos_rect(&app.canvas,x+w-inset-2,y+row,2,1,0);}
        }
    }
    int cx=x+(on?w-30:30),cy=y+30;
    for(int dy=-23;dy<=23;++dy){int span=23;while(span*span+dy*dy>23*23)--span;
        thingsos_rect(&app.canvas,cx-span,cy+dy,2*span+1,1,255);}
    if(!on)app_circle(cx,cy,23,2);
}
static void alarm_date_adjust(unsigned field,int delta){
    uint32_t day=alarm_date_draft;
    if(!thingsos_date_valid(day))return;
    struct tm date={0};date.tm_year=(int)(day/10000)-1900;date.tm_mon=(int)(day/100%100)-1;
    date.tm_mday=(int)(day%100);date.tm_hour=12;date.tm_isdst=-1;
    if(field==2)date.tm_mday+=delta;
    else{
        if(field==0)date.tm_year+=delta;
        else{date.tm_mon+=delta;if(date.tm_mon<0){date.tm_mon=11;--date.tm_year;}
             if(date.tm_mon>11){date.tm_mon=0;++date.tm_year;}}
        if(date.tm_year<120||date.tm_year>199)return;
        while(date.tm_mday>1&&!thingsos_date_valid((uint32_t)(date.tm_year+1900)*10000+(date.tm_mon+1)*100+date.tm_mday))--date.tm_mday;
    }
    if(mktime(&date)==(time_t)-1)return;
    day=(uint32_t)(date.tm_year+1900)*10000+(date.tm_mon+1)*100+date.tm_mday;
    if(thingsos_date_valid(day))alarm_date_draft=day;
}
static void alarm_schedule_draw(void){
    if(alarm_date_field){
        static const char* const fields[]={"年","月","日"};
        unsigned values[]={alarm_date_draft/10000,alarm_date_draft/100%100,alarm_date_draft%100};
        for(unsigned i=0;i<3;++i){int x=36+(int)i*210;char value[16];
            centered_label(fields[i],x,278,26,192);reader_control("+",x,310,192,false);
            snprintf(value,sizeof(value),i==0?"%04u":"%02u",values[i]);centered_label(value,x,475,44,192);
            reader_control("-",x,510,192,false);}
        reader_control("今天",36,654,282,false);reader_control("明天",366,654,282,false);
        label("日期范围包含开始和截止当天。",36,820,23,612);
        reader_control("取消",36,1005,282,false);reader_control("确定",366,1005,282,true);
    }else{
        label("重复日期",36,235,26,612);
        static const char* const preset[]={"每天","工作日","周末"};const unsigned masks[]={127,62,65};
        for(unsigned i=0;i<3;++i)reader_control(preset[i],36+(int)i*210,260,192,alarm_draft.days==masks[i]);
        static const char* const days[]={"一","二","三","四","五","六","日"};
        for(unsigned i=0;i<7;++i)reader_control(days[i],36+(int)i*88,356,80,(alarm_draft.days&(1u<<((i+1)%7)))!=0);
        char value[32];label("开始日期",36,502,25,240);
        alarm_date_text(alarm_draft.start_day,"不限开始",value,sizeof(value));reader_control(value,300,456,348,false);
        label("截止日期",36,620,25,240);
        alarm_date_text(alarm_draft.end_day,"不限截止",value,sizeof(value));reader_control(value,300,574,348,false);
        reader_control("不限开始",36,732,282,alarm_draft.start_day==0);
        reader_control("不限截止",366,732,282,alarm_draft.end_day==0);
        label("起止当天均有效；日期范围内按星期提醒。",36,856,22,612);
        reader_control("完成",36,1005,612,true);
    }
}
static bool alarm_draw(void){
    if(app.page!=THINGSOS_UI_ALARM)return false;
    header(alarm_date_field?(alarm_date_field==1?"开始日期":"截止日期"):
           alarm_schedule_open?"重复与日期":alarm_picking?"选择铃声":alarm_edit>=0?"编辑闹钟":"闹钟");
    if(alarm_schedule_open)alarm_schedule_draw();
    else if(alarm_picking){
        label("来自音乐 · WAV / MP3",36,234,24,612);
        if(!app_media.music_count)label("请先将音乐放入 /sdcard/music",36,340,27,612);
        for(size_t row=0;row<7&&alarm_music_start+row<app_media.music_count;++row){
            int y=260+(int)row*96;const char* name=app_media.music[alarm_music_start+row].name;
            label(name,48,y+47,26,550);if(!strcmp(name,alarm_draft.music))label("✓",610,y+47,26,36);
            thingsos_rect(&app.canvas,36,y+95,612,1,0);
        }
        if(app_media.music_count>7){reader_control("上一页",36,1005,180,false);reader_control("下一页",468,1005,180,false);}
    }else if(alarm_edit>=0){
        reader_control("+",145,245,150,false);reader_control("+",389,245,150,false);
        char time[24];snprintf(time,sizeof(time),"%02u : %02u",alarm_draft.hour,alarm_draft.minute);
        centered_label(time,36,417,76,612);
        reader_control("-",145,450,150,false);reader_control("-",389,450,150,false);
        bool once=alarm_draft.mode==THINGSOS_ALARM_ONCE;
        reader_control("单次",36,560,282,once);reader_control("周期",366,560,282,!once);
        if(once){
            reader_control("今天",36,654,282,alarm_draft.start_day==alarm_relative_day(0));
            reader_control("明天",366,654,282,alarm_draft.start_day==alarm_relative_day(1));
        }else reader_control("重复与日期 >",36,654,612,false);
        char summary[96];alarm_range_text(&alarm_draft,summary,sizeof(summary));label(summary,36,742,18,612);
        char volume[32];snprintf(volume,sizeof(volume),"响铃音量 %u%%",alarm_draft.volume);label(volume,36,792,27,350);
        reader_control("-",420,748,100,false);reader_control("+",548,748,100,false);
        label("铃声 >",36,877,27,612);label(alarm_draft.music[0]?alarm_draft.music:"请从音乐中选择",36,925,25,612);
        reader_control(thingsos_alarm_used(alarms+alarm_edit)?"删除":"取消",36,1005,282,false);reader_control("保存",366,1005,282,true);
    }else{
        time_t clock=wall_time();struct tm date;
        if(clock<1577836800||!localtime_r(&clock,&date)||date.tm_year>199)
            label("请先在设置中校准日期和时间",36,219,22,612);
        unsigned row=0;
        for(unsigned i=0;i<THINGSOS_ALARM_LIMIT;++i)if(thingsos_alarm_used(alarms+i)){
            int y=240+(int)row++*158;char text[96];
            snprintf(text,sizeof(text),"%02u:%02u",alarms[i].hour,alarms[i].minute);label(text,48,y+55,46,400);
            if(alarms[i].mode==THINGSOS_ALARM_ONCE)strlcpy(text,"单次闹钟",sizeof(text));
            else alarm_days_text(alarms[i].days,text,sizeof(text));
            label(text,48,y+88,23,430);
            alarm_range_text(alarms+i,text,sizeof(text));label(text,48,y+115,18,470);
            label(alarms[i].music,48,y+142,18,470);
            alarm_switch_draw(530,y+30,alarms[i].enabled!=0);
            thingsos_rect(&app.canvas,36,y+157,612,1,0);
        }
        if(!row)centered_label("还没有闹钟",36,430,32,612);
        if(row<THINGSOS_ALARM_LIMIT)reader_control("+ 新增闹钟",36,915,612,true);
        label("息屏可提醒；关机不响铃。最多 4 个闹钟。",36,1038,22,612);
        label("工作日按周一至周五，不含节假日调休。",36,1072,22,612);
    }
    nav();return true;
}
static void alarm_schedule_tap(int x,int y){
    if(alarm_date_field){
        if((y>=310&&y<378)||(y>=510&&y<578)){
            unsigned column=(unsigned)(x-36)/210;
            if(column<3&&x<36+(int)column*210+192)alarm_date_adjust(column,y<378?1:-1);
        }else if(y>=654&&y<722){
            uint32_t day=alarm_relative_day(x>=366?1:0);
            if(day)alarm_date_draft=day;else say("请先在设置中校准日期和时间");
        }else if(y>=1005&&y<1073){
            if(x<318)alarm_date_field=0;
            else if(x>=366){
                uint32_t start=alarm_date_field==1?alarm_date_draft:alarm_draft.start_day;
                uint32_t end=alarm_date_field==2?alarm_date_draft:alarm_draft.end_day;
                if(start&&end&&start>end){say("开始日期不能晚于截止日期");return;}
                alarm_draft.start_day=start;alarm_draft.end_day=end;alarm_date_field=0;
            }
        }
    }else if(y>=260&&y<328){
        unsigned column=(unsigned)(x-36)/210;const unsigned masks[]={127,62,65};
        if(column<3&&x<36+(int)column*210+192)alarm_draft.days=(uint8_t)masks[column];
    }else if(y>=356&&y<424){
        unsigned column=(unsigned)(x-36)/88;
        if(column<7&&x<36+(int)column*88+80)alarm_draft.days^=(uint8_t)(1u<<((column+1)%7));
    }else if(x>=300&&((y>=456&&y<524)||(y>=574&&y<642))){
        alarm_date_field=y<524?1:2;
        alarm_date_draft=alarm_date_field==1?alarm_draft.start_day:alarm_draft.end_day;
        if(!alarm_date_draft)alarm_date_draft=alarm_relative_day(0);
        if(!alarm_date_draft)alarm_date_draft=20260101;
    }else if(y>=732&&y<800){
        if(x<318)alarm_draft.start_day=0;else if(x>=366)alarm_draft.end_day=0;
    }else if(y>=1005&&y<1073){
        if(!alarm_draft.days){say("请至少选择一个重复日期");return;}
        alarm_schedule_open=false;
    }
}
static bool alarm_tap(int x,int y){
    if(app.page!=THINGSOS_UI_ALARM)return false;
    if(x<90&&y>=74&&y<200)return alarm_back();
    if(y>=1098)return false;
    if(x<36||x>=648)return true;
    if(alarm_schedule_open){alarm_schedule_tap(x,y);app.dirty=true;return true;}
    if(alarm_picking){
        if(y>=260&&y<932){size_t at=alarm_music_start+(size_t)(y-260)/96;
            if(at<app_media.music_count){strlcpy(alarm_draft.music,app_media.music[at].name,sizeof(alarm_draft.music));alarm_picking=false;app.dirty=true;}}
        if(y>=1005&&y<1073){
            if(x<216)alarm_music_start=alarm_music_start>=7?alarm_music_start-7:0;
            else if(x>=468&&alarm_music_start+7<app_media.music_count)alarm_music_start+=7;
            app.dirty=true;
        }
        return true;
    }
    if(alarm_edit<0){
        unsigned row=0;
        for(unsigned i=0;i<THINGSOS_ALARM_LIMIT;++i)if(thingsos_alarm_used(alarms+i)){
            int top=240+(int)row++*158;if(y<top||y>=top+158)continue;
            if(x>=530&&y>=top+30&&y<top+98){thingsos_alarm_t next=alarms[i];next.enabled=!next.enabled;
                if(next.enabled&&((next.mode==THINGSOS_ALARM_ONCE&&!alarm_once_future(&next))||thingsos_alarm_expired(&next,wall_time())))
                    say("闹钟已过期，请编辑日期和时间");
                else alarm_replace(i,&next);}
            else if(x<530)alarm_begin_edit(i);
            return true;
        }
        if(y>=915&&y<983)for(unsigned i=0;i<THINGSOS_ALARM_LIMIT;++i)if(!thingsos_alarm_used(alarms+i)){alarm_begin_edit(i);break;}
        return true;
    }
    if((y>=245&&y<313)||(y>=450&&y<518)){
        int step=y<313?1:-1;
        if(x>=145&&x<295)alarm_draft.hour=(uint8_t)((alarm_draft.hour+24+step)%24);
        if(x>=389&&x<539)alarm_draft.minute=(uint8_t)((alarm_draft.minute+60+step)%60);
    }else if(y>=560&&y<628){
        if(x<318&&alarm_draft.mode!=THINGSOS_ALARM_ONCE)alarm_set_once(0);
        else if(x>=366&&alarm_draft.mode!=THINGSOS_ALARM_REPEAT){
            alarm_draft.mode=THINGSOS_ALARM_REPEAT;alarm_draft.days=THINGSOS_ALARM_DAILY;
            alarm_draft.start_day=alarm_relative_day(0);alarm_draft.end_day=0;
        }
    }else if(y>=654&&y<722){
        if(alarm_draft.mode==THINGSOS_ALARM_ONCE){if(x<318)alarm_set_once(0);else if(x>=366)alarm_set_once(1);}
        else alarm_schedule_open=true;
    }else if(y>=748&&y<816){
        if(x>=420&&x<520)alarm_draft.volume=alarm_draft.volume>10?alarm_draft.volume-5:10;
        if(x>=548)alarm_draft.volume=alarm_draft.volume<100?alarm_draft.volume+5:100;
    }else if(y>=833&&y<965){app_audio_scan(false);alarm_music_start=0;alarm_picking=true;
    }else if(y>=1005&&y<1073){
        if(x<318){thingsos_alarm_t empty={0};if(!thingsos_alarm_used(alarms+alarm_edit)||alarm_replace((unsigned)alarm_edit,&empty))alarm_edit=-1;}
        else if(x>=366){
            if(alarm_draft.mode==THINGSOS_ALARM_REPEAT&&!alarm_draft.days){say("请至少选择一个重复日期");return true;}
            if(alarm_draft.start_day&&alarm_draft.end_day&&alarm_draft.start_day>alarm_draft.end_day){say("开始日期不能晚于截止日期");return true;}
            if(alarm_draft.mode==THINGSOS_ALARM_ONCE&&!alarm_once_future(&alarm_draft)){say("请选择今天或明天尚未到达的时间");return true;}
            if(alarm_draft.mode==THINGSOS_ALARM_REPEAT&&alarm_draft.end_day&&alarm_relative_day(0)>alarm_draft.end_day){say("日期范围已结束，请重新设置");return true;}
            char path[800];if(!thingsos_alarm_valid(&alarm_draft)||!alarm_file(&alarm_draft,path,sizeof(path))){say("请选择 SD 卡音乐中现有的铃声");return true;}
            /* An edit starts a new schedule; do not immediately replay an elapsed time today. */
            time_t clock=wall_time();struct tm date;
            alarm_draft.last_day=0;
            if(alarm_draft.mode==THINGSOS_ALARM_REPEAT&&clock>=1577836800&&localtime_r(&clock,&date)&&date.tm_hour*60+date.tm_min>=alarm_draft.hour*60+alarm_draft.minute)
                alarm_draft.last_day=(uint32_t)(date.tm_year+1900)*10000+(date.tm_mon+1)*100+date.tm_mday;
            if(alarm_replace((unsigned)alarm_edit,&alarm_draft))alarm_edit=-1;
        }
    }
    app.dirty=true;return true;
}
static bool alarm_ring_tap(int x,int y){
    if(!alarm_active())return false;
    int left=(app.canvas.width-600)/2,top=(app.canvas.height-450)/2;
    if(y>=top+332&&y<top+400){
        if(x>=left+24&&x<left+294)alarm_finish(false);
        else if(x>=left+314&&x<left+576)alarm_finish(true);
    }
    return true;
}
static void alarm_overlay(void){
    if(!alarm_active())return;
    int left=(app.canvas.width-600)/2,top=(app.canvas.height-450)/2;
    thingsos_rect(&app.canvas,left,top,600,450,0);thingsos_rect(&app.canvas,left+3,top+3,594,444,255);
    centered_label("闹钟",left+24,top+70,32,552);
    char time[24];thingsos_alarm_t* a=alarms+alarm_ringing;
    snprintf(time,sizeof(time),"%02u:%02u",a->hour,a->minute);centered_label(time,left+24,top+172,70,552);
    centered_label(a->music,left+24,top+235,25,552);
    centered_label(alarm_message[0]?alarm_message:"到时间了 · 3 分钟后自动停止",left+24,top+290,22,552);
    reader_control("停止",left+24,top+332,270,true);reader_control("稍后 5 分钟",left+314,top+332,262,false);
}
static void alarm_poll(void){
    time_t clock=wall_time();
    for(unsigned i=0;i<THINGSOS_ALARM_LIMIT;++i){
        uint32_t day=thingsos_alarm_due(alarms+i,alarm_checked,clock);
        if(day){
            alarms[i].last_day=day;
            if(!alarm_store(alarms))say("闹钟触发记录保存失败，重启可能重复提醒");
            alarm_pending[i]=now()+300000;
        }
        if(alarm_snooze[i]&&now()>=alarm_snooze[i]){alarm_snooze[i]=0;alarm_pending[i]=now()+300000;}
        if(alarms[i].enabled&&!alarm_pending[i]&&!alarm_snooze[i]&&alarm_ringing!=(int)i&&thingsos_alarm_expired(alarms+i,clock)){
            alarms[i].enabled=0;
            if(!alarm_store(alarms))say("过期闹钟关闭记录保存失败");
            if(app.page==THINGSOS_UI_ALARM)app.dirty=true;
        }
    }
    alarm_checked=clock;
    if(alarm_active()&&now()>=alarm_until)alarm_finish(false);
    if(!alarm_active())for(unsigned i=0;i<THINGSOS_ALARM_LIMIT;++i){
        if(!alarm_pending[i])continue;
        if(now()>alarm_pending[i]||!alarms[i].enabled){alarm_pending[i]=0;continue;}
        /* USB/OTA storage ownership must complete before waking or decoding. */
        if(app.wd_power_action||app.page==THINGSOS_UI_USB||transfer_blocks_ui())continue;
        if(app.asleep)power_action(1);
        if(app.page==THINGSOS_UI_LOCK)unlock_screen();
        alarm_ringing=(int)i;alarm_pending[i]=0;alarm_until=now()+180000;alarm_retry=0;
        alarm_audio_owned=false;alarm_message[0]=0;
        app.controls_open=false;app_media.playlist=app_media.preview=false;
        thingsos_tilt_reset(&app.tilt);thingsos_board_tilt_active(false);
        thingsos_runtime_overlay(&app.runtime,true,now());alarm_redraw();break;
    }
    if(!alarm_active()||now()<alarm_retry)return;
    alarm_retry=now()+3000;
    thingsos_audio_status_t audio;thingsos_audio_status(&audio);
    if(alarm_audio_owned&&(audio.state==THINGSOS_AUDIO_PLAYING||audio.state==THINGSOS_AUDIO_STARTING))return;
    const char* message=NULL;char path[800];
    if(audio.state==THINGSOS_AUDIO_RECORDING||audio.state==THINGSOS_AUDIO_RECORD_PAUSED||app.record_start_pending||app.record_save_pending)
        message="录音中，保存完成后播放铃声";
    else if(audio.state==THINGSOS_AUDIO_STOPPING||audio.state==THINGSOS_AUDIO_STARTING)message="正在等待音频准备或保存";
    else if(!alarm_file(alarms+alarm_ringing,path,sizeof(path)))message="铃声文件不可用，请检查 SD 卡";
    else if(thingsos_ima_active()||thingsos_weidu_busy()||(!thingsos_audio_active()&&thingsos_network_busy()))message="传输处理中，完成后播放铃声";
    else {
        thingsos_audio_volume(alarms[alarm_ringing].volume);
        if(thingsos_audio_play(path,0)){alarm_audio_owned=true;message="到时间了 · 3 分钟后自动停止";}
        else message="音频输出繁忙，正在重试";
    }
    if(message&&strcmp(alarm_message,message)){strlcpy(alarm_message,message,sizeof(alarm_message));alarm_redraw();}
}
