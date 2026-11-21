// SPDX-License-Identifier: Apache-2.0
#include "thingsos/diagnostics.h"
#include "thingsos/guard.h"
#include <time.h>
#ifdef THINGSOS_DIAG_TEST
#include "diagnostics_stubs.h"
#else
#include "board.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "bootloader_common.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_sleep.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif
#ifndef DIAG_STORAGE_DIR
#define DIAG_STORAGE_DIR "/sdcard/.thingsos-diagnostics"
#endif
#define SUMMARY_MAGIC 0x44495331u
typedef struct {
    uint32_t magic, sequence, count, next_reason;
    thingsos_diag_boot_t boots[DIAG_BOOTS];
    thingsos_diag_boot_t last_main;
    thingsos_diag_event_t last, fault;
    uint32_t crc;
} summary_t;
/* Survives software/watchdog resets; power loss relies on the NVS checkpoint.
   Do not install a panic handler or write a filesystem from panic context. */
static RTC_NOINIT_ATTR summary_t retained;
/* ~21 KB: in PSRAM (no internal/DMA SRAM). Guarded by a mutex, not a
   spinlock: pushes shift the event array and snapshots copy it, which must not
   run with interrupts off (LCD EOF refills tolerate only ~270 us). Every
   caller runs in task context. */
static EXT_RAM_BSS_ATTR thingsos_diag_journal_t journal;
static summary_t summary;
static SemaphoreHandle_t journal_lock;
static void journal_take(void){if(journal_lock)xSemaphoreTake(journal_lock,portMAX_DELAY);}
static void journal_give(void){if(journal_lock)xSemaphoreGive(journal_lock);}
static bool started,sd_loaded;
static unsigned sd_slot;
static SemaphoreHandle_t persist_lock;
static uint32_t saved_sequence,nvs_sequence;
static int64_t sd_at,nvs_at;
static int storage_error;
static bool summary_valid(const summary_t* s) {
    return s->magic==SUMMARY_MAGIC && s->count>0 && s->count<=DIAG_BOOTS &&
        s->crc==thingsos_diag_crc(s,offsetof(summary_t,crc));
}
static void retain_locked(void) {
    summary.magic=SUMMARY_MAGIC;summary.sequence=journal.sequence;summary.count=journal.boot_count;
    summary.next_reason=journal.next_reason;
    memcpy(summary.boots,journal.boots,sizeof(summary.boots));
    if(journal.boot_count && !strcmp(journal.boots[journal.boot_count-1].running,"ota_0"))journal.last_main=journal.boots[journal.boot_count-1];
    summary.last_main=journal.last_main;
    summary.crc=thingsos_diag_crc(&summary,offsetof(summary_t,crc));retained=summary;
}
static void checkpoint(void) {
    /* A mounted SD is the sole durable diagnostic store. While mount is in
       progress, retain in RTC and wait; never treat NOT_FINISHED as no card. */
    thingsos_sd_info_t media={0};esp_err_t ready=thingsos_board_sd_info(&media);
    if(ready==ESP_ERR_NOT_FINISHED || (ready==ESP_OK && media.mounted))return;
    if(!persist_lock || xSemaphoreTake(persist_lock,pdMS_TO_TICKS(1000))!=pdTRUE)return;
    summary_t* s=malloc(sizeof(*s));if(!s){storage_error=ESP_ERR_NO_MEM;xSemaphoreGive(persist_lock);return;}
    journal_take();retain_locked();*s=summary;journal_give();
    if(s->sequence==nvs_sequence){free(s);xSemaphoreGive(persist_lock);return;}
    nvs_handle_t h;esp_err_t e=nvs_open("thingsos-diag",NVS_READWRITE,&h);
    if(e==ESP_OK){e=nvs_set_blob(h,"summary",s,sizeof(*s));if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);}
    if(e==ESP_OK)nvs_sequence=s->sequence;
    storage_error=e;free(s);nvs_at=esp_timer_get_time();xSemaphoreGive(persist_lock);
}
void thingsos_diag_start(void) {
    if(started)return;
    persist_lock=xSemaphoreCreateMutex();
    journal_lock=xSemaphoreCreateMutex();
    esp_err_t e=nvs_flash_init(); /* Never erase NVS to make diagnostics work. */
    summary_t* disk=calloc(1,sizeof(*disk));nvs_handle_t h;
    if(disk && e==ESP_OK && nvs_open("thingsos-diag",NVS_READONLY,&h)==ESP_OK) {
        size_t n=sizeof(*disk);if(nvs_get_blob(h,"summary",disk,&n)!=ESP_OK || n!=sizeof(*disk))memset(disk,0,sizeof(*disk));nvs_close(h);
    }
    bool rtc_ok=esp_reset_reason()!=ESP_RST_POWERON && summary_valid(&retained);
    if(rtc_ok && (!disk || !summary_valid(disk) || retained.sequence>=disk->sequence))summary=retained;
    else if(disk && summary_valid(disk))summary=*disk;
    free(disk);
    unsigned reason=summary.next_reason;
    journal.boot_count=summary.count;memcpy(journal.boots,summary.boots,sizeof(journal.boots));
    journal.last_main=summary.last_main;
    journal.sequence=summary.sequence;
    if(summary.fault.kind)thingsos_diag_push(&journal,summary.fault);
    if(summary.last.kind && memcmp(&summary.last,&summary.fault,sizeof(summary.last)))thingsos_diag_push(&journal,summary.last);
    uint32_t id=journal.boot_count?journal.boots[journal.boot_count-1].id+1:1;
    if(journal.boot_count==DIAG_BOOTS){memmove(journal.boots,journal.boots+1,(DIAG_BOOTS-1)*sizeof(journal.boots[0]));--journal.boot_count;}
    thingsos_diag_boot_t* b=&journal.boots[journal.boot_count++];memset(b,0,sizeof(*b));b->id=id;
    const esp_app_desc_t* app=esp_app_get_description();
    snprintf(b->version,sizeof(b->version),"%.31s",app->version);
    for(unsigned i=0;i<32;++i)snprintf(b->elf+2*i,3,"%02x",app->app_elf_sha256[i]);
    const esp_partition_t* running=esp_ota_get_running_partition(),*selected=esp_ota_get_boot_partition();
    snprintf(b->running,sizeof(b->running),"%.16s",running?running->label:"");
    snprintf(b->selected,sizeof(b->selected),"%.16s",selected?selected->label:"");b->reset=esp_reset_reason();
    thingsos_boot_guard_t boot;memcpy(&boot,bootloader_common_get_rtc_retain_mem()->custom,sizeof(boot));
    b->attempts=boot.magic==0x54484731u && boot.complement==~boot.attempts && boot.attempts<=3?boot.attempts:0;
    if(running && running->subtype==ESP_PARTITION_SUBTYPE_APP_FACTORY)
        b->reason=b->attempts>=3?DIAG_REASON_GUARD:reason;
    summary.next_reason=0;started=true;
    thingsos_diag_event(DIAG_BOOT,0,b->reset,b->attempts,0,0);
}
void thingsos_diag_event(unsigned kind,unsigned stage,unsigned a,unsigned b,int code,int error) {
    if(!started)return;
    thingsos_diag_event_t e={.ms=(uint32_t)(esp_timer_get_time()/1000),.kind=kind,.stage=stage,.a=a,.b=b,.code=code,.error=error};
    time_t stamp=time(NULL);if(stamp>=1577836800 && (uint64_t)stamp<=4102444799u)e.utc=(uint32_t)stamp;
    journal_take();e.boot=journal.boots[journal.boot_count-1].id;
    thingsos_diag_push(&journal,e);summary.last=e;
    if(kind==DIAG_FAILURE || (code && kind!=DIAG_NET_METRIC && kind!=DIAG_NET_WIFI))summary.fault=e;
    retain_locked();journal_give();
}
void thingsos_diag_health(void) {
    if(!started)return;
    journal_take();journal.boots[journal.boot_count-1].healthy=1;journal_give();
    thingsos_diag_event(DIAG_HEALTH,0,0,0,0,0);checkpoint();
}
void thingsos_diag_reason(unsigned reason,int code) {
    if(!started)return;
    const esp_partition_t* selected=esp_ota_get_boot_partition();
    journal_take();
    if(selected)snprintf(journal.boots[journal.boot_count-1].selected,17,"%.16s",selected->label);
    if(reason==DIAG_REASON_MANUAL || reason==DIAG_REASON_UPDATE)journal.next_reason=reason;
    else if(reason==DIAG_REASON_INSTALL_FAILED)journal.boots[journal.boot_count-1].reason=reason;
    journal_give();
    thingsos_diag_event(DIAG_RESCUE,reason,0,0,code,0);checkpoint();
}
static thingsos_diag_journal_t* snapshot(void) {
    thingsos_diag_journal_t* j=heap_caps_malloc(sizeof(*j),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!j)j=malloc(sizeof(*j));
    if(!j)return NULL;
    thingsos_diag_event_t fault;
    journal_take();*j=journal;fault=summary.fault;journal_give();
    bool found=false;
    for(unsigned i=0;i<j->event_count;++i)if(!memcmp(j->events+i,&fault,sizeof(fault)))found=true;
    /* Preserve the core fault even after normal activity fills the live ring.
       Consumers order by boot/ms, not by array position. */
    if(fault.kind && !found){uint32_t sequence=j->sequence;thingsos_diag_push(j,fault);j->sequence=sequence;}
    thingsos_diag_seal(j);return j;
}
/* Caller owns SD/UI lock and excludes USB disk/OTA access. Two CRC snapshots
   bound storage and leave one complete generation after an interrupted write. */
void thingsos_diag_poll(bool force) {
    if(!started)return;
    int64_t now=esp_timer_get_time();
    thingsos_sd_info_t sd={0};esp_err_t media_ready=thingsos_board_sd_info(&sd);
    if(media_ready==ESP_ERR_NOT_FINISHED)return;
    bool mounted=media_ready==ESP_OK && sd.mounted;
    if(!mounted)sd_loaded=false;
    if(mounted && !sd_loaded) {
        thingsos_diag_journal_t* old=heap_caps_malloc(sizeof(*old),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        thingsos_diag_journal_t* best=heap_caps_malloc(sizeof(*best),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!old || !best){free(old);free(best);storage_error=ESP_ERR_NO_MEM;return;}
        bool found=false;
        for(unsigned i=0;old && best && i<2;++i) {
            char path[256];snprintf(path,sizeof(path),"%s/events-%u.bin",DIAG_STORAGE_DIR,i);
            FILE* f=fopen(path,"rb");bool valid=f && fread(old,1,sizeof(*old),f)==sizeof(*old) && thingsos_diag_valid(old);if(f)fclose(f);
            if(valid && (!found || old->sequence>best->sequence)){*best=*old;found=true;sd_slot=i^1;}
        }
        if(found) {
            journal_take();
            /* Keep the boot's live breadcrumbs; import older events only. */
            *old=journal;journal.event_count=0;journal.failure_count=0;
            thingsos_diag_boot_t current_boot=journal.boots[journal.boot_count-1];
            uint32_t prior_id=current_boot.id;
            if(best->boot_count && best->boots[best->boot_count-1].id>=current_boot.id) {
                current_boot.id=best->boots[best->boot_count-1].id+1;
                journal.boot_count=best->boot_count;
                if(journal.boot_count==DIAG_BOOTS)--journal.boot_count;
                unsigned skip=best->boot_count-journal.boot_count;
                memcpy(journal.boots,best->boots+skip,journal.boot_count*sizeof(journal.boots[0]));
                journal.boots[journal.boot_count++]=current_boot;
            }
            thingsos_diag_boot_t* current_record=&journal.boots[journal.boot_count-1];
            if(best->last_main.id>journal.last_main.id)journal.last_main=best->last_main;
            if(!strcmp(current_record->running,"rescue") && !current_record->reason)
                current_record->reason=best->next_reason;
            if(journal.sequence<best->sequence)journal.sequence=best->sequence;
            uint32_t current=current_record->id;
            for(unsigned i=0;i<best->event_count;++i)if(best->events[i].boot<current) {
                thingsos_diag_event_t e=best->events[i];thingsos_diag_push(&journal,e);
                if((e.kind==DIAG_FAILURE || (e.code && e.kind!=DIAG_NET_METRIC && e.kind!=DIAG_NET_WIFI)) &&
                   (e.boot>summary.fault.boot || (e.boot==summary.fault.boot && e.ms>=summary.fault.ms)))summary.fault=e;
            }
            for(unsigned i=0;i<best->failure_count;++i)if(best->failures[i].boot<current)
                thingsos_diag_keep_failure(&journal,best->failures[i]);
            for(unsigned i=0;i<old->failure_count;++i) {
                if(old->failures[i].boot==prior_id)old->failures[i].boot=current;
                thingsos_diag_keep_failure(&journal,old->failures[i]);
            }
            for(unsigned i=0;i<old->event_count;++i) {
                if(old->events[i].boot==prior_id)old->events[i].boot=current;
                bool duplicate=false;
                for(unsigned k=0;k<journal.event_count;++k)if(!memcmp(journal.events+k,old->events+i,sizeof(old->events[i])))duplicate=true;
                if(!duplicate)thingsos_diag_push(&journal,old->events[i]);
            }
            retain_locked();journal_give();
        }
        free(old);free(best);sd_loaded=true;
    }
    uint32_t sequence;journal_take();sequence=journal.sequence;journal_give();
    if(mounted && sequence==saved_sequence)return;
    if(mounted && (force || now-sd_at>=30000000)) {
        thingsos_diag_journal_t* j=snapshot();bool ok=false;
        if(j) {
            mkdir(DIAG_STORAGE_DIR,0777);
            /* Alternate by successful checkpoint, not by event parity. */
            char path[256];snprintf(path,sizeof(path),"%s/events-%u.bin",DIAG_STORAGE_DIR,sd_slot);
            FILE* f=fopen(path,"wb");ok=f && fwrite(j,1,sizeof(*j),f)==sizeof(*j) && fflush(f)==0;
            if(f && fclose(f))ok=false;
            if(ok){sd_slot^=1;saved_sequence=j->sequence;}free(j);
        }
        storage_error=ok?0:ESP_FAIL;sd_at=now;
        if(!ok)thingsos_diag_event(DIAG_FAILURE,4,1,0,ESP_FAIL,0);
    }
    /* No SD: persist only the small summary, at most once per five minutes.
       Boot/healthy/explicit rescue transitions have their own checkpoints. */
    if(!mounted && (force || !nvs_at || now-nvs_at>=300000000))checkpoint();
}
bool thingsos_diag_export(thingsos_diag_sink sink,void* ctx) {
    thingsos_diag_journal_t* j=snapshot();if(!j)return false;
    bool ok=thingsos_diag_dump(j,sink,ctx);free(j);return ok;
}
const char* thingsos_diag_reason_text(void) {
    unsigned r=started?journal.boots[journal.boot_count-1].reason:0;
    switch(r){case DIAG_REASON_GUARD:return "连续启动未确认";case DIAG_REASON_MANUAL:return "手动进入";
        case DIAG_REASON_UPDATE:return "固件更新安装";case DIAG_REASON_INSTALL_FAILED:return "更新安装失败";default:return "原因未记录";}
}
void thingsos_diag_summary(char* out,size_t cap) {
    if(!started){snprintf(out,cap,"日志尚未初始化");return;}
    journal_take();thingsos_diag_boot_t b=journal.boots[journal.boot_count-1];unsigned n=journal.event_count;journal_give();
    snprintf(out,cap,"启动 %u · 复位 %u · %s · 事件 %u%s",(unsigned)b.id,(unsigned)b.reset,b.healthy?"已确认":"待确认",n,storage_error?" · 保存失败":"");
}
