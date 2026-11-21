/* SPDX-License-Identifier: Apache-2.0
 * Independently written bounded TXT reader; never rewrites the source book.
 * The GBK lookup keeps the FatFs/ChaN license in vendor/fatfs-txt/.
 */
#include <sys/stat.h>
#include "zlib.h"
#include "thingsos/weidu.h"
#include "../vendor/fatfs-txt/gbk_unicode.inc"
#define TXT_MIN_SEGMENT (THINGSOS_TXT_SEGMENT/2-4)
static void txt_put(unsigned char* p,uint64_t n,unsigned bytes) {
    for(unsigned i=0;i<bytes;++i)p[i]=(unsigned char)(n>>(8*i));
}
static uint64_t txt_get(const unsigned char* p,unsigned bytes) {
    uint64_t n=0;for(unsigned i=0;i<bytes;++i)n|=(uint64_t)p[i]<<(8*i);return n;
}
static unsigned txt_scalar(const unsigned char* p,size_t left,unsigned encoding,uint32_t* cp) {
    if(!left || !p[0])return 0;
    if(p[0]<128){*cp=p[0];return 1;}
    if(encoding==2) {
        if(p[0]==0x80){*cp=0x20ac;return 1;}
        if(left<2 || p[0]<0x81 || p[0]>0xfe || p[1]<0x40 || p[1]>0xfe || p[1]==0x7f)return 0;
        *cp=txt_gbk_unicode[(p[0]-0x81)*190+p[1]-0x40-(p[1]>0x7f)];return *cp?2:0;
    }
    unsigned n=p[0]>=0xc2&&p[0]<=0xdf?2:p[0]>=0xe0&&p[0]<=0xef?3:p[0]>=0xf0&&p[0]<=0xf4?4:0;
    if(!n || left<n)return 0;
    uint32_t value=p[0]&((1u<<(7-n))-1);
    for(unsigned i=1;i<n;++i){if((p[i]&0xc0)!=0x80)return 0;value=(value<<6)|(p[i]&63);}
    if(value<(n==2?0x80u:n==3?0x800u:0x10000u) || value>0x10ffff || (value>=0xd800&&value<=0xdfff))return 0;
    *cp=value;return n;
}
static bool txt_index_read(thingsos_epub_t* e,const char* path,const unsigned char key[20]) {
    if(!path || !*path)return false;
    FILE* f=fopen(path,"rb");if(!f)return false;
    unsigned char h[40];bool ok=fread(h,1,40,f)==40 && !memcmp(h,"TXT2",4) &&
        !memcmp(h+4,key,20) && txt_get(h+36,4)==crc32(0,h,36);
    unsigned count=ok?(unsigned)txt_get(h+28,4):0;
    if(!count || count>1+e->zip.size/TXT_MIN_SEGMENT || (h[24]!=1&&h[24]!=2) ||
       !thingsos_epub_reserve_chapters(e,count))ok=false;
    uint32_t crc=0;
    for(unsigned i=0;ok&&i<count;++i) {
        unsigned char wire[4];ok=fread(wire,1,4,f)==4;
        if(!ok)break;
        crc=crc32(crc,wire,4);e->spine[i]=(unsigned)txt_get(wire,4);
        if((!i&&e->spine[i]) || e->spine[i]>e->zip.size ||
           (i&&(e->spine[i]<=e->spine[i-1] || e->spine[i]-e->spine[i-1]>THINGSOS_TXT_SEGMENT)))ok=false;
    }
    if(ok)ok=crc==txt_get(h+32,4) && fgetc(f)==EOF && !ferror(f) &&
        e->zip.size-e->spine[count-1]<=THINGSOS_TXT_SEGMENT;
    fclose(f);
    if(ok){e->spine_count=count;e->text_encoding=h[24];}
    return ok;
}
static void txt_index_save(thingsos_epub_t* e,const char* path,const unsigned char key[20]) {
    if(!path || !*path)return;
    char temp[640];if(snprintf(temp,sizeof(temp),"%s.tmp",path)>=(int)sizeof(temp))return;
    FILE* f=fopen(temp,"wb");if(!f)return;
    unsigned char h[40]={0};memcpy(h,"TXT2",4);memcpy(h+4,key,20);
    txt_put(h+24,e->text_encoding,4);txt_put(h+28,e->spine_count,4);
    uint32_t crc=0;
    for(unsigned i=0;i<e->spine_count;++i){unsigned char wire[4];txt_put(wire,e->spine[i],4);crc=crc32(crc,wire,4);}
    txt_put(h+32,crc,4);txt_put(h+36,crc32(0,h,36),4);
    bool ok=fwrite(h,1,40,f)==40;
    for(unsigned i=0;ok&&i<e->spine_count;++i){unsigned char wire[4];txt_put(wire,e->spine[i],4);ok=fwrite(wire,1,4,f)==4;}
    if(ok)ok=thingsos_weidu_flush(f);
    if(fclose(f))ok=false;
    if(ok){remove(path);if(rename(temp,path))remove(temp);}else remove(temp);
}
static bool txt_index_build(thingsos_epub_t* e) {
    unsigned char* block=thingsos_psram_alloc(THINGSOS_TXT_SEGMENT+4);if(!block)return false;
    bool ok=false,bom=false;
    for(unsigned encoding=1;encoding<=2;++encoding) {
        if(encoding==2&&bom)break;
        uint32_t pos=0;e->spine_count=0;ok=true;
        do {
            size_t bytes=(size_t)(e->zip.size-pos);if(bytes>THINGSOS_TXT_SEGMENT)bytes=THINGSOS_TXT_SEGMENT;
            if(fseek(e->zip.file,pos,SEEK_SET) || fread(block,1,bytes,e->zip.file)!=bytes){ok=false;break;}
            if(!pos&&bytes>=3)bom=!memcmp(block,"\xef\xbb\xbf",3);
            size_t end=bytes;
            if(pos+bytes<e->zip.size) {
                size_t line=bytes;
                while(line>bytes/2 && block[line-1]!='\n')--line;
                if(line>bytes/2)end=line;
            }
            size_t used=0;
            while(used<end) {
                uint32_t cp;unsigned n=txt_scalar(block+used,bytes-used,encoding,&cp);
                if(!n) {
                    /* A very long line may end in the middle of a scalar. */
                    if(pos+bytes<e->zip.size && bytes-used<4 && end==bytes) {
                        /* Confirm a split scalar using a bounded lookahead. */
                        size_t more=(size_t)(e->zip.size-pos-bytes);
                        if(more>4)more=4;
                        if(fread(block+bytes,1,more,e->zip.file)==more &&
                           txt_scalar(block+used,bytes-used+more,encoding,&cp)>bytes-used) {
                            end=used;
                            break;
                        }
                    }
                    ok=false;break;
                }
                used+=n;
            }
            if(!ok || (bytes&&!end) || e->spine_count==THINGSOS_SPINE_LIMIT){ok=false;break;}
            if(e->spine_count==e->chapter_capacity) {
                size_t capacity=e->chapter_capacity?e->chapter_capacity*2:1;
                size_t limit=1+(size_t)e->zip.size/TXT_MIN_SEGMENT;
                if(capacity>limit)capacity=limit;
                if(capacity<=e->spine_count || !thingsos_epub_reserve_chapters(e,capacity)){ok=false;break;}
            }
            e->spine[e->spine_count++]=pos;pos+=(uint32_t)end;
        } while(pos<e->zip.size);
        if(ok){e->text_encoding=(uint8_t)encoding;break;}
    }
    free(block);return ok;
}
bool thingsos_txt_open(thingsos_epub_t* e,const char* path,const char* index_path) {
    if(!e || !path)return false;
    memset(e,0,sizeof(*e));struct stat info;
    if(stat(path,&info) || info.st_size<0 || (uint64_t)info.st_size>THINGSOS_TXT_BYTES)return false;
    e->zip.file=fopen(path,"rb");if(!e->zip.file)return false;
    e->zip.size=(uint64_t)info.st_size;e->plain_text=true;
    e->chapter_fragments_deferred=true;
    /* The cache uses size/mtime and first+last 4 KiB samples, never book text.
       It is disposable; CRC failure rebuilds it from the read-only source. */
    unsigned char sample[1024],key[20]={0};uint32_t crc=0;
    for(unsigned end=0;end<2;++end) {
        size_t bytes=e->zip.size<4096?(size_t)e->zip.size:4096;
        if(fseek(e->zip.file,end?(long)(e->zip.size-bytes):0,SEEK_SET))goto fail;
        while(bytes){size_t part=bytes>sizeof(sample)?sizeof(sample):bytes;
            if(fread(sample,1,part,e->zip.file)!=part)goto fail;
            crc=crc32(crc,sample,(uInt)part);bytes-=part;}
    }
    txt_put(key,e->zip.size,8);txt_put(key+8,(uint64_t)info.st_mtime,8);txt_put(key+16,crc,4);
    if(!txt_index_read(e,index_path,key)) {
        if(!txt_index_build(e))goto fail;
        txt_index_save(e,index_path,key);
    }
    e->text_signature=((uint64_t)crc<<32)^e->zip.size^(uint64_t)info.st_mtime;
    for(unsigned i=0;i<e->spine_count;++i) {
        e->chapter_sources[i]=e->text_encoding==1?0:i;
        snprintf(e->chapter_names[i],160,"第 %u 部分",i+1);
    }
    e->navigation_loaded=true;e->titles_next=(unsigned)e->spine_count;
    return true;
fail:thingsos_epub_close(e);return false;
}
static bool txt_source_chapter(thingsos_epub_t* e,size_t index,thingsos_chapter_t* c) {
    size_t start=e->spine[index],end=index+1<e->spine_count?e->spine[index+1]:(size_t)e->zip.size;
    if(end<start || end-start>THINGSOS_TXT_SEGMENT)return false;
    size_t bytes=end-start;unsigned char* raw=thingsos_psram_alloc(bytes+1);
    if(!raw)return false;
    if(fseek(e->zip.file,(long)start,SEEK_SET) || fread(raw,1,bytes,e->zip.file)!=bytes){free(raw);return false;}
    size_t used=0;
    for(size_t pos=0;pos<bytes;) {
        uint32_t cp;unsigned n=txt_scalar(raw+pos,bytes-pos,e->text_encoding,&cp);
        if(!n){free(raw);return false;}
        used+=e->text_encoding==1?n:cp<128?1:cp<0x800?2:3;
        pos+=n;
    }
    char* text=(char*)raw;
    if(e->text_encoding==2) {
        text=thingsos_psram_alloc(used+1);
        if(!text){free(raw);return false;}
        size_t cursor=0;
        for(size_t pos=0;pos<bytes;) {
            uint32_t cp;
            pos+=txt_scalar(raw+pos,bytes-pos,2,&cp);
            if(cp<128)text[cursor++]=(char)cp;
            else if(cp<0x800){text[cursor++]=(char)(0xc0|(cp>>6));text[cursor++]=(char)(0x80|(cp&63));}
            else {text[cursor++]=(char)(0xe0|(cp>>12));text[cursor++]=(char)(0x80|((cp>>6)&63));text[cursor++]=(char)(0x80|(cp&63));}
        }
        free(raw);
    }
    memset(c,0,sizeof(*c));text[used]=0;c->text=text;c->bytes=used;
    c->source_start=e->text_encoding==1?start:0;return true;
}
