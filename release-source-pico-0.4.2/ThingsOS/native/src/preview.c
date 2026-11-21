// SPDX-License-Identifier: Apache-2.0
#include "thingsos/preview.h"
#include "thingsos/content.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
static void* allocate(size_t n) {return heap_caps_calloc(1,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
#else
static void* allocate(size_t n) {return calloc(1,n);}
#endif
#define PAGE_BYTES 16384
static bool extension(const char* a,const char* b) {
    if(!a)return false;
    while(*a && *b)if(((*a++)|32)!=((*b++)|32))return false;
    return !*a && !*b;
}
void thingsos_preview_close(thingsos_preview_t* p) {if(p){free(p->text);memset(p,0,sizeof(*p));}}
static void finish_page(thingsos_preview_t* out,size_t available) {
    size_t n=available>PAGE_BYTES?PAGE_BYTES:available;
    if(n<available)while(n && ((unsigned char)out->text[n]&0xc0)==0x80)--n;
    out->next=n<available?out->offset+n:0;
    out->text[n]=0;
}
bool thingsos_preview_read(const char* path,size_t chapter,size_t offset,thingsos_preview_t* out) {
    if(!path || !out)return false;
    memset(out,0,sizeof(*out));out->chapter=chapter;out->offset=offset;
    const char* ext=strrchr(path,'.');bool epub=extension(ext,".epub");
    out->text=allocate(PAGE_BYTES+5);if(!out->text)return false;
    bool ok=false;
    if(epub) {
        thingsos_epub_t* e=allocate(sizeof(*e));thingsos_chapter_t* c=allocate(sizeof(*c));
        if(e && c && thingsos_epub_open(e,path)) {
            thingsos_epub_navigation(e);out->chapters=e->spine_count;
            if(chapter<out->chapters && thingsos_epub_chapter(e,chapter,c) && offset<=c->bytes) {
                size_t bytes=c->bytes-offset,n=bytes>PAGE_BYTES+4?PAGE_BYTES+4:bytes;
                memcpy(out->text,c->text+offset,n);finish_page(out,n);
                if(offset+strlen(out->text)<c->bytes)out->next=offset+strlen(out->text);
                snprintf(out->title,sizeof(out->title),"%s",e->chapter_names[chapter][0]?e->chapter_names[chapter]:e->title);
                ok=true;
            }
        }
        if(c)thingsos_chapter_close(c);
        if(e)thingsos_epub_close(e);
        free(c);free(e);
    } else if((extension(ext,".txt") || extension(ext,".md") || extension(ext,".markdown")) && !chapter && offset<=LONG_MAX) {
        FILE* f=fopen(path,"rb");
        if(f && !fseek(f,(long)offset,SEEK_SET)) {
            size_t n=fread(out->text,1,PAGE_BYTES+4,f);
            ok=!ferror(f);out->chapters=1;finish_page(out,n);
        }
        if(f)fclose(f);
    }
    if(!ok)thingsos_preview_close(out);
    return ok;
}
