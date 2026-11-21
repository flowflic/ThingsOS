// SPDX-License-Identifier: Apache-2.0
/* Host harness replaces hardware only; draws and taps execute the real controller. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <direct.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "thingsos/graphics.h"
#include "thingsos/tilt.h"
#include "thingsos/network_state.h"
#include "thingsos/release.h"
#include "thingsos/network_defaults.h"
#define WEIDU_DEVICE_API static
#include "../firmware/components/thingsos_weidu/weidu_device.h"
#undef WEIDU_DEVICE_API
static thingsos_weidu_status_t host_weidu;
static thingsos_weidu_action_t host_weidu_action;
static unsigned host_weidu_calls;
static char host_weidu_epub[512];
static size_t host_weidu_shelf_total=5;
static bool host_weidu_search_blocked;
static bool host_online_search_async,host_online_search_ready,host_online_added;
static char host_online_root[512]="/sdcard/.thingsos/weidu";
#define ONLINE_ROOT host_online_root
static unsigned host_weidu_chapter,host_weidu_chapters;
static bool host_weidu_prepare_cover;
static unsigned host_cover_serial;
static unsigned thingsos_weidu_device_cover_serial(void){return host_cover_serial;}

static unsigned host_weidu_search_calls;
static unsigned host_library_order_reads;
static bool thingsos_weidu_device_init(void) {return true;}
static void thingsos_weidu_state(thingsos_weidu_status_t* s) {*s=host_weidu;}
static bool thingsos_weidu_busy(void) {return host_weidu.busy;}
static bool thingsos_weidu_start(thingsos_weidu_action_t a,const thingsos_weidu_book_t* b) {
    (void)b;host_weidu_action=a;++host_weidu_calls;host_weidu.busy=true;++host_weidu.serial;return true;
}
static void thingsos_weidu_cancel(void) {host_weidu.busy=false;host_weidu.phase=WD_CANCELLED;++host_weidu.serial;}
static bool thingsos_weidu_device_shelf(size_t start,thingsos_weidu_book_t* books,size_t cap,size_t* n,size_t* total) {
    *total=host_weidu.authenticated?host_weidu_shelf_total:0;*n=start<*total?*total-start:0;if(*n>cap) *n=cap;
    for(size_t i=0;i<*n;++i) {memset(&books[i],0,sizeof(books[i]));snprintf(books[i].id,sizeof(books[i].id),"%u",(unsigned)(i+start+1));
        snprintf(books[i].title,sizeof(books[i].title),"微读测试书 %u",(unsigned)(i+start+1));strcpy(books[i].author,"测试作者");}
    return true;
}
static bool thingsos_weidu_device_search(size_t start,thingsos_weidu_book_t* books,size_t cap,size_t* n,size_t* total,const char* query) {
    ++host_weidu_search_calls;
    if(host_weidu_search_blocked){*n=*total=0;return false;}
    *n=*total=0;
    size_t available=host_weidu.authenticated?host_weidu_shelf_total:0;
    for(size_t i=0;i<available;i++) {
        thingsos_weidu_book_t book;size_t one,all;
        thingsos_weidu_device_shelf(i,&book,1,&one,&all);
        if(query && query[0] && !strstr(book.title,query))continue;
        if((*total)++>=start && *n<cap)books[(*n)++]=book;
    }
    if(query && !strcmp(query,"线上书籍") && host_online_search_ready) {
        if((*total)++>=start && *n<cap){thingsos_weidu_book_t* b=&books[(*n)++];memset(b,0,sizeof(*b));strcpy(b->id,"99");strcpy(b->title,"线上书籍");}
    }
    return true;
}
static bool thingsos_weidu_device_search_online(const char* query) {
    (void)query;if(!host_online_search_async)return true;
    host_online_search_ready=false;host_weidu.background=true;host_weidu.phase=WD_SHELF;
    return thingsos_weidu_start(WD_DEVICE_SEARCH,NULL);
}
static bool thingsos_weidu_device_membership(const thingsos_weidu_book_t* books,size_t count,bool* members) {
    for(size_t i=0;i<count;++i){unsigned id=(unsigned)strtoul(books[i].id,NULL,10);members[i]=(id && id<=host_weidu_shelf_total) || (id==99 && host_online_added);}
    return true;
}
static bool thingsos_weidu_device_cached(const char* id,char* out,size_t cap) {
    (void)id;if(!host_weidu_epub[0] || strlen(host_weidu_epub)>=cap) return false;strcpy(out,host_weidu_epub);return true;
}
static bool host_mp_available;
static char host_mp_article[512];
static bool thingsos_mp_device_request(thingsos_weidu_action_t action,const thingsos_weidu_book_t* account,const thingsos_weidu_book_t* article) {
    return thingsos_weidu_start(action,article?article:account);
}
static bool thingsos_mp_device_page(const char* account,size_t start,thingsos_weidu_book_t* out,size_t cap,size_t* count,size_t* total) {
    *count=0;*total=host_mp_available?(account?10:7):0;
    for(size_t i=start;i<*total && *count<cap;++i){thingsos_weidu_book_t* b=&out[(*count)++];memset(b,0,sizeof(*b));
        snprintf(b->id,sizeof(b->id),account?"MP_WXS_123_article%u":"MP_WXS_%u",(unsigned)i+1);
        snprintf(b->title,sizeof(b->title),account?"公众号文章 %u":"已关注公众号 %u",(unsigned)i+1);b->updated=1792300000;}
    return host_mp_available;
}
static bool thingsos_mp_device_cached(const thingsos_weidu_book_t* account,const thingsos_weidu_book_t* article,char* out,size_t cap) {
    (void)account;(void)article;if(!host_mp_article[0] || strlen(host_mp_article)>=cap)return false;strcpy(out,host_mp_article);return true;
}
typedef struct {int unused;} DIR;
struct dirent {char d_name[256];};
static DIR* opendir(const char* p) {(void)p;return NULL;}
static struct dirent* readdir(DIR* d) {(void)d;return NULL;}
static void closedir(DIR* d) {(void)d;}
#define strcasecmp _stricmp
#define S_ISREG(m) (((m)&_S_IFMT)==_S_IFREG)
#define mkdir(p,m) _mkdir(p)
static size_t strlcpy(char* dst,const char* src,size_t n) {size_t len=strlen(src);if(n) {size_t used=len<n-1?len:n-1;memcpy(dst,src,used);dst[used]=0;}return len;}
struct timeval {time_t tv_sec;int tv_usec;};
static int settimeofday(const struct timeval* v,void* p) {(void)v;(void)p;return 0;}
static struct tm* localtime_r(const time_t* t,struct tm* out) {return localtime_s(out,t)?NULL:out;}
typedef int esp_err_t;
typedef struct {char version[32];} esp_app_desc_t;
static const esp_app_desc_t* esp_app_get_description(void){static esp_app_desc_t a={"0.3.2"};return &a;}
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_OTA_IMG_VALID 2
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdMS_TO_TICKS(t) (t)
static void vTaskDelay(unsigned ms) {(void)ms;}
static int64_t host_clock_us=1000000;
static int64_t esp_timer_get_time(void) {return host_clock_us;}
static uint32_t esp_random(void) {return 12345;}
static void esp_restart(void) {}
typedef struct {const char* label;uint32_t address;} esp_partition_t;
typedef int esp_ota_img_states_t;
static const esp_partition_t* esp_ota_get_running_partition(void) {static esp_partition_t p={"ota_0",0x220000};return &p;}
static const esp_partition_t* esp_ota_get_last_invalid_partition(void) {return NULL;}
static const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t* p) {(void)p;static esp_partition_t t={"ota_1",0x620000};return &t;}
static int esp_ota_get_state_partition(const esp_partition_t* p,int* s) {(void)p;*s=2;return 0;}
static int esp_ota_set_boot_partition(const esp_partition_t* p) {(void)p;return 0;}
typedef struct {bool mounted;} read_pico_sd_info_t;
static bool host_sd;
static char host_picture_root[512];
static int read_pico_sd_get_info(read_pico_sd_info_t* i) {i->mounted=host_sd;return 0;}
static int read_pico_sd_remount(void) {return 1;}
#define PMU_CMD_HOST_REQUEST_RESET 0x0312
static int read_pico_pmu_cmd(unsigned c,const uint8_t* p,size_t n) {(void)c;(void)p;(void)n;return 0;}
static unsigned host_suspend,host_resume,host_off,host_sleep_body_ink,host_sleep_hint_ink;
static int host_suspend_failure;
static unsigned host_unlock_refresh;
static void thingsos_board_unlock_refresh(void) { ++host_unlock_refresh; }
static int read_pico_pmu_power_off(void) {host_off++;return 0;}
static char host_font[512];
static bool thingsos_board_font(thingsos_font_t* f) {return thingsos_font_file(f,host_font);}
static int thingsos_board_set_time(uint32_t t) {(void)t;return 0;}
static void thingsos_board_rom_download(void) {}
typedef struct {char ssid[33];int rssi;bool open;} thingsos_wifi_result_t;
static void thingsos_network_healthy(void) {}
static int thingsos_network_start(bool r) {(void)r;return 0;}
static const char* thingsos_network_ssid(void) {return THINGSOS_AP_SSID;}
static const char* thingsos_network_password(void) {return THINGSOS_AP_PASSWORD;}
static const char* thingsos_network_token(void) {return "0123456789abcdef0123456789abcdef";}
static bool host_network_connected, host_network_busy;
static bool thingsos_network_connected(void) {return host_network_connected;}
static bool thingsos_network_busy(void) {return host_network_busy;}
static thingsos_release_state_t host_release;
static bool host_release_checking;
static bool thingsos_network_release_checking(void){return host_release_checking;}
static unsigned host_release_checks,host_release_installs;
static void thingsos_network_release_state(thingsos_release_state_t* s){*s=host_release;}
static int thingsos_network_release_start(bool install){if(!host_network_connected)return ESP_ERR_INVALID_STATE;if(install)++host_release_installs;else ++host_release_checks;return ESP_OK;}

static void thingsos_network_address(bool h,char* out,size_t cap) {strlcpy(out,h?"http://192.168.4.1":host_network_connected?"http://192.168.1.42":"等待网络地址",cap);}
static bool host_hotspot;
static int thingsos_network_mode(bool h) {if(host_network_busy)return ESP_ERR_INVALID_STATE;host_hotspot=h;return 0;}
static int thingsos_network_forget(void) {return 0;}
static unsigned host_wifi_disconnects;
static int host_wifi_disconnect_error;
static int thingsos_network_disconnect(void) {
    ++host_wifi_disconnects;if(host_wifi_disconnect_error)return host_wifi_disconnect_error;
    host_network_connected=false;return 0;
}
static bool host_http_requested;
static void thingsos_network_service(bool b) {host_http_requested=b;}
static int thingsos_network_service_poll(void) {return 0;}
static void thingsos_network_auto_time(bool b) {(void)b;}
static void thingsos_network_library(const char* p) {(void)p;}
static unsigned host_wifi_connects;
static int thingsos_network_connect(const char* s,const char* p) {(void)s;(void)p;++host_wifi_connects;return 0;}
static unsigned host_wifi_scans,host_wifi_polls;
static bool host_wifi_scan_ready;
static int host_wifi_scan_start_error,host_wifi_scan_result_error;
static int thingsos_network_scan_start(void) {++host_wifi_scans;return host_wifi_scan_start_error;}
static int thingsos_network_scan_poll(thingsos_wifi_result_t* r,size_t cap,size_t* n,bool* done) {
    ++host_wifi_polls;*done=host_wifi_scan_ready;if(!*done)return ESP_OK;
    *n=cap?1:0;if(cap){strlcpy(r[0].ssid,"异步测试网络",sizeof(r[0].ssid));r[0].rssi=-45;r[0].open=false;}
    return host_wifi_scan_result_error;
}
static int thingsos_enter_rescue(void) {return 0;}
typedef unsigned nvs_handle_t;
#define NVS_READWRITE 1
#define NVS_READONLY 0
static bool host_nvs_available, host_nvs_commit_fail,host_nvs_read;
static uint32_t host_saved_weight;
static uint32_t host_saved_indent;
static uint32_t host_saved_ui[8];
static const char* host_ui_keys[]={"ui-px","ui-weight","ui-refresh","ui-mode","wallpaper","refresh","px","mode"};
static uint8_t host_saved_px_default;
static char host_wall_strings[3][320];
static const char* host_wall_keys[]={"wall-book","wall-title","wall-image"};
static uint8_t host_saved_immersive;
static bool host_immersive_present;
static uint8_t host_saved_ripple,host_saved_tilt,host_saved_landscape,host_saved_tilt_sensitivity=1;
static bool host_tilt_sensitivity_present;
static bool host_tilt_active;
static bool host_touch_contact;
static bool thingsos_board_touch_contact_pending(void) {return host_touch_contact;}
static int host_tilt_x,host_tilt_y,host_tilt_z=1000;
static bool host_tilt_available=true;
static thingsos_tilt_stream_t host_tilt_stream;
static bool host_tilt_landscape;
static unsigned host_tilt_level=1;
static bool thingsos_board_tilt_active(bool active) {
    if(!active || active!=host_tilt_active)memset(&host_tilt_stream,0,sizeof(host_tilt_stream));
    host_tilt_active=active;return true;
}
static void thingsos_board_tilt_configure(bool landscape,unsigned level) {
    if(landscape!=host_tilt_landscape || level!=host_tilt_level)memset(&host_tilt_stream,0,sizeof(host_tilt_stream));
    host_tilt_landscape=landscape;host_tilt_level=level;
}
static thingsos_tilt_queue_t host_tilt_samples;
static bool host_tilt_manual;
static uint64_t host_tilt_sample_clock;
static bool thingsos_board_tilt_read_raw(int* x,int* y,int* z,uint64_t* ms) {
    thingsos_tilt_input_t sample;
    if(thingsos_tilt_queue_pop(&host_tilt_samples,&sample)){*x=sample.x;*y=sample.y;*z=sample.z;*ms=sample.ms;return true;}
    if(host_tilt_manual || !host_tilt_available || host_tilt_sample_clock==host_clock_us)return false;
    host_tilt_sample_clock=host_clock_us;*ms=host_clock_us/1000;
    *x=-host_tilt_x;*y=host_tilt_y;*z=-host_tilt_z;return true;
}
static bool thingsos_board_tilt_event(int* direction,uint64_t* at,thingsos_tilt_t* state) {
    int x,y,z;uint64_t ms;
    for(unsigned i=0;i<THINGSOS_TILT_QUEUE_SIZE && thingsos_board_tilt_read_raw(&x,&y,&z,&ms);++i)
        thingsos_tilt_stream_sample(&host_tilt_stream,x,y,z,ms,host_tilt_landscape,host_tilt_level,host_touch_contact);
    *direction=thingsos_tilt_stream_take(&host_tilt_stream,host_clock_us/1000,at);
    *state=host_tilt_stream.state;return true;
}
static uint8_t host_saved_wd[4];
static bool host_saved_wd_valid;
static uint8_t host_note_manual;
static bool host_note_manual_present;
static const char* host_wd_keys[]={"wd-pull","wd-push","wd-time","wd-consent"};
static int nvs_flash_init(void) {return host_nvs_available?0:1;}
static unsigned host_nvs_commits;
static int nvs_open(const char* ns,int mode,nvs_handle_t* h) {if(host_nvs_available && (mode==NVS_READWRITE || host_nvs_read) && !strcmp(ns,"thingsos-ui")) {*h=1;return 0;}return 1;}
static int nvs_set_u32(unsigned h,const char* k,uint32_t v) {(void)h;if(!strcmp(k,"weight")) host_saved_weight=v;if(!strcmp(k,"indent")) host_saved_indent=v;for(unsigned i=0;i<8;++i)if(!strcmp(k,host_ui_keys[i]))host_saved_ui[i]=v;return 0;}
static int nvs_set_u8(unsigned h,const char* k,uint8_t v) {(void)h;if(!strcmp(k,"immersive")){host_saved_immersive=v;host_immersive_present=true;}if(!strcmp(k,"note-manual")){host_note_manual=v;host_note_manual_present=true;}for(unsigned i=0;i<4;++i)if(!strcmp(k,host_wd_keys[i])){host_saved_wd[i]=v;host_saved_wd_valid=true;}if(!strcmp(k,"tilt-sens")){host_saved_tilt_sensitivity=v;host_tilt_sensitivity_present=true;}if(!strcmp(k,"landscape"))host_saved_landscape=v;if(!strcmp(k,"tilt"))host_saved_tilt=v;if(!strcmp(k,"ripple")) host_saved_ripple=v;if(!strcmp(k,"px-default"))host_saved_px_default=v;return 0;}
static int nvs_set_str(unsigned h,const char* k,const char* v) {(void)h;for(unsigned i=0;i<3;++i)if(!strcmp(k,host_wall_keys[i]))snprintf(host_wall_strings[i],sizeof(host_wall_strings[i]),"%s",v);return 0;}
static unsigned char host_pin_record[128];
static size_t host_pin_record_bytes;
static int nvs_set_blob(unsigned h,const char* k,const void* v,size_t n) {
    (void)h;
    if(strcmp(k,"lock-pin-v1"))return 0;
    if(n>sizeof(host_pin_record))return 1;
    memcpy(host_pin_record,v,n);host_pin_record_bytes=n;return 0;
}
static int nvs_get_blob(unsigned h,const char* k,void* v,size_t* n) {
    (void)h;
    if(strcmp(k,"lock-pin-v1") || !host_pin_record_bytes)return ESP_ERR_NVS_NOT_FOUND;
    if(*n<host_pin_record_bytes)return 1;
    *n=host_pin_record_bytes;memcpy(v,host_pin_record,*n);return 0;
}
static int nvs_get_u32(unsigned h,const char* k,uint32_t* v) {(void)h;if(host_nvs_read && !strcmp(k,"weight")) {*v=host_saved_weight;return 0;}if(host_nvs_read && !strcmp(k,"indent")) {*v=host_saved_indent;return 0;}if(host_nvs_read)for(unsigned i=0;i<8;++i)if(!strcmp(k,host_ui_keys[i])){*v=host_saved_ui[i];return 0;}return 1;}
static int nvs_get_u8(unsigned h,const char* k,uint8_t* v) {(void)h;if(host_nvs_read && host_immersive_present && !strcmp(k,"immersive")){*v=host_saved_immersive;return 0;}if(host_nvs_read && host_note_manual_present && !strcmp(k,"note-manual")){*v=host_note_manual;return 0;}if(host_nvs_read&&host_saved_wd_valid)for(unsigned i=0;i<4;++i)if(!strcmp(k,host_wd_keys[i])){*v=host_saved_wd[i];return 0;}if(host_nvs_read && host_tilt_sensitivity_present && !strcmp(k,"tilt-sens")){*v=host_saved_tilt_sensitivity;return 0;}if(host_nvs_read && !strcmp(k,"landscape")){*v=host_saved_landscape,host_saved_tilt_sensitivity=1;
static bool host_tilt_sensitivity_present;return 0;}if(host_nvs_read && !strcmp(k,"tilt")){*v=host_saved_tilt;return 0;}if(host_nvs_read && !strcmp(k,"ripple")) {*v=host_saved_ripple;return 0;}if(host_nvs_read && !strcmp(k,"px-default")){*v=host_saved_px_default;return 0;}return 1;}
static int nvs_get_str(unsigned h,const char* k,char* v,size_t* n) {(void)h;if(host_nvs_read)for(unsigned i=0;i<3;++i)if(!strcmp(k,host_wall_keys[i]) && strlen(host_wall_strings[i])+1<=*n){strcpy(v,host_wall_strings[i]);*n=strlen(v)+1;return 0;}return 1;}
static int nvs_commit(unsigned h) {(void)h;++host_nvs_commits;return host_nvs_commit_fail?1:0;}
static void nvs_close(unsigned h) {(void)h;}


static int thingsos_board_suspend(const thingsos_canvas_t* c) {
    host_suspend++;host_sleep_body_ink=host_sleep_hint_ink=0;
    for(int y=250;y<850;++y) for(int x=36;x<648;++x)
        host_sleep_body_ink+=c->pixels[(size_t)y*c->width+x]<128;
    for(int y=1060;y<1110;++y) for(int x=36;x<648;++x)
        host_sleep_hint_ink+=c->pixels[(size_t)y*c->width+x]<128;
    return host_suspend_failure;
}
static int thingsos_board_resume(void) {host_resume++;return 0;}
static void (*host_scan_barrier)(void);
static unsigned host_cleanup_requests;
static int host_cleanup_x,host_cleanup_y,host_cleanup_w,host_cleanup_h;
static void thingsos_board_cleanup_area(int x,int y,int w,int h) {
    ++host_cleanup_requests;host_cleanup_x=x;host_cleanup_y=y;host_cleanup_w=w;host_cleanup_h=h;
}
static void thingsos_board_scan_barrier(void (*barrier)(void)) {host_scan_barrier=barrier;}
static int thingsos_board_present_reader_stagger(const thingsos_canvas_t* c,bool forward) {
    (void)c;(void)forward;return 0;
}

static bool thingsos_network_job(thingsos_manage_job_t* p) {(void)p;return false;}
static void thingsos_network_job_done(unsigned a,unsigned b,unsigned c) {(void)a;(void)b;(void)c;}
static thingsos_transfer_state_t host_transfer;
static void thingsos_network_transfer(thingsos_transfer_state_t* s) {*s=host_transfer;}
static const char* thingsos_network_failure(void) {return "";}

static const char* thingsos_board_charge(void) {return "未充电";}
static unsigned host_battery_percent=100,host_power_samples;
static bool host_charge_connected,host_power_valid=true;
static bool thingsos_board_power_status(unsigned* percent,bool* connected) {
    ++host_power_samples;
    if(!host_power_valid)return false;
    *percent=host_battery_percent;*connected=host_charge_connected;return true;
}

static void thingsos_network_maintenance(void) {}
static char host_saved_wifi[33];
static const char* thingsos_network_saved_name(void) {return host_saved_wifi;}

static bool thingsos_network_recover_files(void) {return true;}

static bool thingsos_network_pause_requested(void) {return false;}
static void thingsos_network_pause_complete(bool ok) {(void)ok;}
static void thingsos_network_card(uint64_t id) {(void)id;}

static unsigned host_weidu_cover_calls;
static void thingsos_weidu_device_covers(const thingsos_weidu_book_t* books,size_t count) {(void)books;(void)count;++host_weidu_cover_calls;}

static bool thingsos_weidu_device_partial(const char* id) {(void)id;return false;}

static bool thingsos_network_saved(const char* ssid){return host_saved_wifi[0] && !strcmp(ssid,host_saved_wifi);}
static int thingsos_network_reconnect(const char* ssid){return thingsos_network_connect(ssid,"saved-password");}

/* Deterministic calendar rendering, defaulting to the real clock elsewhere. */
static time_t host_wall_time;
static time_t host_calendar_time(time_t* out) {
    time_t value=host_wall_time?host_wall_time:time(NULL);
    if(out)*out=value;
    return value;
}
#define time host_calendar_time

#ifndef ESP_LOGW
#define ESP_LOGW(...) ((void)0)
#endif

#include "thingsos/usb_disk.h"
static thingsos_usb_status_t host_usb;
static void thingsos_board_usb_status(thingsos_usb_status_t* s){*s=host_usb;}

static thingsos_wd_device_status_t host_wd_reading={.pull=WD_CANCEL,.progress=WD_CANCEL,.duration=WD_CANCEL,.notes=WD_CANCEL};
static thingsos_wd_upload_t host_wd_upload;
static void thingsos_wd_device_status(thingsos_wd_device_status_t* out){*out=host_wd_reading;}
static thingsos_rt_device_status_t host_wd_readtime;
static void thingsos_wd_rt_status(thingsos_rt_device_status_t* out){*out=host_wd_readtime;}
static bool thingsos_weidu_background_ready(void){return true;}
static bool thingsos_wd_device_sync_load(const char* id,const char* owner,thingsos_wd_sync_t* out){
    return thingsos_wd_sync_load("/sdcard/.thingsos/weidu",owner,id,out);
}
static bool thingsos_wd_device_sync_save(const char* id,const char* owner,const thingsos_wd_sync_t* in){
    return thingsos_wd_sync_save("/sdcard/.thingsos/weidu",owner,id,in);
}
static bool host_personal_available;
static thingsos_wd_personal_t host_personal[2];
static bool thingsos_wd_device_personal_info(const char* id,unsigned* count){(void)id;*count=host_personal_available?2:0;return host_personal_available;}
static bool thingsos_wd_device_personal(const char* id,unsigned i,thingsos_wd_personal_t* out){(void)id;if(!host_personal_available || i>=2)return false;*out=host_personal[i];return true;}
static bool thingsos_wd_device_personal_find(const char* id,const char* uid,uint32_t a,uint32_t b,thingsos_wd_personal_t* out){(void)id;(void)uid;(void)a;(void)b;(void)out;return false;}
static bool host_note_request_allowed;
static unsigned host_note_requests;
static uint32_t host_note_request_id;
static bool thingsos_wd_device_annotation_request(const char* id,uint64_t book,const char* owner,const thingsos_annotation_t* note){
    (void)id;(void)book;(void)owner;
    if(!host_note_request_allowed)return false;
    ++host_note_requests;host_note_request_id=note->id;host_weidu.busy=true;return true;
}
static bool thingsos_wd_device_request(thingsos_weidu_action_t a,const char* id,const thingsos_wd_upload_t* up){(void)id;if(up)host_wd_upload=*up;return thingsos_weidu_start(a,NULL);}
static bool host_wd_notes_available;
static bool thingsos_wd_device_notes(const char* id,thingsos_wd_notes_info_t* out){(void)id;memset(out,0,sizeof(*out));if(host_wd_notes_available)for(unsigned i=0;i<3;++i)out->count[i]=out->total[i]=3;return host_wd_notes_available;}
static bool thingsos_wd_device_note(const char* id,unsigned k,unsigned i,thingsos_wd_note_t* out){
 (void)id;(void)k;if(!host_wd_notes_available||i>=3)return false;memset(out,0,sizeof(*out));
 strcpy(out->text,i==0?"读书使我们在别人的思想中，看见自己的生活。":i==1?"这是一条较长的划线，打开后可以阅读完整内容，并从阅读页跳转到相应章节。":"每一次重新阅读，都会发现新的理解。");
 strcpy(out->uid,"1");strcpy(out->chapter,"第一章");strcpy(out->author,"读书的人");out->heat=128;return true;
}

static bool thingsos_wd_device_defer(const thingsos_wd_upload_t* in){(void)in;return true;}

static bool host_wd_mapped_lines;
static bool host_wd_map(const char* root,const char* id,unsigned index,const thingsos_chapter_t* ch,thingsos_wd_map_t* map) {
    if(!host_wd_mapped_lines)return thingsos_wd_map(root,id,index,ch,map);
    map->text=ch->text;map->bytes=ch->bytes;return true;
}
#define thingsos_wd_map host_wd_map
static bool thingsos_wd_device_lines(const char* id,unsigned chapter,const thingsos_wd_map_t* map,const thingsos_chapter_t* text,thingsos_text_underline_t* lines,size_t* count,size_t* total){
    (void)id;(void)chapter;(void)map;(void)text;*count=*total=0;
    if(!host_wd_mapped_lines)return false;
    lines[0]=(thingsos_text_underline_t){0,4,false};*count=*total=1;return true;
}

static bool host_thought_cache,host_thought_line,host_thought_long;
static const char* host_thought_authors[4],*host_thought_texts[4];
static unsigned host_thought_requests;
static thingsos_wd_thought_query_t host_thought_query;
static bool thingsos_wd_device_thought_request(const thingsos_wd_thought_query_t* q){++host_thought_requests;host_thought_query=*q;return thingsos_weidu_start(WD_DEVICE_THOUGHTS,NULL);}
static bool thingsos_wd_device_thought_info(const thingsos_wd_thought_query_t* q,thingsos_wd_thought_info_t* info){
 (void)q;memset(info,0,sizeof(*info));if(!host_thought_cache)return false;
 info->count=4;info->next_idx=q->max_idx+10;info->more=q->max_idx==0;strcpy(info->sync,"1234567890123456789");return true;
}
static bool thingsos_wd_device_thought(const thingsos_wd_thought_query_t* q,unsigned i,thingsos_wd_note_t* n){
 (void)q;if(!host_thought_cache||i>=4)return false;memset(n,0,sizeof(*n));strcpy(n->author,"读者的想法");strcpy(n->text,"这段文字让我想起自己的经历。阅读让我们看到另一种可能，也让不同的理解在这里相遇。");if(host_thought_authors[i])strlcpy(n->author,host_thought_authors[i],sizeof(n->author));if(host_thought_texts[i])strlcpy(n->text,host_thought_texts[i],sizeof(n->text));if(host_thought_long){const char* paragraph="书里的故事与自己的生活并不相同，但某些感受会在阅读中慢慢靠近。文字没有改变，我们的理解却比从前多了一层。\n\n";while(strlen(n->text)+strlen(paragraph)<sizeof(n->text))strcat(n->text,paragraph);}return true;
}
static bool thingsos_wd_device_line_note(const char* id,unsigned chapter,const thingsos_wd_map_t* map,const thingsos_chapter_t* text,size_t begin,size_t end,thingsos_wd_note_t* n){
 (void)id;(void)chapter;(void)map;(void)text;(void)begin;(void)end;if(!host_thought_line)return false;
 memset(n,0,sizeof(*n));strcpy(n->uid,"1");strcpy(n->text,"读书使我们在别人的思想中，看见自己的生活。");n->range=true;n->begin=3;n->length=4;return true;
}

static bool thingsos_weidu_device_chapters(const thingsos_weidu_book_t* b,unsigned i,unsigned n,bool cover){host_weidu_prepare_cover=cover;host_weidu_chapter=i;host_weidu_chapters=n;host_weidu.background=true;return thingsos_weidu_start(WD_DEVICE_CHAPTER,b);}
static bool thingsos_weidu_device_chapter(const thingsos_weidu_book_t* b,unsigned i){return thingsos_weidu_device_chapters(b,i,1,false);}
static bool thingsos_wd_device_schedule(const thingsos_wd_upload_t* in){return thingsos_wd_device_request(WD_DEVICE_UPLOAD,in->id,in);}
static void thingsos_wd_device_poll(bool idle){(void)idle;}
