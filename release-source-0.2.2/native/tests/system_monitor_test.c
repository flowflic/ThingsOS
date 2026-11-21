/* SPDX-License-Identifier: Apache-2.0 */
#include "thingsos/system_monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"System metrics line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int64_t test_now;
static int test_pclk=20;
static int64_t esp_timer_get_time(void){return test_now;}
static int metalio_epd_pclk_mhz(void){return test_pclk;}
typedef struct {uint32_t count;uint64_t last_seconds;bool during_clear;} EpdUnderrunStats;
static EpdUnderrunStats test_faults;
static EpdUnderrunStats epd_underrun_stats(void){return test_faults;}
#include "../firmware/components/thingsos_board/display_stats.inc"
static void display_stats_test(void) {
    thingsos_system_display_t s;thingsos_board_display_stats(&s);
    CHECK(s.pclk_mhz==20&&!s.refresh_valid);
    test_faults=(EpdUnderrunStats){.count=4,.last_seconds=91,.during_clear=true};display_last_failed=true;
    thingsos_board_display_stats(&s);
    CHECK(s.underrun_count==4&&s.underrun_last_seconds==91&&s.underrun_during_clear&&s.last_present_failed);
    display_last_failed=false;
    thingsos_board_display_stats(&s);CHECK(s.underrun_count==4&&!s.last_present_failed);
    test_now=1312000;display_record_success(1000000);
    thingsos_board_display_stats(&s);CHECK(s.refresh_valid&&s.last_refresh_ms==312);
    test_pclk=10;thingsos_board_display_stats(&s);CHECK(s.pclk_mhz==10&&s.last_refresh_ms==312);
    display_record_success(test_now+1);thingsos_board_display_stats(&s);CHECK(s.last_refresh_ms==312);
    display_record_success(test_now);thingsos_board_display_stats(&s);CHECK(s.refresh_valid&&!s.last_refresh_ms);
    test_now=((int64_t)UINT32_MAX+1)*1000;display_record_success(0);
    thingsos_board_display_stats(&s);CHECK(s.last_refresh_ms==UINT32_MAX);
    test_pclk=0;thingsos_board_display_stats(&s);CHECK(!s.pclk_mhz);
    thingsos_board_display_stats(NULL);
}
int main(void) {
    display_stats_test();
    thingsos_system_cpu_window_t w={0};uint16_t used=999;
    uint64_t t[2]={1000000,1000100},i[2]={500000,500000};
    CHECK(!thingsos_system_cpu_step(&w,t,i,&used));
    t[0]+=2000000;t[1]+=2000000;i[0]+=2000000;
    CHECK(thingsos_system_cpu_step(&w,t,i,&used)&&used==5000); /* One idle, one busy. */
    t[0]+=100000;t[1]+=100000;
    CHECK(thingsos_system_cpu_step(&w,t,i,&used)&&used==5000);
    t[0]+=1900000;t[1]+=3900000;i[0]+=1000000;i[1]+=1000000;
    CHECK(thingsos_system_cpu_step(&w,t,i,&used)&&used==6250); /* Per-core normalization. */
    t[0]+=2000000;t[1]+=2000000;i[0]+=2000000;i[1]+=2000000;
    CHECK(thingsos_system_cpu_step(&w,t,i,&used)&&used==0);
    t[0]+=2000000;t[1]+=2000000;
    CHECK(thingsos_system_cpu_step(&w,t,i,&used)&&used==10000);
    t[0]+=11000000;t[1]+=11000000;CHECK(!thingsos_system_cpu_step(&w,t,i,&used));
    t[0]--;CHECK(!thingsos_system_cpu_step(&w,t,i,&used));
    t[0]+=2000000;t[1]+=2000000;i[0]=1;CHECK(!thingsos_system_cpu_step(&w,t,i,&used));
    w=(thingsos_system_cpu_window_t){0};
    t[0]=t[1]=UINT32_MAX-1000000ULL;i[0]=i[1]=UINT32_MAX-1500000ULL;
    CHECK(!thingsos_system_cpu_step(&w,t,i,&used));
    t[0]+=2000000;t[1]+=2000000;i[0]+=1500000;i[1]+=1500000;
    CHECK(thingsos_system_cpu_step(&w,t,i,&used)&&used==2500);
    thingsos_system_memory_t m;
    CHECK(thingsos_system_flash_layout(16ULL<<20,0xae0000,6905840,&m));
    CHECK(m.valid&&m.total==(16ULL<<20)&&m.free==0xae0000-6905840);
    CHECK(!thingsos_system_flash_layout(1,2,1,&m)&&!m.valid);
    CHECK(!thingsos_system_flash_layout(16,12,13,&m));
    CHECK(!thingsos_system_flash_layout(16,12,0,&m));
    CHECK(thingsos_system_ratio(50,100,608)==304);
    CHECK(thingsos_system_ratio(150,100,608)==608);
    CHECK(thingsos_system_ratio(0,0,608)==0);
    CHECK(thingsos_system_ratio(UINT64_MAX-1,UINT64_MAX,0)==0);
    CHECK(thingsos_system_ratio(UINT64_MAX/2,UINT64_MAX,100)==49);
    CHECK(thingsos_system_ratio(UINT64_MAX-1,UINT64_MAX,UINT_MAX)==UINT_MAX-1);
    puts("PASS: per-core CPU windows, 64-bit continuity, image/reserved Flash accounting and bounded bar ratios");return 0;
}
