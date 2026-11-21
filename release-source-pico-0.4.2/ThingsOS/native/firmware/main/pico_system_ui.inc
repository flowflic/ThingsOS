/* SPDX-License-Identifier: Apache-2.0 */
/* Two-tab system layout from E-Ink4 Plus, populated by the Pico board. */
typedef struct { uint32_t total,free,minimum,largest; } pico_system_heap_t;
static bool pico_system_details;
#ifndef THINGSOS_HOST_UI
static uint64_t pico_system_sample_at;

static void pico_system_poll(bool idle) {
    if(app.page!=THINGSOS_UI_SYSTEM || app.pin.required) {
        pico_system_sample_at=0;
        pico_system_details=false;
        return;
    }
    if(idle && !app.controls_open && !app.dirty && !app.ui_action_at && now()>=pico_system_sample_at)
        background_redraw();
}

static pico_system_heap_t pico_system_heap(uint32_t caps) {
    multi_heap_info_t h;
    heap_caps_get_info(&h,caps);
    return (pico_system_heap_t){heap_caps_get_total_size(caps),h.total_free_bytes,
        h.minimum_free_bytes,h.largest_free_block};
}

static bool pico_flash_usage(uint32_t* total,uint32_t* used,uint32_t* headroom) {
    static uint32_t chip,occupied,remaining;
    static bool ready;
    if(!ready) {
        const esp_partition_t* running=esp_ota_get_running_partition();
        if(!running || esp_flash_get_size(NULL,&chip)!=ESP_OK || running->size>chip)return false;
        esp_partition_pos_t pos={.offset=running->address,.size=running->size};
        esp_image_metadata_t image;
        if(esp_image_get_metadata(&pos,&image)!=ESP_OK || !image.image_len ||
           image.image_len>running->size)return false;
        remaining=running->size-image.image_len;
        occupied=chip-remaining;
        ready=true;
    }
    *total=chip;*used=occupied;*headroom=remaining;
    return true;
}
#endif

static void pico_system_right(const char* text,int y,unsigned size) {
    thingsos_font_size(ui_font(),size);
    thingsos_label_right(ui_font(),&app.canvas,text,36,y,612);
}
static void pico_system_meter(const char* name,const char* value,uint32_t used,uint32_t total,
                              const char* left,const char* right,int y) {
    label(name,36,y,28,410);
    pico_system_right(value,y,30);
    int fill=total?(int)((uint64_t)(used>total?total:used)*608/total):0;
    thingsos_rect(&app.canvas,36,y+27,612,28,0);
    thingsos_rect(&app.canvas,38,y+29,608,24,232);
    if(fill)thingsos_rect(&app.canvas,38,y+29,fill,24,0);
    label(left,36,y+82,22,324);
    pico_system_right(right,y+82,22);
}
static void pico_system_memory(const char* name,pico_system_heap_t h,int y) {
    char value[24],left[64],right[64];
    uint32_t used=h.total>=h.free?h.total-h.free:0;
    if(h.total)snprintf(value,sizeof(value),"%u%%",(unsigned)((uint64_t)used*100/h.total));
    else strlcpy(value,"—",sizeof(value));
    snprintf(left,sizeof(left),"已用 %u / %u KiB",(unsigned)(used/1024),(unsigned)(h.total/1024));
    snprintf(right,sizeof(right),"可用 %u · 最低 %u KiB",(unsigned)(h.free/1024),(unsigned)(h.minimum/1024));
    pico_system_meter(name,value,used,h.total,left,right,y);
}
static void pico_system_largest(const char* name,pico_system_heap_t h,int y) {
    char value[40],left[40],right[40];
    snprintf(value,sizeof(value),"%u KiB",(unsigned)(h.largest/1024));
    snprintf(left,sizeof(left),"总空闲 %u KiB",(unsigned)(h.free/1024));
    snprintf(right,sizeof(right),"最低空闲 %u KiB",(unsigned)(h.minimum/1024));
    pico_system_meter(name,value,h.largest,h.free,left,right,y);
}

static bool pico_system_draw(void) {
    if(app.page!=THINGSOS_UI_SYSTEM)return false;
    app.font.ui_size_percent=100;app.font.ui_weight_override=app.ui_weight+1;
    label("< 返回",36,155,26,160);
    centered_label("系统",180,160,34,324);
    pico_system_right("刷新",155,26);
    thingsos_rect(&app.canvas,36,210,612,1,170);
    centered_label("资源概览",36,252,25,292);
    centered_label("更多指标",356,252,25,292);
    thingsos_rect(&app.canvas,pico_system_details?356:36,271,292,3,0);
    label("实时监控 · 每 2 秒更新",36,296,21,390);

    char value[40],left[64],right[64];
    pico_system_heap_t sram={0},psram={0},dma={0};
    uint32_t chip=0,used=0,remaining=0,refresh=0,underruns=0,last=0,cooldown_s=0;
    unsigned mhz=0,limit=0,pclk=0,recoveries=0;
    bool flash=false,pclk_latched=false;
#ifndef THINGSOS_HOST_UI
    pico_system_sample_at=now()+2000;
    sram=pico_system_heap(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    psram=pico_system_heap(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    dma=pico_system_heap(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
    flash=pico_flash_usage(&chip,&used,&remaining);
    mhz=(unsigned)(esp_clk_cpu_freq()/1000000);limit=CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    pclk=thingsos_board_panel_clock();refresh=thingsos_board_last_refresh_ms();
    pclk_latched=thingsos_board_panel_clock_latched();
    recoveries=thingsos_board_panel_clock_recoveries();
    cooldown_s=thingsos_board_panel_clock_cooldown_s();
    underruns=thingsos_board_scan_underruns();last=thingsos_board_last_underrun_seconds();
#endif
    if(pico_system_details) {
        snprintf(value,sizeof(value),"%u MHz",pclk);
        if(pclk<18) {
            if(pclk_latched)strlcpy(left,"已锁定 12 MHz（重启恢复）",sizeof(left));
            else if(cooldown_s)snprintf(left,sizeof(left),"冷却中 · %u 秒后可恢复",(unsigned)cooldown_s);
            else strlcpy(left,"等待稳定刷新后自动恢复",sizeof(left));
        } else if(recoveries)snprintf(left,sizeof(left),"当前 PCLK · 已恢复 %u 次",recoveries);
        else strlcpy(left,"当前 PCLK",sizeof(left));
        pico_system_meter("屏幕扫描时钟",value,pclk,18,left,"小纸标称 18 MHz",336);
        pico_system_largest("SRAM 最大连续空闲",sram,473);
        pico_system_largest("PSRAM 最大连续空闲",psram,610);
        snprintf(value,sizeof(value),"%u ms",(unsigned)refresh);
        uint32_t range=refresh/1000*1000+1000;
        snprintf(right,sizeof(right),"量程 %u ms",(unsigned)range);
        pico_system_meter("最近刷新耗时",value,refresh,range,"上次成功呈现",right,747);
        snprintf(value,sizeof(value),"%u 次",(unsigned)underruns);
        label("开机后欠载次数",36,884,28,410);
        pico_system_right(value,884,30);
        if(underruns)snprintf(left,sizeof(left),"最近在开机后 %u 秒",(unsigned)last);
        else strlcpy(left,"本次开机未记录到扫描欠载",sizeof(left));
        label(left,36,925,22,612);
    } else {
        snprintf(value,sizeof(value),"%u MHz",mhz);
        snprintf(right,sizeof(right),"配置上限 %u MHz",limit);
        pico_system_meter("CPU 频率",value,mhz,limit,"ESP32-S3",right,328);
        pico_system_memory("SRAM",sram,452);
        pico_system_memory("PSRAM",psram,576);
        pico_system_memory("内部 DMA 内存",dma,700);
        if(flash) {
            snprintf(value,sizeof(value),"%u%%",(unsigned)((uint64_t)used*100/chip));
            snprintf(left,sizeof(left),"占用 %u / %u KiB",(unsigned)(used/1024),(unsigned)(chip/1024));
            snprintf(right,sizeof(right),"升级余量 %u KiB",(unsigned)(remaining/1024));
        } else {
            strlcpy(value,"—",sizeof(value));strlcpy(left,"Flash 不可读取",sizeof(left));right[0]=0;
        }
        pico_system_meter("Flash",value,used,chip,left,right,824);
    }
    reader_control("诊断日志",356,940,292,false);
    nav();
    return true;
}
static bool pico_system_tap(int x,int y) {
    if(app.page!=THINGSOS_UI_SYSTEM)return false;
    if(y>=220 && y<286 && x>=36 && x<648) {
        pico_system_details=x>=342;
        app.dirty=true;
    } else if(y>=74 && y<200 && x>=528 && x<648)app.dirty=true;
    else if(y>=940 && y<1008 && x>=356 && x<648)navigate(THINGSOS_UI_LOGS);
    return true;
}
