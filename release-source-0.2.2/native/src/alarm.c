// SPDX-License-Identifier: Apache-2.0
#include "thingsos/alarm.h"
#include "thingsos/store.h"
#include <ctype.h>
#include <string.h>

static bool calendar(time_t t, struct tm* out) {
    if(t < 1577836800) return false;
#ifdef _WIN32
    if(localtime_s(out,&t))return false;
#else
    if(!localtime_r(&t,out))return false;
#endif
    return out->tm_year>=120 && out->tm_year<=199;
}
static uint32_t calendar_day(const struct tm* date) {
    return (uint32_t)(date->tm_year+1900)*10000+(date->tm_mon+1)*100+date->tm_mday;
}
bool thingsos_alarm_used(const thingsos_alarm_t* a) {
    return a && (a->mode==THINGSOS_ALARM_ONCE || a->days!=0);
}
bool thingsos_alarm_valid(const thingsos_alarm_t* a) {
    if(!a || a->enabled>1 || a->hour>23 || a->minute>59 || a->days>127 ||
       a->mode>THINGSOS_ALARM_ONCE || a->volume>100 ||
       (thingsos_alarm_used(a) && a->volume<10))return false;
    if(a->reserved[0] || a->reserved[1])return false;
    if((a->last_day && !thingsos_date_valid(a->last_day)) ||
       (a->start_day && !thingsos_date_valid(a->start_day)) ||
       (a->end_day && !thingsos_date_valid(a->end_day)) ||
       (a->start_day && a->end_day && a->start_day>a->end_day))return false;
    if(a->mode==THINGSOS_ALARM_ONCE && (a->days || !a->start_day || a->end_day!=a->start_day))return false;
    const char* end=memchr(a->music,0,sizeof(a->music));
    if(!end)return false;
    if(!thingsos_alarm_used(a))return !a->enabled && !a->start_day && !a->end_day;
    if(end-a->music<5 || a->music[0]=='.')return false;
    for(const char* p=a->music;p<end;++p)
        if((unsigned char)*p<32 || *p=='/' || *p=='\\' || *p==':')return false;
    const char* ext=end-4;
    return ext[0]=='.' && ((tolower((unsigned char)ext[1])=='m' &&
        tolower((unsigned char)ext[2])=='p' && ext[3]=='3') ||
        (tolower((unsigned char)ext[1])=='w' && tolower((unsigned char)ext[2])=='a' &&
         tolower((unsigned char)ext[3])=='v'));
}
bool thingsos_alarm_from_v1(const thingsos_alarm_v1_t* old,thingsos_alarm_t* a) {
    if(!old || !a || old->reserved[0] || old->reserved[1] || old->reserved[2])return false;
    thingsos_alarm_t next={.enabled=old->enabled,.hour=old->hour,.minute=old->minute,
        .days=old->days,.volume=old->volume,.last_day=old->last_day};
    memcpy(next.music,old->music,sizeof(next.music));
    if(!thingsos_alarm_valid(&next))return false;
    *a=next;return true;
}
bool thingsos_alarm_expired(const thingsos_alarm_t* a,time_t current) {
    struct tm date;
    if(!thingsos_alarm_valid(a) || !thingsos_alarm_used(a) || !calendar(current,&date))return false;
    uint32_t day=calendar_day(&date);
    if(a->mode==THINGSOS_ALARM_ONCE)
        return a->last_day>=a->start_day || day>a->start_day ||
            (day==a->start_day && date.tm_hour*3600+date.tm_min*60+date.tm_sec>a->hour*3600+a->minute*60+300);
    return a->end_day && day>a->end_day;
}
uint32_t thingsos_alarm_due(const thingsos_alarm_t* a,time_t previous,time_t current) {
    struct tm date;
    if(!thingsos_alarm_valid(a) || !a->enabled || !thingsos_alarm_used(a) || !calendar(current,&date))return 0;
    time_t minute=current-date.tm_sec;
    if(previous<=0 || previous>current)previous=minute-1;
    if(previous<current-300)previous=current-300;
    for(time_t at=minute;at>previous;at-=60) {
        if(!calendar(at,&date))continue;
        uint32_t day=calendar_day(&date);
        if((a->start_day && day<a->start_day) || (a->end_day && day>a->end_day))continue;
        if(day>a->last_day && (a->mode==THINGSOS_ALARM_ONCE || (a->days&(1u<<date.tm_wday))) &&
           date.tm_hour==a->hour && date.tm_min==a->minute)return day;
    }
    return 0;
}
