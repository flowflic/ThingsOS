// SPDX-License-Identifier: Apache-2.0
#include "thingsos/cover_cache.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#define sync_file(f) _commit(_fileno(f))
#else
#include <unistd.h>
#define sync_file(f) fsync(fileno(f))
#endif
#define HEADER_BYTES 40u
static uint64_t hash_bytes(uint64_t h,const void* data,size_t n) {
    const unsigned char* p=data;
    for(size_t i=0;i<n;++i)h=(h^p[i])*1099511628211ULL;
    return h;
}
static void put64(unsigned char* p,uint64_t v) {
    for(unsigned i=0;i<8;++i)p[i]=(unsigned char)(v>>(8*i));
}
static uint64_t get64(const unsigned char* p) {
    uint64_t v=0;for(unsigned i=0;i<8;++i)v|=(uint64_t)p[i]<<(8*i);return v;
}
uint64_t thingsos_cover_source_stamp(const char* path) {
    struct stat s;
    if(!path || stat(path,&s) || (s.st_mode&S_IFMT)!=S_IFREG || s.st_size<=0)return 0;
    uint64_t h=hash_bytes(14695981039346656037ULL,path,strlen(path));
    unsigned char fields[16];put64(fields,(uint64_t)s.st_size);put64(fields+8,(uint64_t)s.st_mtime);
    h=hash_bytes(h,fields,sizeof(fields));return h?h:1;
}
static bool bounds(const thingsos_canvas_t* c,int x,int y,unsigned w,unsigned h) {
    return c && c->pixels && c->width>0 && c->height>0 && x>=0 && y>=0 && w && h &&
        x<c->width && y<c->height && w<=(unsigned)(c->width-x) && h<=(unsigned)(c->height-y);
}
static bool load_one(const char* path,uint64_t stamp,thingsos_canvas_t* c,int x,int y,unsigned w,unsigned h) {
    FILE* f=fopen(path,"rb");if(!f)return false;
    unsigned char header[HEADER_BYTES],block[1024];
    uint64_t bytes=(uint64_t)w*h,hash=14695981039346656037ULL;
    bool ok=fread(header,1,sizeof(header),f)==sizeof(header) && !memcmp(header,"TOSLCV1",8) &&
        get64(header+8)==((uint64_t)h<<32|w) && get64(header+16)==stamp && get64(header+32)==bytes;
    uint64_t left=bytes;
    while(ok && left) {
        size_t n=left>sizeof(block)?sizeof(block):(size_t)left;
        ok=fread(block,1,n,f)==n;if(ok)hash=hash_bytes(hash,block,n);left-=n;
    }
    ok=ok && hash==get64(header+24) && fgetc(f)==EOF && !ferror(f);
    /* Validate before painting so a corrupt cache cannot leave visible fragments. */
    if(ok)ok=!fseek(f,HEADER_BYTES,SEEK_SET);
    for(unsigned row=0;ok && row<h;++row)
        ok=fread(c->pixels+(size_t)(y+(int)row)*c->width+x,1,w,f)==w;
    if(fclose(f))ok=false;
    return ok;
}
bool thingsos_cover_cache_load(const char* path,uint64_t stamp,thingsos_canvas_t* c,int x,int y,unsigned w,unsigned h) {
    if(!path || !stamp || !bounds(c,x,y,w,h))return false;
    if(load_one(path,stamp,c,x,y,w,h))return true;
    char backup[640];int n=snprintf(backup,sizeof(backup),"%s.bak",path);
    return n>0 && (size_t)n<sizeof(backup) && load_one(backup,stamp,c,x,y,w,h);
}
bool thingsos_cover_cache_save(const char* path,uint64_t stamp,const thingsos_canvas_t* c,int x,int y,unsigned w,unsigned h) {
    if(!path || !stamp || !bounds(c,x,y,w,h))return false;
    char temp[640],backup[640];
    int a=snprintf(temp,sizeof(temp),"%s.tmp",path),b=snprintf(backup,sizeof(backup),"%s.bak",path);
    if(a<=0 || b<=0 || (size_t)a>=sizeof(temp) || (size_t)b>=sizeof(backup))return false;
    unsigned char header[HEADER_BYTES]={0};memcpy(header,"TOSLCV1",8);
    put64(header+8,(uint64_t)h<<32|w);put64(header+16,stamp);put64(header+32,(uint64_t)w*h);
    uint64_t hash=14695981039346656037ULL;
    for(unsigned row=0;row<h;++row)hash=hash_bytes(hash,c->pixels+(size_t)(y+(int)row)*c->width+x,w);
    put64(header+24,hash);
    FILE* f=fopen(temp,"wb");if(!f)return false;
    bool ok=fwrite(header,1,sizeof(header),f)==sizeof(header);
    for(unsigned row=0;ok && row<h;++row)
        ok=fwrite(c->pixels+(size_t)(y+(int)row)*c->width+x,1,w,f)==w;
    if(ok)ok=fflush(f)==0 && sync_file(f)==0;
    if(fclose(f))ok=false;
    if(!ok){remove(temp);return false;}
    struct stat old;bool existed=stat(path,&old)==0;
    if(existed) {
        remove(backup);
        if(rename(path,backup)){remove(temp);return false;}
    }
    if(rename(temp,path)) {
        if(existed)rename(backup,path);
        remove(temp);return false;
    }
    remove(backup);return true;
}
