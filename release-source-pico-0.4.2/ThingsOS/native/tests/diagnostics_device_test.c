// SPDX-License-Identifier: Apache-2.0
#define THINGSOS_DIAG_TEST 1
#include <assert.h>
#include "../firmware/components/thingsos_board/diagnostics_device.c"
static void reboot(unsigned reset,bool rescue) {
    free(journal_data);journal_data=NULL;journal_lock=NULL;persist_lock=NULL;
    memset(&summary,0,sizeof(summary));
    started=sd_loaded=false;saved_sequence=nvs_sequence=0;sd_at=nvs_at=0;sd_slot=0;
    host_reset=reset;host_time+=1000000;
    host_running.subtype=rescue?0:16;strcpy(host_running.label,rescue?"rescue":"ota_0");
    thingsos_diag_start();
}
static bool collect(void* raw,const char* data,size_t count) {
    char* out=raw;size_t used=strlen(out);
    if(used+count>=4096)return false;
    memcpy(out+used,data,count);out[used+count]=0;return true;
}
int main(int argc,char** argv) {
    assert(argc==2);host_diag_dir=argv[1];_mkdir(host_diag_dir);
    for(unsigned i=0;i<2;++i){char p[256];snprintf(p,sizeof(p),"%s/events-%u.bin",host_diag_dir,i);remove(p);}
    reboot(ESP_RST_POWERON,false);assert(host_writes==0);
    thingsos_diag_poll(true);assert(host_writes==0); /* mount pending */
    host_media=ESP_OK;host_mounted=true;
    thingsos_diag_poll(true);thingsos_diag_health();assert(host_writes==0);
    for(unsigned i=0;i<20;++i)thingsos_diag_event(DIAG_NET_HTTP,1,i,503,-1,110);
    thingsos_diag_reason(DIAG_REASON_MANUAL,0);thingsos_diag_poll(true);assert(host_writes==0);
    memset(&retained,0,sizeof(retained)); /* power loss must recover from SD */
    reboot(ESP_RST_POWERON,true);thingsos_diag_poll(true);
    assert(journal.boot_count>=2 && journal.boots[journal.boot_count-1].reason==DIAG_REASON_MANUAL);
    assert(journal.boots[journal.boot_count-2].healthy && !strcmp(journal.boots[journal.boot_count-2].running,"ota_0"));
    assert(journal.event_count>20 && host_writes==0);
    thingsos_diag_reason(DIAG_REASON_INSTALL_FAILED,-1);thingsos_diag_poll(true);assert(host_writes==0);
    host_mounted=false;host_media=ESP_FAIL;host_time+=1000000;thingsos_diag_poll(false);assert(host_writes==1);
    unsigned writes=host_writes;
    for(unsigned i=0;i<100;++i){thingsos_diag_event(DIAG_TASK,1,i,0,0,0);thingsos_diag_poll(false);}
    assert(host_writes==writes); /* events never commit flash individually */
    thingsos_diag_event(DIAG_FAILURE,3,12,313,-7,28);
    reboot(2,false);assert(summary.fault.code==-7); /* RTC keeps most recent fault */
    thingsos_diag_health();assert(host_writes==writes+1);
    memset(&retained,0,sizeof(retained));reboot(ESP_RST_POWERON,false);assert(journal.boot_count==DIAG_BOOTS);
    assert(journal.boots[journal.boot_count-2].healthy);
    thingsos_boot_guard_t g={0x54484731u,3,~3u};memcpy(host_rtc.custom,&g,sizeof(g));
    reboot(2,true);assert(!strcmp(thingsos_diag_reason_text(),"连续启动未确认"));
    uint32_t main_id=journal.last_main.id;assert(main_id);
    for(unsigned i=0;i<6;++i)reboot(2,true);
    assert(journal.last_main.id==main_id && !strcmp(journal.last_main.running,"ota_0"));
    host_nvs_fail=true;thingsos_diag_health();assert(journal.boots[journal.boot_count-1].healthy);
    for(unsigned i=0;i<100;++i)thingsos_diag_event(DIAG_PAGE,0,i,i+1,0,0);
    thingsos_diag_journal_t* exported=snapshot();assert(exported && thingsos_diag_valid(exported));
    bool fault_found=false;
    for(unsigned i=0;i<exported->event_count;++i)if(exported->events[i].kind==DIAG_FAILURE)fault_found=true;
    assert(fault_found);free(exported);
    host_no_psram=true;host_nvs_fail=false;
    reboot(2,true);assert(journal_data==NULL);
    thingsos_diag_event(DIAG_FAILURE,6,12,0,-42,0);
    thingsos_diag_poll(true);
    char json[4096]={0};
    assert(thingsos_diag_export(collect,json) && strstr(json,"\"code\":-42"));
    assert(strstr(json,"\"running\":\"rescue\""));
    free(journal_data);
    puts("PASS: SD-only durability, pending-mount deferral, SD power-loss history, no-card bounded NVS, RTC fault, health, rescue reason");return 0;
}
