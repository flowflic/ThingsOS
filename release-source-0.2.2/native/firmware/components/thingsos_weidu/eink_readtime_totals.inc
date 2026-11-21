/* SPDX-License-Identifier: Apache-2.0
 * Account-bound acknowledged Native lifetime total and reading-date totals
 * for Web / Native receipts. Callers own the existing I/O lock.
 */
#include "zlib.h"
static void rt_total_put(unsigned char* p,uint64_t n,unsigned bytes) {
    for(unsigned i=0;i<bytes;++i)p[i]=(unsigned char)(n>>(8*i));
}
static uint64_t rt_total_get(const unsigned char* p,unsigned bytes) {
    uint64_t n=0;for(unsigned i=0;i<bytes;++i)n|=(uint64_t)p[i]<<(8*i);return n;
}
static bool rt_total_owner(const char* owner,char space[16]) {
    if(!owner || strlen(owner)!=32)return false;
    for(unsigned i=0;i<32;++i)if(!isxdigit((unsigned char)owner[i]))return false;
    snprintf(space,16,"rtu-%.11s",owner);return true;
}

#define RT_DAY_SLOTS 32u
#define RT_TOTAL_BYTES (176u+RT_DAY_SLOTS*20u)
typedef struct {uint32_t day;uint64_t web,native;} rt_day_total_t;
typedef struct {
    uint64_t native_total;
    char native_key[65],web_key[65];
    rt_day_total_t days[RT_DAY_SLOTS];
} rt_total_state_t;
static uint32_t rt_reading_day(uint32_t stamp,int zone) {
    int64_t local=(int64_t)stamp+(int64_t)zone*60;
    return stamp>=1790812800u&&zone>=-840&&zone<=840&&local>0?(uint32_t)(local/86400):0;
}
static bool rt_total_load_state(const char* owner,rt_total_state_t* state) {
    memset(state,0,sizeof(*state));
    char space[16];if(!rt_total_owner(owner,space))return false;
    nvs_handle_t h;esp_err_t e=nvs_open(space,NVS_READONLY,&h);
    if(e==ESP_ERR_NVS_NOT_FOUND)return true;
    if(e!=ESP_OK)return false;
    unsigned char wire[RT_TOTAL_BYTES];size_t bytes=sizeof(wire);
    e=nvs_get_blob(h,"total-v2",wire,&bytes);
    if(e==ESP_ERR_NVS_NOT_FOUND) {
        bytes=sizeof(wire);e=nvs_get_blob(h,"total-v1",wire,&bytes);nvs_close(h);
        if(e==ESP_ERR_NVS_NOT_FOUND)return true;
        if(e!=ESP_OK||bytes!=112||memcmp(wire,"WRS1",4)||memcmp(wire+12,owner,32)||
           rt_total_get(wire+108,4)!=crc32(0,wire,108))return false;
        for(unsigned i=44;i<108;++i)if(!isxdigit(wire[i]))return false;
        state->native_total=rt_total_get(wire+4,8);memcpy(state->native_key,wire+44,64);
        return true; /* The legacy lifetime total has no recoverable reading dates. */
    }
    nvs_close(h);
    if(e!=ESP_OK||bytes!=sizeof(wire)||memcmp(wire,"WRS2",4)||memcmp(wire+12,owner,32)||
       rt_total_get(wire+sizeof(wire)-4,4)!=crc32(0,wire,sizeof(wire)-4))return false;
    for(unsigned i=44;i<172;++i)if(wire[i]&&!isxdigit(wire[i]))return false;
    state->native_total=rt_total_get(wire+4,8);
    memcpy(state->native_key,wire+44,64);memcpy(state->web_key,wire+108,64);
    for(unsigned i=0;i<RT_DAY_SLOTS;++i) {
        const unsigned char* d=wire+172+i*20;
        state->days[i]=(rt_day_total_t){(uint32_t)rt_total_get(d,4),rt_total_get(d+4,8),rt_total_get(d+12,8)};
        if(state->days[i].web>UINT64_MAX-state->days[i].native)return false;
        for(unsigned j=0;j<i;++j)if(state->days[i].day&&state->days[i].day==state->days[j].day)return false;
    }
    return true;
}
static bool rt_total_save_state(const char* owner,const rt_total_state_t* state) {
    char space[16];if(!rt_total_owner(owner,space))return false;
    unsigned char wire[RT_TOTAL_BYTES]={0};
    memcpy(wire,"WRS2",4);rt_total_put(wire+4,state->native_total,8);memcpy(wire+12,owner,32);
    memcpy(wire+44,state->native_key,64);memcpy(wire+108,state->web_key,64);
    for(unsigned i=0;i<RT_DAY_SLOTS;++i) {
        unsigned char* d=wire+172+i*20;
        rt_total_put(d,state->days[i].day,4);rt_total_put(d+4,state->days[i].web,8);rt_total_put(d+12,state->days[i].native,8);
    }
    rt_total_put(wire+sizeof(wire)-4,crc32(0,wire,sizeof(wire)-4),4);
    nvs_handle_t h;if(nvs_open(space,NVS_READWRITE,&h)!=ESP_OK)return false;
    bool ok=nvs_set_blob(h,"total-v2",wire,sizeof(wire))==ESP_OK&&nvs_commit(h)==ESP_OK;
    if(ok){unsigned char saved[RT_TOTAL_BYTES];size_t bytes=sizeof(saved);
        ok=nvs_get_blob(h,"total-v2",saved,&bytes)==ESP_OK&&bytes==sizeof(wire)&&!memcmp(wire,saved,bytes);}
    nvs_close(h);return ok;
}
static bool rt_day_add(rt_total_state_t* s,uint32_t day,uint64_t seconds,bool web) {
    if(!day)return false;
    unsigned slot=0;
    for(unsigned i=0;i<RT_DAY_SLOTS;++i) {
        if(s->days[i].day==day){slot=i;goto found;}
        if(s->days[i].day<s->days[slot].day)slot=i;
    }
    /* Keep the latest reading dates, so old backfills cannot evict today. */
    if(s->days[slot].day>day)return true;
    s->days[slot]=(rt_day_total_t){.day=day};
found:;
    rt_day_total_t* d=&s->days[slot];
    if(d->web>UINT64_MAX-d->native||seconds>UINT64_MAX-d->web-d->native)return false;
    if(web)d->web+=seconds;else d->native+=seconds;
    return true;
}
static bool rt_daily_read(const char* owner,uint32_t stamp,uint64_t* web,uint64_t* native) {
    *web=*native=0;uint32_t day=rt_reading_day(stamp,480);if(!day)return false;
    rt_total_state_t* s=thingsos_psram_alloc(sizeof(*s));if(!s)return false;
    bool ok=rt_total_load_state(owner,s);
    if(ok)for(unsigned i=0;i<RT_DAY_SLOTS;++i)if(s->days[i].day==day){*web=s->days[i].web;*native=s->days[i].native;break;}
    free(s);return ok;
}
static bool rt_uploaded_load(const char* owner,uint64_t* total,char last[65]) {
    *total=0;memset(last,0,65);
    rt_total_state_t* s=thingsos_psram_alloc(sizeof(*s));if(!s)return false;
    bool ok=rt_total_load_state(owner,s);
    if(ok){*total=s->native_total;memcpy(last,s->native_key,65);}
    free(s);return ok;
}
static int rt_total_order(const void* a,const void* b){return memcmp(a,b,12);}
static thingsos_weidu_error_t rt_uploaded_commit(const char* owner,const thingsos_rt_batch_t* batch,
                                                const cJSON* ack,uint64_t* total) {
    const cJSON* succ=cJSON_GetObjectItemCaseSensitive(ack,"succ");
    if(!cJSON_IsTrue(succ)&&!(cJSON_IsNumber(succ)&&succ->valuedouble==1))return WD_UNCONFIRMED;
    if(!batch||!batch->count||batch->count>WD_RT_BATCH)return WD_PROTOCOL;
    rt_total_state_t* s=thingsos_psram_alloc(sizeof(*s));if(!s)return WD_MEMORY;
    if(!rt_total_load_state(owner,s)){free(s);return WD_STORAGE;}
    *total=s->native_total;
    unsigned char* ids=thingsos_psram_alloc((size_t)batch->count*12);
    if(!ids){free(s);return WD_MEMORY;}
    uint64_t seconds=0;bool ok=true;char key[65];
    for(unsigned i=0;i<batch->count;++i) {
        const thingsos_rt_row_t* row=&batch->rows[i];
        if(!row->id||!row->seconds||row->seconds>3600||row->hour%3600||!rt_reading_day(row->hour,row->zone))ok=false;
        rt_total_put(ids+12*i,row->id,8);rt_total_put(ids+12*i+8,row->seconds,4);seconds+=row->seconds;
    }
    qsort(ids,batch->count,12,rt_total_order);
    for(unsigned i=1;i<batch->count;++i)if(!memcmp(ids+12*(i-1),ids+12*i,8))ok=false;
    if(ok)ok=thingsos_rt_hash(ids,(size_t)batch->count*12,key);
    free(ids);
    thingsos_weidu_error_t e=WD_OK;
    if(!ok)e=WD_PROTOCOL;
    else if(!strcmp(s->native_key,key)){free(s);return WD_OK;}
    else if(seconds>UINT64_MAX-s->native_total)e=WD_LIMIT;
    else {
        for(unsigned i=0;i<batch->count&&ok;++i)ok=rt_day_add(s,rt_reading_day(batch->rows[i].hour,batch->rows[i].zone),batch->rows[i].seconds,false);
        if(!ok)e=WD_LIMIT;
        else {s->native_total+=seconds;strcpy(s->native_key,key);
            if(!rt_total_save_state(owner,s))e=WD_STORAGE;else *total=s->native_total;}
    }
    free(s);return e;
}
static thingsos_weidu_error_t rt_web_commit(const char* owner,const char* book,const char* session,
                                            uint32_t end,uint32_t seconds) {
    if(!seconds||end<seconds||!rt_reading_day(end-seconds,480)||!book||!session)return WD_PROTOCOL;
    char event[320],key[65];
    int n=snprintf(event,sizeof(event),"%s/%s/%u/%u",book,session,(unsigned)end,(unsigned)seconds);
    if(n<0||(size_t)n>=sizeof(event)||!thingsos_rt_hash(event,(size_t)n,key))return WD_PROTOCOL;
    rt_total_state_t* s=thingsos_psram_alloc(sizeof(*s));if(!s)return WD_MEMORY;
    if(!rt_total_load_state(owner,s)){free(s);return WD_STORAGE;}
    if(!strcmp(s->web_key,key)){free(s);return WD_OK;}
    bool ok=true;uint32_t begin=end-seconds;
    while(begin<end&&ok) {
        uint32_t day=rt_reading_day(begin,480);
        uint64_t midnight=((uint64_t)begin+28800)/86400*86400+86400-28800;
        uint32_t until=midnight<end?(uint32_t)midnight:end;
        ok=rt_day_add(s,day,until-begin,true);begin=until;
    }
    thingsos_weidu_error_t e=ok?WD_OK:WD_LIMIT;
    if(ok){strcpy(s->web_key,key);if(!rt_total_save_state(owner,s))e=WD_STORAGE;}
    free(s);return e;
}
