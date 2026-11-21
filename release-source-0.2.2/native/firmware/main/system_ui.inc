/* SPDX-License-Identifier: Apache-2.0
 * User-approved 684x1216 System UE. Main task owns sampling and presentation.
 */
static struct {
    thingsos_system_snapshot_t state;
    uint64_t next;
    uint32_t wall;
    bool visible,painted,regional,details;
} system_ui;
static void app_system_take(void) {
    thingsos_system_read(&system_ui.state);
    system_ui.wall=0;thingsos_board_time(&system_ui.wall);
    system_ui.next=now()+2000;
}
static void app_system_begin(void) {
    memset(&system_ui,0,sizeof(system_ui));system_ui.visible=true;
    thingsos_system_reset();app_system_take();
}
static void app_system_leave(void) {
    system_ui.visible=system_ui.painted=system_ui.regional=false;
}
static void app_system_poll(void) {
    if(app.page!=THINGSOS_UI_SYSTEM||app.asleep||app.pin.required){app_system_leave();return;}
    if(!system_ui.visible){app_system_begin();background_redraw();return;}
    if(now()<system_ui.next||app.dirty||app.controls_open||app_media.preview||
       thingsos_board_touch_contact_pending())return;
    app_system_take();system_ui.regional=system_ui.painted;background_redraw();
}
static bool app_system_region(void) {
    return app.page==THINGSOS_UI_SYSTEM&&system_ui.regional&&system_ui.painted&&
        !app.controls_open&&!app.controls_refresh&&!app.feedback_refresh&&
        !app.force_refresh&&!app.transition_refresh&&!app.lock_wave_pending&&!app.message[0];
}
static void app_system_presented(bool ok) {
    if(app.page==THINGSOS_UI_SYSTEM&&!app.controls_open)system_ui.painted=ok;
    system_ui.regional=false;
}
static esp_err_t app_system_present(void) {
    return thingsos_board_present_metrics(&app.canvas);
}
static void system_right(const char* text,int y,unsigned px) {
    thingsos_font_size(ui_font(),px);thingsos_label_right(ui_font(),&app.canvas,text,36,y,612);
}
static void system_units(char* out,size_t bytes,uint64_t units,bool internal) {
    if(internal)snprintf(out,bytes,"%llu KiB",(unsigned long long)units);
    else snprintf(out,bytes,"%llu.%02u MiB",(unsigned long long)(units/100),(unsigned)(units%100));
}
static uint64_t system_scaled(uint64_t bytes,bool internal) {
    uint64_t base=internal?1024:1048576,mul=internal?1:100;
    return bytes/base*mul+bytes%base*mul/base;
}
static void system_bar(unsigned y,uint64_t used,uint64_t total) {
    thingsos_rect(&app.canvas,36,(int)y,612,28,0);
    thingsos_rect(&app.canvas,38,(int)y+2,608,24,232);
    unsigned filled=thingsos_system_ratio(used,total,608);
    if(filled)thingsos_rect(&app.canvas,38,(int)y+2,(int)filled,24,0);
}
static void system_memory(const char* title,const thingsos_system_memory_t* m,unsigned row,bool internal,bool flash) {
    int y=336+(int)row*148;char text[96],size[48];
    label(title,36,y,30,300);
    bool valid=m->valid&&m->total&&m->free<=m->total;
    if(!valid) {
        system_right("--",y,32);system_bar((unsigned)y+27,0,0);
        label("状态不可读取",36,y+82,22,360);return;
    }
    uint64_t total=system_scaled(m->total,internal),free=system_scaled(m->free,internal),used=total-free;
    system_units(size,sizeof(size),total,internal);
    unsigned scale=app.font.ui_size_percent?app.font.ui_size_percent:100;
    int capacity=36+(int)((!strcmp(title,"SRAM")?118:!strcmp(title,"PSRAM")?150:98)*scale/100);
    label(size,capacity,y,22,300);
    snprintf(text,sizeof(text),"%u%%",thingsos_system_ratio(m->total-m->free,m->total,100));
    system_right(text,y,32);system_bar((unsigned)y+27,m->total-m->free,m->total);
    system_units(size,sizeof(size),used,internal);snprintf(text,sizeof(text),"%s %s",flash?"已占用":"已用",size);
    label(text,36,y+82,22,310);
    system_units(size,sizeof(size),free,internal);snprintf(text,sizeof(text),"%s %s",flash?"余量":"可用",size);
    system_right(text,y+82,22);

}
static void system_extra(const char* title,unsigned row,const char* value,uint64_t used,uint64_t total,
                         const char* left,const char* right) {
    int y=336+(int)row*148;
    label(title,36,y,28,445);system_right(value,y,30);system_bar((unsigned)y+27,used,total);
    label(left,36,y+82,22,330);system_right(right,y+82,22);
}
static void system_largest(const char* title,const thingsos_system_memory_t* m,unsigned row,bool internal) {
    char value[48],left[80],right[48],units[48];
    bool valid=m->valid&&m->largest<=m->free;
    if(valid) {
        system_units(value,sizeof(value),system_scaled(m->largest,internal),internal);
        system_units(units,sizeof(units),system_scaled(m->free,internal),internal);
        snprintf(left,sizeof(left),"总空闲 %s",units);
        snprintf(right,sizeof(right),"占空闲 %u%%",thingsos_system_ratio(m->largest,m->free,100));
    } else {strlcpy(value,"--",sizeof(value));strlcpy(left,"状态不可读取",sizeof(left));right[0]=0;}
    system_extra(title,row,value,valid?m->largest:0,valid?m->free:0,left,right);
}
static bool app_system_draw(void) {
    if(app.page!=THINGSOS_UI_SYSTEM)return false;
    const thingsos_system_snapshot_t* s=&system_ui.state;char text[96];
    label("< 返回",36,155,26,160);centered_label("系统",180,160,34,324);
    thingsos_font_size(ui_font(),26);thingsos_label_right(ui_font(),&app.canvas,"刷新",548,155,100);
    thingsos_rect(&app.canvas,36,210,612,1,170);
    centered_label("资源概览",36,252,25,292);centered_label("更多指标",356,252,25,292);
    thingsos_rect(&app.canvas,system_ui.details?356:36,271,292,3,0);
    label(s->cpu_error?"实时监控 · CPU 采样异常":s->cpu_valid?"实时监控 · 每 2 秒更新":"实时监控 · 正在采样",36,296,21,390);
    if(system_ui.wall){time_t stamp=system_ui.wall;struct tm current;localtime_r(&stamp,&current);
        snprintf(text,sizeof(text),"%02d:%02d:%02d 更新",current.tm_hour,current.tm_min,current.tm_sec);}
    else strlcpy(text,"已更新",sizeof(text));
    system_right(text,296,21);
    if(system_ui.details) {
        char value[48],right[64];
        if(s->display.pclk_mhz)snprintf(value,sizeof(value),"%u MHz",(unsigned)s->display.pclk_mhz);
        else strlcpy(value,"--",sizeof(value));
        system_extra("屏幕扫描时钟",0,value,s->display.pclk_mhz,20,"当前 PCLK 配置","参考上限 20 MHz");
        system_largest("SRAM 最大连续空闲",&s->sram,1,true);
        system_largest("PSRAM 最大连续空闲",&s->psram,2,false);
        uint64_t scale=((uint64_t)s->display.last_refresh_ms/1000+1)*1000;
        if(s->display.last_refresh_ms&&s->display.last_refresh_ms%1000==0)scale=s->display.last_refresh_ms;
        if(s->display.refresh_valid)snprintf(value,sizeof(value),"%u ms",(unsigned)s->display.last_refresh_ms);
        else strlcpy(value,"--",sizeof(value));
        snprintf(right,sizeof(right),"量程 %llu ms",(unsigned long long)scale);
        system_extra("最近刷新耗时",3,value,s->display.refresh_valid?s->display.last_refresh_ms:0,scale,
            "上次成功呈现",right);
        label("开机后欠载次数",36,928,28,445);
        snprintf(value,sizeof(value),"%u 次",(unsigned)s->display.underrun_count);system_right(value,928,30);
        if(s->display.underrun_count) {
            snprintf(text,sizeof(text),"最近：开机后 %llu 秒 · %s",(unsigned long long)s->display.underrun_last_seconds,
                s->display.underrun_during_clear?"清屏":"刷新");
            label(text,36,970,22,612);
            label(s->display.last_present_failed?"最近呈现失败，等待恢复":"最近呈现成功，请结合条纹现象判断",36,1010,21,612);
        } else label("本次开机未记录到扫描欠载",36,982,22,612);
        label("最大连续块相对总空闲 · 耗时量程至少 1 秒",36,1054,20,612);
    } else {
        label("CPU 频率",36,336,30,330);
        if(s->cpu_mhz)snprintf(text,sizeof(text),"%u MHz",(unsigned)s->cpu_mhz);else strlcpy(text,"--",sizeof(text));
        system_right(text,336,32);system_bar(363,s->cpu_mhz,s->cpu_max_mhz);
        label("0 MHz",36,418,22,230);snprintf(text,sizeof(text),"频率上限 %u MHz",(unsigned)s->cpu_max_mhz);system_right(text,418,22);
        label("CPU 使用率",36,484,30,360);
        unsigned load=(s->cpu_used_bp+50)/100;if(load>100)load=100;
        if(s->cpu_valid)snprintf(text,sizeof(text),"%u%%",load);else strlcpy(text,s->cpu_error?"不可用":"采样中",sizeof(text));
        system_right(text,484,s->cpu_valid?32:26);system_bar(511,s->cpu_valid?s->cpu_used_bp:0,10000);
        label("双核平均",36,566,22,300);
        if(s->cpu_valid)snprintf(text,sizeof(text),"空闲 %u%%",100-load);else strlcpy(text,"空闲 --",sizeof(text));system_right(text,566,22);
        system_memory("SRAM",&s->sram,2,true,false);system_memory("PSRAM",&s->psram,3,false,false);system_memory("Flash",&s->flash,4,false,true);
        label("内存为可分配堆 · Flash 不含 SD 卡",36,1054,21,612);
    }
    nav();return true;
}
static bool app_system_tap(int x,int y) {
    if(app.page!=THINGSOS_UI_SYSTEM)return false;
    if(y>=220&&y<286) {
        system_ui.details=x>=342;system_ui.regional=false;app.dirty=true;return true;
    }
    if(y>=74&&y<200) {
        if(x<190)app_back();
        else if(x>=528){app_system_take();system_ui.regional=system_ui.painted;app.dirty=true;}
        return true;
    }
    return y<1098;
}
