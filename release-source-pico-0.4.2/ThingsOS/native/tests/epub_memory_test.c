// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "image_platform/esp_heap_caps.h"
static int fail_at=-1,calls;
static void* blocks[32];
static unsigned live;
static void* tracked_allocate(size_t bytes) {
    if(calls++==fail_at)return NULL;
    void* p=malloc(bytes);if(!p)return NULL;
    for(unsigned i=0;i<32;++i)if(!blocks[i]){blocks[i]=p;++live;return p;}
    abort();
}
static void tracked_free(void* p) {
    if(!p)return;
    for(unsigned i=0;i<32;++i)if(blocks[i]==p){blocks[i]=NULL;--live;break;}
    free(p);
}
void* heap_caps_malloc(size_t n,unsigned caps){(void)caps;return tracked_allocate(n);}
void* heap_caps_calloc(size_t n,size_t bytes,unsigned caps){(void)caps;void* p=tracked_allocate(n*bytes);if(p)memset(p,0,n*bytes);return p;}
void* heap_caps_realloc(void* p,size_t n,unsigned caps){(void)p;(void)n;(void)caps;abort();}
#define ESP_PLATFORM
#define free tracked_free
#include "../src/epub.c"
#undef free
#define CHECK(x) do{if(!(x)){fprintf(stderr,"EPUB memory line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void) {
    thingsos_epub_t e={0};
    CHECK(sizeof(e)<4096 && thingsos_epub_reserve_chapters(&e,100) && live==5);
    strcpy(e.chapter_names[99],"last title");e.spine[99]=93;e.chapter_sources[99]=84;e.chapter_levels[99]=3;
    void* names=e.chapter_names;void* spine=e.spine;
    for(int fail=0;fail<4;++fail) {
        calls=0;fail_at=fail;
        CHECK(!thingsos_epub_reserve_chapters(&e,2048));
        CHECK(e.chapter_capacity==100 && e.chapter_names==names && e.spine==spine && live==5);
        CHECK(!strcmp(e.chapter_names[99],"last title") && e.spine[99]==93 && e.chapter_sources[99]==84 && e.chapter_levels[99]==3);
    }
    fail_at=-1;CHECK(thingsos_epub_reserve_chapters(&e,2048) && live==5);
    CHECK(!strcmp(e.chapter_names[99],"last title") && e.spine[99]==93 && e.chapter_sources[99]==84 && e.chapter_levels[99]==3);
    thingsos_epub_close(&e);CHECK(!live && !e.spine && !e.chapter_names);
    printf("EPUB state=%zu bytes; 329 metadata bytes/chapter; growth rollback retains all arrays and frees temporary allocations\n",sizeof(e));return 0;
}
