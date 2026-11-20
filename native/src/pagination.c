// SPDX-License-Identifier: Apache-2.0
#include "thingsos/pagination.h"
#include "thingsos/mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
size_t thingsos_page_find(const thingsos_page_position_t* p, size_t n, uint32_t ch, uint32_t off) {
    if (!p || !n) return 0;
    size_t lo=0, hi=n;
    while(lo<hi) { size_t m=lo+(hi-lo)/2;
        if(p[m].chapter<ch || (p[m].chapter==ch && p[m].offset<=off)) lo=m+1;
        else hi=m;
    }
    return lo ? lo-1 : 0;
}
unsigned thingsos_book_percent(size_t page, size_t count) {
    if(count<=1) return 0;
    if(page>=count-1) return 100;
    return (unsigned)((uint64_t)page*100/(count-1));
}
size_t thingsos_page_percent(unsigned percent, size_t count) {
    if(!count) return 0;
    if(percent>100) percent=100;
    return (size_t)((uint64_t)(count-1)*percent/100);
}
static uint32_t checksum(const void* data,size_t bytes) {
    const unsigned char* p=data;uint32_t h=2166136261u;
    for(size_t i=0;i<bytes;++i) h=(h^p[i])*16777619u;
    return h;
}
bool thingsos_page_cache_load(const char* path,uint64_t key,thingsos_page_position_t** out,size_t* count) {
    *out=NULL;*count=0;
    FILE* f=fopen(path,"rb");if(!f) return false;
    uint64_t header[4];thingsos_page_position_t* p=NULL;bool ok=false;
    if(fread(header,sizeof(header),1,f)!=1 || header[0]!=0x31584750485354ULL ||
       header[1]!=key || !header[2] || header[2]>65536) goto done;
    size_t n=(size_t)header[2];p=thingsos_psram_alloc(n*sizeof(*p));if(!p) goto done;
    if(fread(p,sizeof(*p),n,f)!=n || fgetc(f)!=EOF || ferror(f) ||
       checksum(p,n*sizeof(*p))!=header[3] || p[0].chapter || p[0].offset) goto done;
    for(size_t i=1;i<n;++i)
        if(p[i].chapter<p[i-1].chapter || p[i].chapter>p[i-1].chapter+1 ||
           (p[i].chapter==p[i-1].chapter && p[i].offset<=p[i-1].offset) ||
           (p[i].chapter!=p[i-1].chapter && p[i].offset)) goto done;
    *out=p;*count=n;p=NULL;ok=true;
done:free(p);fclose(f);return ok;
}
bool thingsos_page_cache_save(const char* path,uint64_t key,const thingsos_page_position_t* p,size_t n) {
    if(!p || !n || n>65536) return false;
    char temporary[600];if(snprintf(temporary,sizeof(temporary),"%s.tmp",path)>=(int)sizeof(temporary)) return false;
    FILE* f=fopen(temporary,"wb");if(!f) return false;
    uint64_t header[]={0x31584750485354ULL,key,n,checksum(p,n*sizeof(*p))};
    bool ok=fwrite(header,sizeof(header),1,f)==1 && fwrite(p,sizeof(*p),n,f)==n;
    if(fclose(f)) ok=false;
    if(ok) {remove(path);ok=rename(temporary,path)==0;}
    if(!ok) remove(temporary);
    return ok;
}
/* A separate file/magic keeps interrupted work out of the full-index path. */
static bool checkpoint_positions(const thingsos_page_position_t* p,size_t n,unsigned done) {
    if(!p || !n || n>65536 || !done || p[0].chapter || p[0].offset || p[n-1].chapter!=done-1)return false;
    for(size_t i=1;i<n;++i)
        if(p[i].chapter<p[i-1].chapter || p[i].chapter>p[i-1].chapter+1 ||
           (p[i].chapter==p[i-1].chapter && p[i].offset<=p[i-1].offset) ||
           (p[i].chapter!=p[i-1].chapter && p[i].offset))return false;
    return true;
}
bool thingsos_page_checkpoint_load(const char* path,uint64_t key,unsigned chapters,
    thingsos_page_position_t** out,size_t* count,unsigned* completed) {
    *out=NULL;*count=0;*completed=0;
    FILE* f=fopen(path,"rb");if(!f)return false;
    uint64_t h[7];thingsos_page_position_t* p=NULL;bool ok=false;
    if(fread(h,sizeof(h),1,f)!=1 || h[0]!=0x31545043485354ULL || h[1]!=key ||
       h[2]!=chapters || !h[3] || h[3]>=chapters || !h[4] || h[4]>65536 ||
       h[6]!=checksum(h,6*sizeof(*h)))goto done;
    size_t n=(size_t)h[4];p=thingsos_psram_alloc(n*sizeof(*p));
    if(!p || fread(p,sizeof(*p),n,f)!=n || fgetc(f)!=EOF || ferror(f) ||
       h[5]!=checksum(p,n*sizeof(*p)) || !checkpoint_positions(p,n,(unsigned)h[3]))goto done;
    *out=p;*count=n;*completed=(unsigned)h[3];p=NULL;ok=true;
done:free(p);fclose(f);return ok;
}
bool thingsos_page_checkpoint_save(const char* path,uint64_t key,unsigned chapters,
    const thingsos_page_position_t* p,size_t n,unsigned completed) {
    if(completed>=chapters || !checkpoint_positions(p,n,completed))return false;
    char tmp[640];if(snprintf(tmp,sizeof(tmp),"%s.tmp",path)>=(int)sizeof(tmp))return false;
    uint64_t h[]={0x31545043485354ULL,key,chapters,completed,n,checksum(p,n*sizeof(*p)),0};
    h[6]=checksum(h,6*sizeof(*h));FILE* f=fopen(tmp,"wb");if(!f)return false;
    bool ok=fwrite(h,sizeof(h),1,f)==1 && fwrite(p,sizeof(*p),n,f)==n;
    if(fflush(f))ok=false;
    if(fclose(f))ok=false;
    if(ok) {remove(path);ok=rename(tmp,path)==0;}
    if(!ok)remove(tmp);
    return ok;
}
