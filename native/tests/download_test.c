// SPDX-License-Identifier: Apache-2.0
#include "thingsos/download.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {
    size_t position;
    unsigned opens,closes,breaks;
    bool broken,permanent,reject_range,oversized;
} fake_t;
static uint8_t value(size_t n){return (uint8_t)(n*17+n/251);}
static int read_body(void* p,uint8_t* data,size_t n) {
    fake_t* f=p;
    if(f->oversized)return (int)n+1;
    if(f->permanent||f->broken)return -1;
    const size_t cuts[]={1234,5678};
    if(f->breaks<2) {
        size_t cut=cuts[f->breaks];
        if(f->position==cut){++f->breaks;f->broken=true;return 0;}
        if(n>cut-f->position)n=cut-f->position;
    }
    if(n>137)n=137;
    for(size_t i=0;i<n;++i)data[i]=value(f->position+i);
    f->position+=n;return (int)n;
}
static bool open_body(void* p,size_t offset) {
    fake_t* f=p;++f->opens;
    assert(offset==f->position);
    if(f->reject_range)return false;
    f->broken=false;return true;
}
static void close_body(void* p){++((fake_t*)p)->closes;}
static thingsos_download_t stream(fake_t* f) {
    return (thingsos_download_t){.context=f,.read=read_body,.open=open_body,.close=close_body,.bytes=9000};
}
int main(void) {
    assert(thingsos_download_range("bytes 1214950-3958991/3958992",1214950,3958992,2744042));
    const char* bad[]={"bytes 0-8999/9000","bytes 1234-8998/9000","bytes 1234-8999/9001",
        "bytes 1234-8999/*","bytes +1234-8999/9000","bytes 1234-8999/9000x",
        "bytes 1234-184467440737095516160000/9000","bytes ","",NULL};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)assert(!thingsos_download_range(bad[i],1234,9000,7766));
    assert(thingsos_download_range("bytes 1234-8999/9000",1234,9000,7766));
    assert(!thingsos_download_range("bytes 1234-8999/9000",1234,9000,9000));
    fake_t f={0};thingsos_download_t d=stream(&f);uint8_t buffer[4096];size_t got=0;
    while(got<9000) {
        int n=thingsos_download_read(&d,buffer,sizeof(buffer));assert(n>0);
        for(int i=0;i<n;++i)assert(buffer[i]==value(got+(size_t)i));
        got+=(size_t)n;
    }
    assert(got==9000&&d.received==9000&&d.retries==2&&f.opens==2&&f.closes==2);
    assert(thingsos_download_read(&d,buffer,sizeof(buffer))==0);
    f=(fake_t){.permanent=true};d=stream(&f);
    assert(thingsos_download_read(&d,buffer,10)<0&&d.received==0&&d.retries==4&&f.opens==4);
    f=(fake_t){.permanent=true,.reject_range=true};d=stream(&f);
    assert(thingsos_download_read(&d,buffer,10)<0&&d.retries==4&&f.opens==4);
    f=(fake_t){.oversized=true};d=stream(&f);
    assert(thingsos_download_read(&d,buffer,10)<0&&d.received==0&&f.closes==1);
    puts("Download: exact resume after partial reads, byte continuity, strict ranges, bounded retries and invalid transport sizes passed");
    return 0;
}
