/* SPDX-License-Identifier: Apache-2.0 */
/* Account aggregate live PSRAM across the parser, ZIP and WebP decoder. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "thingsos/graphics.h"
#include "image_platform/esp_heap_caps.h"
size_t local_epub_budget=4714412,local_epub_live,local_epub_peak,local_epub_denied;
static size_t local_epub_block_limit=SIZE_MAX;
static struct {void* p;size_t bytes;} blocks[512];
void* heap_caps_malloc(size_t n,unsigned caps) {
    (void)caps;
    if(n>local_epub_budget-local_epub_live || n>local_epub_block_limit){++local_epub_denied;return NULL;}
    void* p=malloc(n);if(!p)return NULL;
    for(unsigned i=0;i<512;++i)if(!blocks[i].p){
        blocks[i].p=p;blocks[i].bytes=n;local_epub_live+=n;
        if(local_epub_live>local_epub_peak)local_epub_peak=local_epub_live;
        return p;
    }
    abort();
}
void local_epub_free(void* p) {
    if(!p)return;
    for(unsigned i=0;i<512;++i)if(blocks[i].p==p){local_epub_live-=blocks[i].bytes;blocks[i].p=NULL;break;}
    free(p);
}
void* heap_caps_calloc(size_t n,size_t bytes,unsigned caps) {
    if(bytes && n>SIZE_MAX/bytes)return NULL;
    void* p=heap_caps_malloc(n*bytes,caps);if(p)memset(p,0,n*bytes);return p;
}
void* heap_caps_realloc(void* p,size_t n,unsigned caps) {
    if(!p)return heap_caps_malloc(n,caps);
    size_t old=0;for(unsigned i=0;i<512;++i)if(blocks[i].p==p)old=blocks[i].bytes;
    void* next=heap_caps_malloc(n,caps);
    if(next){memcpy(next,p,n<old?n:old);local_epub_free(p);}return next;
}
size_t heap_caps_get_free_size(unsigned caps){(void)caps;return local_epub_budget-local_epub_live;}
size_t heap_caps_get_largest_free_block(unsigned caps){size_t n=heap_caps_get_free_size(caps);return n<local_epub_block_limit?n:local_epub_block_limit;}
int main(int argc,char** argv) {
    if(argc<2)return 2;
    if(argc>2)local_epub_budget=(size_t)strtoull(argv[2],NULL,10);
    if(argc>3)local_epub_block_limit=(size_t)strtoull(argv[3],NULL,10);
    thingsos_epub_t e={0};
    if(!thingsos_epub_open(&e,argv[1])){printf("OPEN FAILED peak=%zu denied=%zu\n",local_epub_peak,local_epub_denied);return 1;}
    thingsos_epub_navigation_fast(&e);
    printf("OPEN chapters=%zu live=%zu peak=%zu denied=%zu\n",e.spine_count,local_epub_live,local_epub_peak,local_epub_denied);
    unsigned failures=0,images=0;
    for(size_t i=0;i<e.spine_count;++i){
        thingsos_chapter_t c={0};
        if(!thingsos_epub_chapter(&e,i,&c)){printf("CHAPTER FAILED %zu\n",i);++failures;}
        thingsos_chapter_close(&c);
    }
    for(size_t i=0;i<e.zip.count;++i){
        const char* name=e.zip.entries[i].name;
        if(!strstr(name,".webp") && !strstr(name,".png") && !strstr(name,".jpg"))continue;
        unsigned char* raw=NULL,*gray=NULL;size_t bytes=0;unsigned w=0,h=0;
        bool ok=thingsos_zip_read(&e.zip,name,THINGSOS_MEMBER_BYTES,&raw,&bytes) &&
            thingsos_decode_reader_image(raw,bytes,684,1216,&gray,&w,&h);
        printf("%s IMAGE %zu %ux%u live=%zu peak=%zu denied=%zu\n",ok?"PASS":"FAIL",i,w,h,local_epub_live,local_epub_peak,local_epub_denied);
        if(!ok)++failures;
        ++images;local_epub_free(raw);local_epub_free(gray);
    }
    thingsos_epub_close(&e);
    printf("RESULT images=%u failures=%u peak=%zu denied=%zu leaked=%zu\n",images,failures,local_epub_peak,local_epub_denied,local_epub_live);
    return failures || local_epub_live?1:0;
}
