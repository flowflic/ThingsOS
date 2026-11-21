// SPDX-License-Identifier: Apache-2.0
/* Calendar cells are actual dates, Monday first; future days are outlined. */
#define STATS_CALENDAR_DAYS 94
#define STATS_HEAT_X 88
#define STATS_HEAT_Y 392
#define STATS_HEAT_PITCH_X 36
#define STATS_HEAT_PITCH_Y 27
#define STATS_HEAT_CELL 24
typedef struct {
    uint32_t dates[STATS_CALENDAR_DAYS];
    uint64_t duration[STATS_CALENDAR_DAYS];
    unsigned count, leading, columns, active;
    uint32_t today;
} stats_calendar_t;
static uint32_t stats_date(const struct tm* day) {
    return (uint32_t)(day->tm_year+1900)*10000+(day->tm_mon+1)*100+day->tm_mday;
}
static uint64_t stats_day_duration(uint32_t date) {
    for(size_t i=0;i<app.records.day_count;++i)
        if(app.records.days[i].date==date)return app.records.days[i].milliseconds;
    /* The aggregate ring retains 64 days. The existing per-book sidecar
       recovers earlier daily totals without changing rollback storage format. */
    uint64_t total=0;
    for(size_t i=0;i<app.records.book_day_count;++i)
        if(app.records.book_days[i].date==date) {
            uint64_t value=app.records.book_days[i].milliseconds;
            total=UINT64_MAX-total<value?UINT64_MAX:total+value;
        }
    return total;
}
static bool stats_calendar_build(time_t now,stats_calendar_t* calendar) {
    memset(calendar,0,sizeof(*calendar));
    struct tm today;
    if(now<1577836800 || !localtime_r(&now,&today))return false;
    today.tm_hour=12;today.tm_min=today.tm_sec=0;today.tm_isdst=-1;
    struct tm first=today;first.tm_mday=1;first.tm_mon-=2;
    if(mktime(&first)==(time_t)-1)return false;
    struct tm last=today;last.tm_mon++;last.tm_mday=0;
    if(mktime(&last)==(time_t)-1)return false;
    calendar->leading=(unsigned)(first.tm_wday+6)%7;
    calendar->today=stats_date(&today);
    uint32_t end=stats_date(&last);
    while(stats_date(&first)<=end && calendar->count<STATS_CALENDAR_DAYS) {
        unsigned i=calendar->count++;calendar->dates[i]=stats_date(&first);
        calendar->duration[i]=calendar->dates[i]<=calendar->today?stats_day_duration(calendar->dates[i]):0;
        if(calendar->duration[i])++calendar->active;
        first.tm_mday++;first.tm_isdst=-1;
        if(mktime(&first)==(time_t)-1)return false;
    }
    calendar->columns=(calendar->leading+calendar->count+6)/7;
    return calendar->count>0 && calendar->columns<=15;
}
static unsigned stats_heat_level(uint64_t ms) {
    return !ms?0:ms<5ULL*60000?1:ms<10ULL*60000?2:ms<15ULL*60000?3:
        ms<30ULL*60000?4:ms<45ULL*60000?5:ms<60ULL*60000?6:7;
}
static void stats_heatmap(time_t now) {
    stats_calendar_t calendar;
    label("三个月 · 阅读足迹",36,342,26,440);
    if(!stats_calendar_build(now,&calendar)) {
        centered_label("校准日期后显示每日阅读记录",36,490,24,612);
        return;
    }
    char text[64];
    static const unsigned char shades[]={238,204,170,136,102,68,34,0};
    for(unsigned row=0;row<7;++row) {
        static const char* days[]={"一","二","三","四","五","六","日"};
        if(row%2==0)label(days[row],42,STATS_HEAT_Y+(int)row*STATS_HEAT_PITCH_Y+20,20,30);
    }
    for(unsigned i=0;i<calendar.count;++i) {
        unsigned slot=calendar.leading+i,col=slot/7,row=slot%7;
        int x=STATS_HEAT_X+(int)col*STATS_HEAT_PITCH_X;
        int y=STATS_HEAT_Y+(int)row*STATS_HEAT_PITCH_Y;
        if(calendar.dates[i]>calendar.today) {
            thingsos_rect(&app.canvas,x,y,STATS_HEAT_CELL,1,204);
            thingsos_rect(&app.canvas,x,y+STATS_HEAT_CELL-1,STATS_HEAT_CELL,1,204);
            thingsos_rect(&app.canvas,x,y,1,STATS_HEAT_CELL,204);
            thingsos_rect(&app.canvas,x+STATS_HEAT_CELL-1,y,1,STATS_HEAT_CELL,204);
        } else thingsos_rect(&app.canvas,x,y,STATS_HEAT_CELL,STATS_HEAT_CELL,shades[stats_heat_level(calendar.duration[i])]);
        if((!i && calendar.dates[i]%100<=24) || calendar.dates[i]%100==1) {
            snprintf(text,sizeof(text),"%u月",(unsigned)(calendar.dates[i]/100%100));
            label(text,x,376,20,90);
        }
    }
}


#define STATS_TABS_X 36
#define STATS_TABS_Y 144
#define STATS_TAB_WIDTH 204
#define STATS_TAB_HEIGHT 64
static const char* stats_period_name(unsigned period) {
    static const char* names[]={"今日","本周","本月"};return names[period<3?period:0];
}
static int stats_tab_hit(int x,int y) {
    if(x<STATS_TABS_X || x>=STATS_TABS_X+3*STATS_TAB_WIDTH ||
       y<STATS_TABS_Y || y>=STATS_TABS_Y+STATS_TAB_HEIGHT)return -1;
    return (x-STATS_TABS_X)/STATS_TAB_WIDTH;
}
typedef struct {
    uint32_t first,last,today;
    unsigned count;
    bool dated;
    uint64_t values[31],total,unknown,book_ms[THINGSOS_RECORD_BOOKS];
} stats_period_t;
static bool stats_period_build(time_t time,unsigned period,stats_period_t* out) {
    memset(out,0,sizeof(*out));out->first=UINT32_MAX;
    struct tm today;
    if(time<1577836800 || !localtime_r(&time,&today) || today.tm_year>199)return false;
    today.tm_hour=12;today.tm_min=today.tm_sec=0;today.tm_isdst=-1;
    out->today=stats_date(&today);
    struct tm first=today,last=today;
    if(period==1) {first.tm_mday-=(today.tm_wday+6)%7;last=first;last.tm_mday+=6;out->count=7;}
    else if(period==2) {first.tm_mday=1;last.tm_mon++;last.tm_mday=0;}
    else out->count=24;
    if(mktime(&first)==(time_t)-1 || mktime(&last)==(time_t)-1)return false;
    out->first=stats_date(&first);out->last=stats_date(&last);out->dated=true;
    if(period==2)out->count=(unsigned)last.tm_mday;
    if(period==0) {
        out->total=stats_day_duration(out->today);
        uint64_t known=0;
        if(app.records.hour_date==out->today)for(unsigned h=0;h<24;++h) {
            out->values[h]=app.records.hour_ms[h];known+=out->values[h];
        }
        out->unknown=out->total>known?out->total-known:0;
    } else {
        for(unsigned i=0;i<out->count;++i) {
            struct tm day=first;day.tm_mday+=(int)i;day.tm_isdst=-1;
            if(mktime(&day)==(time_t)-1)return false;
            uint32_t date=stats_date(&day);
            if(date<=out->today)out->values[i]=stats_day_duration(date);
            out->total+=out->values[i];
        }
    }
    uint32_t through=out->last<out->today?out->last:out->today;
    for(size_t i=0;i<app.records.book_count;++i)
        out->book_ms[i]=thingsos_book_time(&app.records,app.records.books[i].id,out->first,through);
    return true;
}
static uint64_t stats_chart_step(const stats_period_t* data) {
    uint64_t maximum=0;
    for(unsigned i=0;i<data->count;++i)if(data->values[i]>maximum)maximum=data->values[i];
    uint64_t minutes=(maximum+59999)/60000;
    uint64_t target=(minutes+3)/4,scale=1;if(!target)target=1;
    while(target>10) {target=(target+9)/10;scale*=10;}
    return (target<=1?1:target<=2?2:target<=5?5:10)*scale;
}
static void stats_chart(const stats_period_t* data,unsigned period) {
    char text[100];
    snprintf(text,sizeof(text),"%s阅读 %llu 分钟",stats_period_name(period),(unsigned long long)(data->total/60000));
    label(text,36,644,24,612);
    if(!data->dated) {centered_label("校准日期后显示阅读统计",36,792,24,612);return;}
    if(data->unknown) {
        snprintf(text,sizeof(text),"其中 %llu 分 %02u 秒暂无小时明细",(unsigned long long)(data->unknown/60000),(unsigned)(data->unknown/1000%60));
        label(text,36,675,20,612);
    }
    const int left=76,width=572,bottom=866,height=156;
    uint64_t step=stats_chart_step(data),top=step*4;
    label("分钟",36,698,18,100);
    for(unsigned row=0;row<=4;++row) {
        int y=bottom-(int)row*height/4;
        thingsos_rect(&app.canvas,left,y,width,1,row?220:100);
        snprintf(text,sizeof(text),"%llu",(unsigned long long)(step*row));
        thingsos_font_size(ui_font(),18);
        thingsos_label_right(ui_font(),&app.canvas,text,30,y+6,38);
    }
    static const char* weekdays[]={"周一","周二","周三","周四","周五","周六","周日"};
    for(unsigned i=0;i<data->count;++i) {
        int x=left+(int)(i*width/data->count),next=left+(int)((i+1)*width/data->count);
        int h=(int)((double)data->values[i]/60000/(double)top*height);
        if(data->values[i] && h<1)h=1;
        if(h)thingsos_rect(&app.canvas,x+3,bottom-h,next-x-6,h,0);
        /* Every slot is a real hour/day; sparse labels keep 24/31 slots legible. */
        bool tick=period==1 || (period==0?(i%3==0 || i==23):(i%5==0 || i+1==data->count));
        thingsos_rect(&app.canvas,(x+next)/2,bottom,1,5,100);
        if(tick) {
            if(period==1)snprintf(text,sizeof(text),"%s",weekdays[i]);
            else snprintf(text,sizeof(text),"%u",period==0?i:i+1);
            centered_label(text,(x+next)/2-32,902,period==1?22:18,64);
        }
    }
    label(period==0?"时":"日",620,928,18,28);
}
