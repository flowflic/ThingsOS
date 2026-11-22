// SPDX-License-Identifier: Apache-2.0
/* Host harness replaces hardware only; draws and taps execute the real controller. */
#pragma once
/* PSRAM placement attribute has no meaning on the host. */
#ifndef EXT_RAM_BSS_ATTR
#define EXT_RAM_BSS_ATTR
#endif
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
#include "thingsos/audio.h"
#include "thingsos/bluetooth_protocol.h"
#include "thingsos/ble_discovery.h"
#include "thingsos/ima.h"
#include "thingsos/cubox.h"
#include "thingsos/flomo.h"
#include "thingsos/tilt.h"
#include "thingsos/network_state.h"
#include "thingsos/release.h"
#include "thingsos/network_defaults.h"
#define WEIDU_DEVICE_API static
#include "../firmware/components/thingsos_weidu/weidu_device.h"
#undef WEIDU_DEVICE_API
#define THINGSOS_SYSTEM_API static
#include "../firmware/components/thingsos_system/system_monitor_device.h"
#undef THINGSOS_SYSTEM_API
static thingsos_system_snapshot_t host_system;
static unsigned host_system_reads,host_system_resets;
static void thingsos_system_reset(void){++host_system_resets;}
static void thingsos_system_read(thingsos_system_snapshot_t* s){++host_system_reads;*s=host_system;}
bool thingsos_board_time(uint32_t* out){if(out)*out=1794628800;return true;}
static thingsos_rt_device_status_t host_rt_status;
static bool host_offline_present;
static uint8_t host_saved_offline;
static void thingsos_wd_rt_status(thingsos_rt_device_status_t* s){*s=host_rt_status;}
static bool thingsos_wd_rt_flush(thingsos_rt_capture_t* c){(void)c;return true;}
static void thingsos_wd_rt_poll(bool enabled,bool idle){(void)enabled;(void)idle;}
static unsigned host_rt_resumes;
static bool thingsos_wd_rt_resume(void){++host_rt_resumes;host_rt_status.blocked=false;return true;}
static thingsos_weidu_status_t host_weidu;
static thingsos_weidu_action_t host_weidu_action;
static unsigned host_weidu_calls;
static char host_weidu_epub[512];
static size_t host_weidu_shelf_total=5;
static bool host_weidu_search_blocked;
static bool host_online_search_async,host_online_search_ready,host_online_added;
static unsigned host_cover_serial;
static unsigned thingsos_weidu_device_cover_serial(void){return host_cover_serial;}
static unsigned host_weidu_search_calls;
static unsigned host_library_order_reads;
static bool thingsos_weidu_device_init(void) {return true;}
static unsigned host_weidu_chapter;
static unsigned host_weidu_chapters;
static bool host_weidu_prepare_cover;
static thingsos_weidu_start_result_t host_weidu_chapter_start=WD_START_OK;
static bool thingsos_network_connected(void);
static char host_online_root[512]="/sdcard/.thingsos/weidu";
#define ONLINE_ROOT host_online_root
static bool thingsos_weidu_device_chapter(const thingsos_weidu_book_t* b,unsigned index) {
    host_weidu_chapter=index;return thingsos_weidu_start(WD_DEVICE_CHAPTER,b);
}
static thingsos_weidu_start_result_t thingsos_weidu_device_chapters(const thingsos_weidu_book_t* b,unsigned index,unsigned count,bool prepare_cover) {
    if(!thingsos_network_connected())return WD_START_OFFLINE;
    if(host_weidu_chapter_start!=WD_START_OK)return host_weidu_chapter_start;
    host_weidu_prepare_cover=prepare_cover;
    host_weidu_chapter=index;host_weidu_chapters=count;
    return thingsos_weidu_start(WD_DEVICE_CHAPTER,b)?WD_START_OK:WD_START_BUSY;
}
static void thingsos_board_static_page(bool enabled){(void)enabled;}
static unsigned host_loading_presents;
static unsigned host_full_refreshes;
void thingsos_board_full_refresh(void){++host_full_refreshes;}
static unsigned host_metrics_presents;
static unsigned host_audio_diff_presents;
static unsigned host_audio_clean_presents;
static int host_audio_clean_x,host_audio_clean_y,host_audio_clean_w,host_audio_clean_h;
int thingsos_board_present_audio_diff_clean(const thingsos_canvas_t* c,int x,int y,int w,int h){
    (void)c;++host_audio_clean_presents;host_audio_clean_x=x;host_audio_clean_y=y;host_audio_clean_w=w;host_audio_clean_h=h;return 0;
}

int thingsos_board_present_audio_diff(const thingsos_canvas_t* c){(void)c;++host_audio_diff_presents;return 0;}

static int host_metrics_result;
int thingsos_board_present_metrics(const thingsos_canvas_t* c){(void)c;++host_metrics_presents;return host_metrics_result;}
static int host_loading_x,host_loading_y,host_loading_w,host_loading_h,host_loading_result;
static int thingsos_board_present_loading(const thingsos_canvas_t* c,int x,int y,int w,int h) {
    (void)c;++host_loading_presents;host_loading_x=x;host_loading_y=y;host_loading_w=w;host_loading_h=h;
    return host_loading_result;
}
static void thingsos_weidu_state(thingsos_weidu_status_t* s) {*s=host_weidu;}
static bool thingsos_weidu_busy(void) {return host_weidu.busy;}
static bool host_weidu_start_refused;
static bool thingsos_weidu_start(thingsos_weidu_action_t a,const thingsos_weidu_book_t* b) {
    if(host_weidu_start_refused){++host_weidu_calls;return false;}
    (void)b;host_weidu_action=a;++host_weidu_calls;host_weidu.busy=true;
    host_weidu.background=a==WD_DEVICE_COVERS || a==WD_DEVICE_CHAPTER || a==WD_DEVICE_SEARCH ||
        (a>=WD_DEVICE_NOTES && a<=WD_DEVICE_THOUGHTS) || (a>=WD_DEVICE_ENTER && a<=WD_DEVICE_NOTE_WRITE);
    ++host_weidu.serial;return true;
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
struct dirent {char d_name[256];};
typedef struct {HANDLE handle;WIN32_FIND_DATAW data;bool first;struct dirent entry;} DIR;
static DIR* opendir(const char* p) {
    char pattern[1024];snprintf(pattern,sizeof(pattern),"%s/*",p);wchar_t wide[1024];
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,pattern,-1,wide,1024))return NULL;
    DIR* d=calloc(1,sizeof(*d));if(!d)return NULL;
    d->handle=FindFirstFileW(wide,&d->data);if(d->handle==INVALID_HANDLE_VALUE){free(d);return NULL;}
    d->first=true;return d;
}
static struct dirent* readdir(DIR* d) {
    if(!d)return NULL;
    do {
        if(!d->first&&!FindNextFileW(d->handle,&d->data))return NULL;
        d->first=false;
        if(WideCharToMultiByte(CP_UTF8,0,d->data.cFileName,-1,d->entry.d_name,sizeof(d->entry.d_name),NULL,NULL))return &d->entry;
    }while(true);
}
static void closedir(DIR* d) {if(d){FindClose(d->handle);free(d);}}
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
#define ESP_FAIL -1
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
typedef struct {bool mounted;uint64_t free_bytes,capacity_bytes;} thingsos_sd_info_t;
static bool host_sd;
static char host_picture_root[512];
static int thingsos_board_sd_info(thingsos_sd_info_t* i) {i->mounted=host_sd;i->free_bytes=3ULL*1024*1024*1024;i->capacity_bytes=8ULL*1024*1024*1024;return 0;}
static int thingsos_board_sd_remount(void) {return 1;}
static void thingsos_board_restart(void) {}
static unsigned host_light_mode,host_light_level,host_haptic_ms,host_side_key;
static void thingsos_board_frontlight(unsigned mode,unsigned level) {host_light_mode=mode;host_light_level=level;}
static unsigned host_light_warmth;
static void thingsos_board_frontlight_mix(bool enabled,unsigned level,unsigned warmth){host_light_mode=enabled?(warmth==100?1:warmth==0?2:3):0;host_light_level=level;host_light_warmth=warmth;}
static thingsos_audio_status_t host_audio={.volume=40};
static bool host_audio_deferred,host_audio_record_reject,host_audio_stop_reject,host_audio_pause_deferred;
static bool thingsos_audio_active(void){return host_audio.state!=THINGSOS_AUDIO_IDLE&&host_audio.state!=THINGSOS_AUDIO_ERROR;}
/* Playing/recording hold the network claim; a parked playback pause does not. */
static bool thingsos_audio_claimed(void){return host_audio.state==THINGSOS_AUDIO_PLAYING||host_audio.state==THINGSOS_AUDIO_RECORDING||host_audio.state==THINGSOS_AUDIO_RECORD_PAUSED;}
static void thingsos_audio_status(thingsos_audio_status_t* s){*s=host_audio;}
static void thingsos_audio_volume(unsigned v){host_audio.volume=v;}
static bool thingsos_audio_play(const char* path,uint32_t ms){host_audio.state=host_audio_deferred?THINGSOS_AUDIO_STARTING:THINGSOS_AUDIO_PLAYING;host_audio.position_ms=ms;host_audio.duration_ms=180000;strlcpy(host_audio.path,path,sizeof(host_audio.path));++host_audio.serial;return true;}
static bool thingsos_audio_record(void){if(host_audio_record_reject)return false;host_audio.state=host_audio_deferred?THINGSOS_AUDIO_STARTING:THINGSOS_AUDIO_RECORDING;host_audio.position_ms=0;strlcpy(host_audio.path,"/sdcard/audio/REC-host.wav",sizeof(host_audio.path));++host_audio.serial;return true;}
static bool thingsos_audio_pause(bool p){if(host_audio_pause_deferred)return true;bool rec=host_audio.state==THINGSOS_AUDIO_RECORDING||host_audio.state==THINGSOS_AUDIO_RECORD_PAUSED;host_audio.state=rec?(p?THINGSOS_AUDIO_RECORD_PAUSED:THINGSOS_AUDIO_RECORDING):(p?THINGSOS_AUDIO_PLAY_PAUSED:THINGSOS_AUDIO_PLAYING);++host_audio.serial;return true;}
static bool thingsos_audio_stop(void){if(host_audio_stop_reject)return false;host_audio.state=host_audio_deferred?THINGSOS_AUDIO_STOPPING:THINGSOS_AUDIO_IDLE;host_audio.ended=false;++host_audio.serial;return true;}
static bool thingsos_audio_seek(uint32_t ms){host_audio.position_ms=ms;++host_audio.serial;return true;}
static bool thingsos_audio_quiesce_locked(void){return thingsos_audio_stop()&&host_audio.state==THINGSOS_AUDIO_IDLE;}
static bool thingsos_audio_suspend_locked(void){if(host_audio.state==THINGSOS_AUDIO_PLAYING)host_audio.state=THINGSOS_AUDIO_PLAY_PAUSED;return host_audio.state==THINGSOS_AUDIO_IDLE||host_audio.state==THINGSOS_AUDIO_PLAY_PAUSED||host_audio.state==THINGSOS_AUDIO_ERROR;}
static thingsos_bt_status_t host_bt={.automatic=true};
static bool thingsos_bluetooth_enabled(void){return host_bt.enabled;}
static bool thingsos_bluetooth_connected(void){return host_bt.enabled&&host_bt.connected;}
static void thingsos_bluetooth_status(thingsos_bt_status_t* s){*s=host_bt;}
static bool thingsos_bluetooth_power(bool on){host_bt.enabled=on;host_bt.connected=false;host_bt.phase=on?THINGSOS_BT_READY:THINGSOS_BT_OFF;++host_bt.serial;return true;}
static bool thingsos_bluetooth_scan(bool on){if(on){host_bt.enabled=true;host_bt.connected=false;}host_bt.phase=on?THINGSOS_BT_SCANNING:THINGSOS_BT_READY;++host_bt.serial;return true;}
static bool thingsos_bluetooth_connect(const thingsos_bt_device_t* d){host_bt.enabled=true;host_bt.current=*d;host_bt.connected=true;host_bt.phase=THINGSOS_BT_CONNECTED;thingsos_bt_remember(&host_bt,d);++host_bt.serial;return true;}
static bool thingsos_bluetooth_disconnect(void){host_bt.connected=false;host_bt.phase=THINGSOS_BT_READY;++host_bt.serial;return true;}
static bool thingsos_bluetooth_forget(const char* address){bool ok=thingsos_bt_forget_record(&host_bt,address);host_bt.connected=false;++host_bt.serial;return ok;}
static bool thingsos_bluetooth_automatic(bool on){host_bt.automatic=on;++host_bt.serial;return true;}
static thingsos_ble_status_t host_ble;
static bool host_ble_started_before_audio_pause;
static bool thingsos_ble_scan(bool on){if(on&&host_audio.state==THINGSOS_AUDIO_PLAYING)host_ble_started_before_audio_pause=true;host_ble.scanning=on;++host_ble.serial;return true;}
static void thingsos_ble_status(thingsos_ble_status_t* out){*out=host_ble;}
static unsigned host_ble_suspends,host_ble_resumes;
static bool thingsos_ble_suspend(void){++host_ble_suspends;return true;}
static void thingsos_ble_resume(void){++host_ble_resumes;}
static bool host_ble_pair_accept;static uint32_t host_ble_pair_passkey;
static void thingsos_ble_init(void){}
static bool thingsos_ble_connect(const thingsos_ble_device_t* d){host_ble.current=host_ble.identity=*d;host_ble.connected=host_ble.bonded=host_ble.hid_ready=true;host_ble.phase=THINGSOS_BLE_CONNECTED;host_ble.pairing=THINGSOS_BLE_PAIR_NONE;thingsos_ble_remember(&host_ble,d);++host_ble.generation;++host_ble.serial;return true;}
static bool thingsos_ble_disconnect(void){host_ble.connected=host_ble.bonded=host_ble.hid_ready=false;host_ble.phase=THINGSOS_BLE_IDLE;host_ble.pairing=THINGSOS_BLE_PAIR_NONE;++host_ble.generation;++host_ble.serial;return true;}
static bool thingsos_ble_forget(const thingsos_ble_device_t* d){thingsos_ble_forget_record(&host_ble,d);return thingsos_ble_disconnect();}
static bool thingsos_ble_automatic(bool on){host_ble.automatic=on;++host_ble.serial;return true;}
static bool thingsos_ble_pair_reply(bool accept,uint32_t key){host_ble_pair_accept=accept;host_ble_pair_passkey=key;host_ble.pairing=THINGSOS_BLE_PAIR_NONE;++host_ble.serial;if(!accept)thingsos_ble_disconnect();return true;}
static thingsos_hid_key_t host_ble_keys[32];static unsigned host_ble_key_count;
static bool thingsos_ble_input(thingsos_hid_key_t* key){if(!host_ble_key_count)return false;*key=host_ble_keys[0];--host_ble_key_count;memmove(host_ble_keys,host_ble_keys+1,host_ble_key_count*sizeof(*key));return true;}
static thingsos_ima_status_t host_ima;
static bool host_ima_keep;
static void thingsos_ima_init(void){}
static void thingsos_ima_status(thingsos_ima_status_t* out){*out=host_ima;}
static bool thingsos_ima_active(void){return host_ima.busy;}
static bool thingsos_ima_pairing(void){return host_ima.pairing;}
static bool thingsos_ima_pair(bool enabled){host_ima.pairing=enabled;if(enabled)host_ima.config_phase=THINGSOS_IMA_CONFIG_IDLE;++host_ima.serial;return true;}
static void thingsos_ima_pair_poll(void){}
static bool thingsos_ima_library(unsigned i,thingsos_ima_library_t* out){if(i)return false;strlcpy(out->id,"kb-test",sizeof(out->id));strlcpy(out->name,"测试知识库",sizeof(out->name));return true;}
static bool thingsos_ima_refresh(bool next){(void)next;host_ima.count=1;++host_ima.serial;return host_ima.configured;}
static bool thingsos_ima_choose(unsigned i,bool persist){(void)persist;if(i)return false;strlcpy(host_ima.kb_name,"测试知识库",sizeof(host_ima.kb_name));return true;}
static bool thingsos_ima_upload(const char* path,bool keep){host_ima_keep=keep;strlcpy(host_ima.job.path,path,sizeof(host_ima.job.path));host_ima.job.phase=IMA_TRANSFERRING;host_ima.busy=true;++host_ima.serial;return host_ima.configured;}
static bool thingsos_ima_check(void){host_ima.job.phase=IMA_UPLOADED;++host_ima.serial;return true;}
static void thingsos_ima_cancel(void){host_ima.busy=false;host_ima.job.phase=IMA_CANCELLED;++host_ima.serial;}
static bool thingsos_ima_disconnect(void){host_ima.configured=false;return true;}
static bool thingsos_ima_quiesce_locked(void){thingsos_ima_cancel();return true;}
static bool thingsos_ima_file_status(const char* path,char* out,size_t cap){(void)path;strlcpy(out,"未上传",cap);return false;}
static thingsos_cubox_status_t host_cubox={.server="cubox.pro",.folder_name="全部文件夹"};
static thingsos_cubox_folder_t host_cubox_folders[4];
static unsigned host_cubox_folder_count;
static thingsos_cubox_card_t host_cubox_cards[8];
static unsigned host_cubox_card_count;
static thingsos_cubox_detail_t host_cubox_detail;
static bool host_cubox_detail_ok;
static char host_cubox_article[512];
static void thingsos_cubox_init(void){}
static void thingsos_cubox_set_network_yield(bool (*yield_locked)(void)){(void)yield_locked;}
static void thingsos_cubox_status(thingsos_cubox_status_t* out){host_cubox.folder_count=host_cubox_folder_count;host_cubox.card_count=host_cubox_card_count;*out=host_cubox;}
static bool thingsos_cubox_active(void){return host_cubox.busy;}
static bool thingsos_cubox_pairing(void){return host_cubox.pairing;}
static bool thingsos_cubox_pair(bool enabled){host_cubox.pairing=enabled;if(enabled){host_cubox.config_phase=THINGSOS_CUBOX_CONFIG_IDLE;host_cubox.pair_seconds=300;strlcpy(host_cubox.code,"87654321",sizeof(host_cubox.code));}++host_cubox.serial;return true;}
static void thingsos_cubox_pair_poll(void){}
static bool thingsos_cubox_disconnect(void){host_cubox.configured=false;++host_cubox.serial;return true;}
static bool thingsos_cubox_quiesce_locked(void){host_cubox.busy=false;host_cubox.pairing=false;return true;}
static bool thingsos_cubox_folder(unsigned i,thingsos_cubox_folder_t* out){if(i>=host_cubox_folder_count)return false;*out=host_cubox_folders[i];return true;}
static bool thingsos_cubox_card(unsigned i,thingsos_cubox_card_t* out){if(i>=host_cubox_card_count)return false;*out=host_cubox_cards[i];return true;}
static bool thingsos_cubox_detail_copy(const char* id,thingsos_cubox_detail_t* out){if(host_cubox_detail_ok&&!strcmp(host_cubox_detail.card.id,id)){*out=host_cubox_detail;return true;}for(unsigned i=0;i<host_cubox_card_count;++i)if(!strcmp(host_cubox_cards[i].id,id)){memset(out,0,sizeof(*out));out->card=host_cubox_cards[i];return true;}return false;}
static bool thingsos_cubox_article_path(const char* id,char* out,size_t cap){if(!id||!host_cubox_article[0])return false;strlcpy(out,host_cubox_article,cap);return true;}
static bool thingsos_cubox_set_filter(thingsos_cubox_filter_t f,const char* fid,const char* fname,const char* q,bool online){(void)online;host_cubox.filter=f;strlcpy(host_cubox.folder_id,fid?fid:"",sizeof(host_cubox.folder_id));strlcpy(host_cubox.folder_name,(fid&&fid[0]&&fname)?fname:"全部文件夹",sizeof(host_cubox.folder_name));strlcpy(host_cubox.query,q?q:"",sizeof(host_cubox.query));++host_cubox.serial;return true;}
static bool thingsos_cubox_select_slot(unsigned slot){if(slot>1u)return false;host_cubox.slot=slot;++host_cubox.serial;return true;}
static bool thingsos_cubox_refresh_folders(void){++host_cubox.serial;return host_cubox.configured;}
static bool thingsos_cubox_refresh_list(bool more){(void)more;++host_cubox.serial;return host_cubox.configured;}
static bool thingsos_cubox_fetch_detail(const char* id){(void)id;++host_cubox.serial;return host_cubox.configured;}
static bool thingsos_cubox_download(const char* id){strlcpy(host_cubox.active_card_id,id?id:"",sizeof(host_cubox.active_card_id));host_cubox.busy=true;host_cubox.op=THINGSOS_CUBOX_OP_DOWNLOAD;++host_cubox.serial;return host_cubox.configured;}
static bool thingsos_cubox_mark(const char* id,int r,int st){for(unsigned i=0;i<host_cubox_card_count;++i)if(!strcmp(host_cubox_cards[i].id,id)){if(r>=0)host_cubox_cards[i].read=r!=0;if(st>=0)host_cubox_cards[i].starred=st!=0;}if(host_cubox_detail_ok&&!strcmp(host_cubox_detail.card.id,id)){if(r>=0)host_cubox_detail.card.read=r!=0;if(st>=0)host_cubox_detail.card.starred=st!=0;}++host_cubox.serial;return true;}
static bool thingsos_cubox_archive(const char* id){if(host_cubox_detail_ok&&!strcmp(host_cubox_detail.card.id,id))host_cubox_detail.card.archived=true;++host_cubox.serial;return true;}
static bool thingsos_cubox_delete_cache(const char* id){for(unsigned i=0;i<host_cubox_card_count;++i)if(!strcmp(host_cubox_cards[i].id,id))host_cubox_cards[i].cached=false;if(host_cubox_detail_ok&&!strcmp(host_cubox_detail.card.id,id))host_cubox_detail.card.cached=false;host_cubox_article[0]=0;++host_cubox.serial;return true;}
static void thingsos_cubox_load_offline(void){}
static void thingsos_cubox_cancel(void){host_cubox.busy=false;++host_cubox.serial;}
static thingsos_flomo_status_t host_flomo;
static thingsos_flomo_memo_t host_flomo_memos[8];
static thingsos_flomo_tag_t host_flomo_tags[8];
static thingsos_flomo_memo_t host_flomo_related[4];
static unsigned host_flomo_fetches,host_flomo_lists;
static char host_flomo_full[256];
static void thingsos_flomo_init(void){}
static void thingsos_flomo_set_network_yield(bool (*yield_locked)(void)){(void)yield_locked;}
static void thingsos_flomo_status(thingsos_flomo_status_t* out){*out=host_flomo;}
static bool thingsos_flomo_active(void){return host_flomo.busy;}
static bool thingsos_flomo_pairing(void){return host_flomo.pairing;}
static bool thingsos_flomo_pair(bool enabled){host_flomo.pairing=enabled;if(enabled){host_flomo.config_phase=THINGSOS_FLOMO_CONFIG_IDLE;host_flomo.pair_seconds=300;strlcpy(host_flomo.code,"12345678",sizeof(host_flomo.code));}++host_flomo.serial;return true;}
static void thingsos_flomo_pair_poll(void){}
static bool thingsos_flomo_disconnect(void){host_flomo.configured=false;host_flomo.memo_count=0;++host_flomo.serial;return true;}
static bool thingsos_flomo_quiesce_locked(void){host_flomo.busy=false;host_flomo.pairing=false;return true;}
static bool thingsos_flomo_memo(unsigned i,thingsos_flomo_memo_t* out){if(i>=host_flomo.memo_count)return false;*out=host_flomo_memos[i];return true;}
static bool thingsos_flomo_tag(unsigned i,thingsos_flomo_tag_t* out){if(i>=host_flomo.tag_count)return false;*out=host_flomo_tags[i];return true;}
static bool thingsos_flomo_related(unsigned i,thingsos_flomo_memo_t* out){if(i>=host_flomo.related_count)return false;*out=host_flomo_related[i];return true;}
static bool thingsos_flomo_detail_copy(const char* id,thingsos_flomo_memo_t* memo,char* content,size_t cap,size_t* len,bool* complete){
    for(unsigned i=0;i<host_flomo.memo_count+host_flomo.related_count;++i){
        const thingsos_flomo_memo_t* m=i<host_flomo.memo_count?&host_flomo_memos[i]:&host_flomo_related[i-host_flomo.memo_count];
        if(strcmp(m->id,id))continue;
        *memo=*m;bool full=host_flomo_full[0]&&!strcmp(host_flomo.related_for,id);
        strlcpy(content,full?host_flomo_full:m->preview,cap);if(len)*len=strlen(content);if(complete)*complete=full||!m->truncated;return true;
    }
    return false;
}
static bool thingsos_flomo_set_filter(thingsos_flomo_mode_t mode,const char* tag,const char* query,bool online){host_flomo.mode=mode;strlcpy(host_flomo.tag,tag?tag:"",sizeof(host_flomo.tag));strlcpy(host_flomo.query,query?query:"",sizeof(host_flomo.query));if(online)++host_flomo_lists;++host_flomo.serial;return true;}
static bool thingsos_flomo_refresh_list(bool more){(void)more;++host_flomo_lists;++host_flomo.serial;return host_flomo.configured;}
static bool thingsos_flomo_refresh_tags(void){++host_flomo.serial;return host_flomo.configured;}
static bool thingsos_flomo_fetch_detail(const char* id){++host_flomo_fetches;strlcpy(host_flomo.active_id,id,sizeof(host_flomo.active_id));++host_flomo.serial;return host_flomo.configured;}
static void thingsos_flomo_load_offline(void){}
static void thingsos_flomo_cancel(void){host_flomo.busy=false;++host_flomo.serial;}
static void thingsos_board_haptic(unsigned ms) {host_haptic_ms=ms;}
static unsigned host_ai_key;
static unsigned thingsos_board_side_key(void) {unsigned k=host_side_key;host_side_key=0;return k;}
static unsigned thingsos_board_ai_key(void) {unsigned k=host_ai_key;host_ai_key=0;return k;}
static unsigned host_suspend,host_resume,host_off,host_sleep_body_ink,host_sleep_hint_ink;
static int host_suspend_failure;
static unsigned host_lock_presents;static bool host_lock_forward;static int host_lock_result;
static int thingsos_board_power_off(void) {host_off++;return 0;}
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
static bool host_network_connected, host_network_busy,host_hotspot;
static bool thingsos_network_connected(void) {return host_network_connected;}
static bool thingsos_network_connecting(void) {return false;}
static bool thingsos_network_busy(void) {return host_network_busy;}
/* Host model collapses transfer and external claims into one flag; an audio
   session is the external claim that must not block the WiFi page. */
static bool thingsos_network_wifi_busy(void) {return host_network_busy && !thingsos_audio_active();}
static bool host_radio_enabled=true;
static unsigned host_network_sleeps,host_network_wakes;
static void thingsos_network_sleep(void){++host_network_sleeps;}
static int thingsos_network_wake(void){++host_network_wakes;return ESP_OK;}
static int thingsos_network_radio(bool enabled){host_radio_enabled=enabled;if(!enabled){host_network_connected=false;host_hotspot=false;}return 0;}
static bool thingsos_network_radio_enabled(void){return host_radio_enabled;}
static thingsos_release_state_t host_release;
static bool host_release_checking;
static bool thingsos_network_release_checking(void){return host_release_checking;}
static unsigned host_release_checks,host_release_installs;
static void thingsos_network_release_state(thingsos_release_state_t* s){*s=host_release;}
static int thingsos_network_release_start(bool install){if(!host_network_connected)return ESP_ERR_INVALID_STATE;if(install)++host_release_installs;else ++host_release_checks;return ESP_OK;}

static void thingsos_network_address(bool h,char* out,size_t cap) {strlcpy(out,h?"http://192.168.4.1":host_network_connected?"http://192.168.1.42":"等待网络地址",cap);}
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
static int host_pending_invert=-1,host_saved_invert=-1;
static uint8_t host_saved_immersive,host_saved_vertical;
static bool host_vertical_present;
static int host_pending_vertical=-1;
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
static int nvs_set_u8(unsigned h,const char* k,uint8_t v) {if(!strcmp(k,"wd-offline")){host_saved_offline=v;host_offline_present=true;}(void)h;if(!strcmp(k,"vertical"))host_pending_vertical=v;if(!strcmp(k,"ui-invert"))host_pending_invert=v;if(!strcmp(k,"immersive")){host_saved_immersive=v;host_immersive_present=true;}if(!strcmp(k,"note-manual")){host_note_manual=v;host_note_manual_present=true;}for(unsigned i=0;i<4;++i)if(!strcmp(k,host_wd_keys[i])){host_saved_wd[i]=v;host_saved_wd_valid=true;}if(!strcmp(k,"tilt-sens")){host_saved_tilt_sensitivity=v;host_tilt_sensitivity_present=true;}if(!strcmp(k,"landscape"))host_saved_landscape=v;if(!strcmp(k,"tilt"))host_saved_tilt=v;if(!strcmp(k,"ripple")) host_saved_ripple=v;if(!strcmp(k,"px-default"))host_saved_px_default=v;return 0;}
static const char* host_font_keys[]={"font","ui-font"};
static char host_font_saved[2][320],host_font_pending[2][320];static unsigned host_font_pending_mask,host_font_saved_mask;
static int nvs_set_str(unsigned h,const char* k,const char* v) {(void)h;
    for(unsigned i=0;i<2;++i)if(!strcmp(k,host_font_keys[i])){snprintf(host_font_pending[i],320,"%s",v);host_font_pending_mask|=1u<<i;}
    for(unsigned i=0;i<3;++i)if(!strcmp(k,host_wall_keys[i]))snprintf(host_wall_strings[i],sizeof(host_wall_strings[i]),"%s",v);return 0;}
static unsigned char host_alarm_blob[1024],host_alarm_pending[1024];
static size_t host_alarm_blob_size,host_alarm_pending_size;
static unsigned char host_alarm_v1_blob[1024];
static size_t host_alarm_v1_blob_size;
static unsigned char host_pin_blob[128],host_pin_pending[128];
static bool host_private_input;
static void thingsos_board_private_input(bool enabled){host_private_input=enabled;}
static size_t host_pin_blob_size,host_pin_pending_size;
static bool host_pin_set_fail;
static int nvs_set_blob(unsigned h,const char* k,const void* v,size_t n) {(void)h;
    if(!strcmp(k,"lock-pin-v1")){if(host_pin_set_fail||n>sizeof(host_pin_pending))return ESP_FAIL;memcpy(host_pin_pending,v,n);host_pin_pending_size=n;return ESP_OK;}
    if(!strcmp(k,"alarms-v2")){if(n>sizeof(host_alarm_pending))return 1;memcpy(host_alarm_pending,v,n);host_alarm_pending_size=n;}return 0;}
static int nvs_get_blob(unsigned h,const char* k,void* v,size_t* n) {(void)h;
    if(!strcmp(k,"lock-pin-v1")){if(!host_nvs_read||!host_pin_blob_size)return ESP_ERR_NVS_NOT_FOUND;if(*n<host_pin_blob_size){*n=host_pin_blob_size;return ESP_FAIL;}if(v)memcpy(v,host_pin_blob,host_pin_blob_size);*n=host_pin_blob_size;return ESP_OK;}
    const unsigned char* data=!strcmp(k,"alarms-v2")?host_alarm_blob:host_alarm_v1_blob;
    size_t size=!strcmp(k,"alarms-v2")?host_alarm_blob_size:!strcmp(k,"alarms-v1")?host_alarm_v1_blob_size:0;
    if(!host_nvs_read||!size)return ESP_ERR_NVS_NOT_FOUND;
    if(*n<size){*n=size;return ESP_FAIL;}
    if(v)memcpy(v,data,size);*n=size;return ESP_OK;}
static int nvs_get_u32(unsigned h,const char* k,uint32_t* v) {(void)h;if(host_nvs_read && !strcmp(k,"weight")) {*v=host_saved_weight;return 0;}if(host_nvs_read && !strcmp(k,"indent")) {*v=host_saved_indent;return 0;}if(host_nvs_read)for(unsigned i=0;i<8;++i)if(!strcmp(k,host_ui_keys[i])){*v=host_saved_ui[i];return 0;}return 1;}
static int nvs_get_u8(unsigned h,const char* k,uint8_t* v) {if(host_nvs_read&&host_offline_present&&!strcmp(k,"wd-offline")){*v=host_saved_offline;return 0;}(void)h;if(host_nvs_read && host_vertical_present && !strcmp(k,"vertical")){*v=host_saved_vertical;return 0;}if(host_nvs_read && host_saved_invert>=0 && !strcmp(k,"ui-invert")){*v=(uint8_t)host_saved_invert;return 0;}if(host_nvs_read && host_immersive_present && !strcmp(k,"immersive")){*v=host_saved_immersive;return 0;}if(host_nvs_read && host_note_manual_present && !strcmp(k,"note-manual")){*v=host_note_manual;return 0;}if(host_nvs_read&&host_saved_wd_valid)for(unsigned i=0;i<4;++i)if(!strcmp(k,host_wd_keys[i])){*v=host_saved_wd[i];return 0;}if(host_nvs_read && host_tilt_sensitivity_present && !strcmp(k,"tilt-sens")){*v=host_saved_tilt_sensitivity;return 0;}if(host_nvs_read && !strcmp(k,"landscape")){*v=host_saved_landscape,host_saved_tilt_sensitivity=1;
static bool host_tilt_sensitivity_present;return 0;}if(host_nvs_read && !strcmp(k,"tilt")){*v=host_saved_tilt;return 0;}if(host_nvs_read && !strcmp(k,"ripple")) {*v=host_saved_ripple;return 0;}if(host_nvs_read && !strcmp(k,"px-default")){*v=host_saved_px_default;return 0;}return 1;}
static int nvs_get_str(unsigned h,const char* k,char* v,size_t* n) {(void)h;
    if(host_nvs_read)for(unsigned i=0;i<2;++i)if((host_font_saved_mask&(1u<<i))&&!strcmp(k,host_font_keys[i])&&strlen(host_font_saved[i])+1<=*n){strcpy(v,host_font_saved[i]);*n=strlen(v)+1;return 0;}
    if(host_nvs_read)for(unsigned i=0;i<3;++i)if(!strcmp(k,host_wall_keys[i]) && strlen(host_wall_strings[i])+1<=*n){strcpy(v,host_wall_strings[i]);*n=strlen(v)+1;return 0;}return 1;}
static int nvs_commit(unsigned h) {(void)h;++host_nvs_commits;if(host_nvs_commit_fail)return 1;
    for(unsigned i=0;i<2;++i)if(host_font_pending_mask&(1u<<i)){strcpy(host_font_saved[i],host_font_pending[i]);host_font_saved_mask|=1u<<i;}
    if(host_pin_pending_size){host_pin_blob_size=host_pin_pending_size;memcpy(host_pin_blob,host_pin_pending,host_pin_blob_size);}
    if(host_pending_invert>=0)host_saved_invert=host_pending_invert;
    if(host_pending_vertical>=0){host_saved_vertical=(uint8_t)host_pending_vertical;host_vertical_present=true;}
    if(host_alarm_pending_size){host_alarm_blob_size=host_alarm_pending_size;memcpy(host_alarm_blob,host_alarm_pending,host_alarm_blob_size);}return 0;}
static void nvs_close(unsigned h) {(void)h;host_font_pending_mask=0;host_alarm_pending_size=0;host_pin_pending_size=0;host_pending_invert=-1;host_pending_vertical=-1;}


static int thingsos_board_suspend(const thingsos_canvas_t* c) {
    host_suspend++;host_sleep_body_ink=host_sleep_hint_ink=0;
    for(int y=250;y<850;++y) for(int x=36;x<648;++x)
        host_sleep_body_ink+=c->pixels[(size_t)y*c->width+x]<128;
    for(int y=1060;y<1110;++y) for(int x=36;x<648;++x)
        host_sleep_hint_ink+=c->pixels[(size_t)y*c->width+x]<128;
    return host_suspend_failure;
}
static int thingsos_board_resume(void) {host_resume++;return 0;}
static unsigned host_trace_awake;
static void thingsos_board_trace_awake(void){++host_trace_awake;}
static void thingsos_board_trace_report(void){}
enum { THINGSOS_MARK_LOOP_TOP=1, THINGSOS_MARK_PAIR_POLLED, THINGSOS_MARK_SERVICE_POLLED, THINGSOS_MARK_IO_LOCK_WAIT,
    THINGSOS_MARK_IO_LOCKED, THINGSOS_MARK_APP_POLLED, THINGSOS_MARK_ALARM_POLLED, THINGSOS_MARK_POWER_KEY, THINGSOS_MARK_SLEEP_WAIT,
    THINGSOS_MARK_RESUME_NET=20, THINGSOS_MARK_RESUME_NET_DONE, THINGSOS_MARK_RESUME_UI,
    THINGSOS_MARK_DRAW=30, THINGSOS_MARK_PRESENT, THINGSOS_MARK_PRESENT_DONE };
static unsigned host_trace_mark;
static void thingsos_board_trace_mark(unsigned mark){host_trace_mark=mark;}
static int thingsos_board_present_lock_transition(const thingsos_canvas_t* c,bool forward){
    (void)c;++host_lock_presents;host_lock_forward=forward;return host_lock_result;
}
static void (*host_scan_barrier)(void);
static unsigned host_cleanup_requests;
static int host_cleanup_x,host_cleanup_y,host_cleanup_w,host_cleanup_h;
static void thingsos_board_menu_refresh(void) {}
static void thingsos_board_cleanup_area(int x,int y,int w,int h) {
    ++host_cleanup_requests;host_cleanup_x=x;host_cleanup_y=y;host_cleanup_w=w;host_cleanup_h=h;
}
static void thingsos_board_scan_barrier(void (*barrier)(void)) {host_scan_barrier=barrier;}
static int thingsos_board_present_reader_stagger(const thingsos_canvas_t* c,bool forward) {
    (void)c;(void)forward;return 0;
}
static int thingsos_board_present_reader_image(const thingsos_canvas_t* c) {
    (void)c;return 0;
}

static bool thingsos_network_job(thingsos_manage_job_t* p) {(void)p;return false;}
static void thingsos_network_job_done(unsigned a,unsigned b,unsigned c) {(void)a;(void)b;(void)c;}
static thingsos_transfer_state_t host_transfer;
static void thingsos_network_transfer(thingsos_transfer_state_t* s) {*s=host_transfer;}
static bool thingsos_network_cancel_upload(void){if(host_transfer.ota)return false;host_transfer.active=false;host_network_busy=false;return true;}
static const char* thingsos_network_failure(void) {return "";}

static const char* thingsos_board_charge(void) {return "未充电";}
static bool host_charging;
static bool thingsos_board_charging(void) {return host_charging;}
static int host_battery=100;
static int thingsos_board_battery(void) {return host_battery;}

static void thingsos_network_maintenance(void) {}
static char host_saved_wifi[33];
static const char* thingsos_network_saved_name(void) {return host_saved_wifi;}

static bool thingsos_network_recover_files(void) {return true;}

static bool thingsos_network_pause_requested(void) {return false;}
static void thingsos_network_pause_complete(bool ok) {(void)ok;}
static void thingsos_network_card(uint64_t id) {(void)id;}

static bool host_weidu_background_ready=true;
static unsigned host_weidu_cover_calls;
static bool thingsos_weidu_background_ready(void) {return host_weidu_background_ready;}
static void thingsos_weidu_device_covers(const thingsos_weidu_book_t* books,size_t count) {
    (void)books;
    (void)count;
    ++host_weidu_cover_calls;
}

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
static bool host_wd_scheduled;
static thingsos_wd_upload_t host_wd_scheduled_upload;
static struct {
    char id[64],owner[33];
    thingsos_wd_sync_t sync;
    bool valid;
} host_wd_sync[4];
static bool host_wd_queue_conflict;
static thingsos_wd_progress_t host_wd_queue_cloud;
static void host_wd_sync_reset(void) {
    memset(host_wd_sync,0,sizeof(host_wd_sync));
    host_wd_queue_conflict=false;
    memset(&host_wd_queue_cloud,0,sizeof(host_wd_queue_cloud));
}
static bool thingsos_wd_device_sync_load(const char* id,const char* owner,thingsos_wd_sync_t* out) {
    if(!out)return false;
    memset(out,0,sizeof(*out));
    const char* resolved=(owner && *owner)?owner:host_wd_reading.owner;
    if(!id || !*id || !resolved || !*resolved)return false;
    for(unsigned i=0;i<4;++i)if(host_wd_sync[i].valid && !strcmp(host_wd_sync[i].id,id) && !strcmp(host_wd_sync[i].owner,resolved)) {
        *out=host_wd_sync[i].sync;return true;
    }
    return false;
}
static bool thingsos_wd_device_sync_save(const char* id,const char* owner,const thingsos_wd_sync_t* in) {
    const char* resolved=(owner && *owner)?owner:host_wd_reading.owner;
    if(!id || !*id || !resolved || !*resolved || !in)return false;
    int slot=-1;
    for(unsigned i=0;i<4;++i) {
        if(host_wd_sync[i].valid && !strcmp(host_wd_sync[i].id,id) && !strcmp(host_wd_sync[i].owner,resolved)){slot=(int)i;break;}
        if(!host_wd_sync[i].valid && slot<0)slot=(int)i;
    }
    if(slot<0)slot=0;
    host_wd_sync[slot].valid=true;
    strlcpy(host_wd_sync[slot].id,id,sizeof(host_wd_sync[slot].id));
    strlcpy(host_wd_sync[slot].owner,resolved,sizeof(host_wd_sync[slot].owner));
    host_wd_sync[slot].sync=*in;
    return true;
}
static bool thingsos_wd_device_schedule(const thingsos_wd_upload_t* in) {
    host_wd_scheduled=true;host_wd_scheduled_upload=*in;return true;
}
static void thingsos_wd_device_poll(bool idle) {
    if(idle && host_wd_scheduled && !host_weidu.busy) {
        host_wd_scheduled=false;
        if(host_wd_queue_conflict) {
            thingsos_wd_sync_t sync={0};
            thingsos_wd_device_sync_load(host_wd_scheduled_upload.id,host_wd_reading.owner,&sync);
            if(thingsos_wd_progress_compare(&host_wd_queue_cloud,NULL,0,false,sync.local_updated,&sync)==WD_SYNC_CLOUD_NEWER) {
                sync.conflict_pending=true;sync.conflict_cloud=host_wd_queue_cloud;
                thingsos_wd_device_sync_save(host_wd_scheduled_upload.id,host_wd_reading.owner,&sync);
                strlcpy(host_wd_reading.book,host_wd_scheduled_upload.id,sizeof(host_wd_reading.book));
                host_wd_reading.action=WD_DEVICE_UPLOAD;host_wd_reading.pull=WD_OK;host_wd_reading.progress=WD_OK;
                host_wd_reading.cloud=host_wd_queue_cloud;host_wd_reading.conflict=true;++host_wd_reading.serial;
                return;
            }
        }
        thingsos_wd_device_request(WD_DEVICE_UPLOAD,host_wd_scheduled_upload.id,&host_wd_scheduled_upload);
    }
}
static int host_wd_cloud_chapter=-1;
static size_t host_wd_cloud_at=0;
static int host_wd_chapter_stub(const char* root,const char* id,const char* uid) {
    if(host_wd_cloud_chapter>=0)return host_wd_cloud_chapter;
    return thingsos_wd_chapter(root,id,uid);
}
static bool host_wd_local_stub(const thingsos_wd_map_t* map,uint32_t offset,size_t* at) {
    if(host_wd_cloud_chapter>=0){*at=host_wd_cloud_at;return true;}
    return thingsos_wd_local(map,offset,at);
}
#define thingsos_wd_chapter host_wd_chapter_stub
#define thingsos_wd_local host_wd_local_stub

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
