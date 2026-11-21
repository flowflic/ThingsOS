// SPDX-License-Identifier: Apache-2.0
#include "thingsos/alarm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"alarm line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static time_t clock_at(int month,int day,int hour,int minute,int second){
    struct tm date={0};date.tm_year=126;date.tm_mon=month-1;date.tm_mday=day;
    date.tm_hour=hour;date.tm_min=minute;date.tm_sec=second;date.tm_isdst=-1;
    return mktime(&date);
}
static time_t year_at(int year,int month,int day,int hour,int minute,int second){
    struct tm date={0};date.tm_year=year-1900;date.tm_mon=month-1;date.tm_mday=day;
    date.tm_hour=hour;date.tm_min=minute;date.tm_sec=second;date.tm_isdst=-1;return mktime(&date);
}
int main(void){
    /* Civil-time policy deliberately independent of an internet calendar. */
#ifdef _WIN32
    _putenv_s("TZ","CST-8");_tzset();
#else
    setenv("TZ","CST-8",1);tzset();
#endif
    thingsos_alarm_t a={.enabled=1,.hour=7,.minute=30,.days=THINGSOS_ALARM_DAILY,.volume=60};
    strcpy(a.music,"morning.MP3");CHECK(thingsos_alarm_valid(&a));
    time_t monday=clock_at(10,26,7,30,0),friday=clock_at(10,30,7,30,0),saturday=clock_at(10,31,7,30,0),sunday=clock_at(11,1,7,30,0);
    CHECK(thingsos_alarm_due(&a,monday-2,monday)==20261026);
    CHECK(!thingsos_alarm_due(&a,monday-20,monday-1));
    CHECK(thingsos_alarm_due(&a,0,monday+59)==20261026);
    CHECK(!thingsos_alarm_due(&a,0,monday+60));
    CHECK(thingsos_alarm_due(&a,monday-2,monday+299)==20261026);
    CHECK(!thingsos_alarm_due(&a,monday-2,monday+301));
    CHECK(!thingsos_alarm_due(&a,monday,monday+1));
    a.last_day=20261026;CHECK(!thingsos_alarm_due(&a,0,monday));
    CHECK(!thingsos_alarm_due(&a,monday+10,monday)); /* backwards correction */
    CHECK(thingsos_alarm_due(&a,friday-1,friday)==20261030);
    a.days=THINGSOS_ALARM_WEEKDAYS;
    CHECK(thingsos_alarm_due(&a,0,friday)==20261030);
    CHECK(!thingsos_alarm_due(&a,0,saturday));CHECK(!thingsos_alarm_due(&a,0,sunday));
    a.days=THINGSOS_ALARM_WEEKEND;
    CHECK(!thingsos_alarm_due(&a,0,friday));CHECK(thingsos_alarm_due(&a,0,saturday)==20261031);
    CHECK(thingsos_alarm_due(&a,0,sunday)==20261101);
    a.days=1u<<1;CHECK(!thingsos_alarm_due(&a,0,friday));a.last_day=0;CHECK(thingsos_alarm_due(&a,0,monday));
    a.hour=23;a.minute=59;a.days=127;
    CHECK(thingsos_alarm_due(&a,clock_at(10,31,23,58,59),clock_at(11,1,0,0,2))==20261031);
    a.hour=0;a.minute=0;CHECK(thingsos_alarm_due(&a,clock_at(10,31,23,59,59),clock_at(11,1,0,0,2))==20261101);
    a.enabled=0;CHECK(!thingsos_alarm_due(&a,0,sunday));a.enabled=1;
    a.hour=24;CHECK(!thingsos_alarm_valid(&a));a.hour=7;a.minute=60;CHECK(!thingsos_alarm_valid(&a));a.minute=30;
    a.days=128;CHECK(!thingsos_alarm_valid(&a));a.days=127;
    a.volume=0;CHECK(!thingsos_alarm_valid(&a));a.volume=60;
    strcpy(a.music,"../bad.mp3");CHECK(!thingsos_alarm_valid(&a));strcpy(a.music,"dir/bad.wav");CHECK(!thingsos_alarm_valid(&a));
    strcpy(a.music,"x.flac");CHECK(!thingsos_alarm_valid(&a));memset(a.music,'x',sizeof(a.music));CHECK(!thingsos_alarm_valid(&a));
    strcpy(a.music,"birds.wav");CHECK(thingsos_alarm_valid(&a));CHECK(!thingsos_alarm_due(&a,0,1));
    a=(thingsos_alarm_t){.enabled=1,.hour=7,.minute=30,.days=127,.volume=60,
        .start_day=20261030,.end_day=20261101};strcpy(a.music,"birds.wav");
    CHECK(thingsos_alarm_valid(&a));
    CHECK(!thingsos_alarm_due(&a,0,monday));
    CHECK(thingsos_alarm_due(&a,0,friday)==20261030);
    CHECK(thingsos_alarm_due(&a,0,sunday)==20261101);
    CHECK(!thingsos_alarm_due(&a,0,clock_at(11,2,7,30,0)));
    a.days=62;CHECK(!thingsos_alarm_due(&a,0,sunday));
    a.start_day=20261102;CHECK(!thingsos_alarm_valid(&a));
    a.start_day=20260229;CHECK(!thingsos_alarm_valid(&a));
    a.start_day=20240229;a.end_day=20240229;a.days=127;
    CHECK(thingsos_alarm_valid(&a));CHECK(thingsos_alarm_due(&a,0,year_at(2024,2,29,7,30,0))==20240229);
    a.start_day=a.end_day=20261231;a.hour=23;a.minute=59;
    CHECK(thingsos_alarm_due(&a,year_at(2026,12,31,23,58,59),year_at(2027,1,1,0,2,0))==20261231);
    CHECK(thingsos_alarm_expired(&a,year_at(2027,1,1,0,0,0)));
    a.mode=THINGSOS_ALARM_ONCE;a.days=0;
    CHECK(thingsos_alarm_used(&a)&&thingsos_alarm_valid(&a));
    CHECK(thingsos_alarm_due(&a,0,year_at(2026,12,31,23,59,1))==20261231);
    CHECK(!thingsos_alarm_due(&a,0,year_at(2027,1,1,23,59,1)));
    a.last_day=20261231;CHECK(!thingsos_alarm_due(&a,0,year_at(2026,12,31,23,59,1)));
    CHECK(thingsos_alarm_expired(&a,year_at(2026,12,31,23,59,1)));
    a.last_day=0;a.start_day=a.end_day=20261031;a.hour=7;a.minute=30;
    CHECK(!thingsos_alarm_expired(&a,saturday+300));CHECK(thingsos_alarm_expired(&a,saturday+301));
    a.end_day=20261101;CHECK(!thingsos_alarm_valid(&a));a.end_day=a.start_day;
    a.days=1;CHECK(!thingsos_alarm_valid(&a));a.days=0;
    a.mode=2;CHECK(!thingsos_alarm_valid(&a));
    thingsos_alarm_v1_t old={.enabled=1,.hour=7,.minute=30,.days=62,.volume=60,.last_day=20261026};
    strcpy(old.music,"birds.wav");
    CHECK(sizeof(old)==204 && sizeof(thingsos_alarm_t)==212);
    CHECK(thingsos_alarm_from_v1(&old,&a));
    CHECK(a.mode==THINGSOS_ALARM_REPEAT&&a.start_day==0&&a.end_day==0&&a.last_day==20261026&&a.days==62);
    CHECK(thingsos_alarm_due(&a,0,friday)==20261030);
    old.reserved[1]=1;CHECK(!thingsos_alarm_from_v1(&old,&a));
    memset(&old,0,sizeof(old));CHECK(thingsos_alarm_from_v1(&old,&a)&&!thingsos_alarm_used(&a));
    puts("PASS: recurring/one-shot alarms, inclusive date ranges, leap/year boundaries, expiry, deduplication and v1 migration");
    return 0;
}
