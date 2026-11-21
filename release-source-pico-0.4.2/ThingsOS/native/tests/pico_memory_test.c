// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "image_platform/esp_heap_caps.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Pico memory line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static size_t available=8u*1024u*1024u,largest=8u*1024u*1024u;
static int deny;
void* heap_caps_malloc(size_t n,unsigned caps){CHECK(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));return deny || n>largest?NULL:malloc(n);}
void* heap_caps_calloc(size_t n,size_t bytes,unsigned caps){void* p=heap_caps_malloc(n*bytes,caps);if(p)memset(p,0,n*bytes);return p;}
void* heap_caps_realloc(void* p,size_t n,unsigned caps){CHECK(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));return deny || n>largest?NULL:realloc(p,n);}
size_t heap_caps_get_free_size(unsigned caps){(void)caps;return available;}
size_t heap_caps_get_largest_free_block(unsigned caps){(void)caps;return largest;}
#define ESP_PLATFORM
#include "thingsos/mem.h"
#include "../src/webp_memory.c"
int main(void) {
    deny=1;
    CHECK(!thingsos_psram_alloc(65536) && !thingsos_psram_calloc(512,160));
    unsigned char* original=malloc(32);CHECK(original);memset(original,0x5a,32);
    CHECK(!thingsos_psram_realloc(original,65536));
    for(unsigned i=0;i<32;++i)CHECK(original[i]==0x5a);
    free(original);
    void* small=thingsos_psram_alloc(16);CHECK(small);free(small);
    CHECK(!thingsos_psram_calloc(SIZE_MAX,160));
    deny=0;
    void* a=thingsos_webp_malloc(2u*1024u*1024u);CHECK(a);
    CHECK(!thingsos_webp_malloc(2u*1024u*1024u));
    thingsos_webp_free(a);CHECK(!webp_live);
    a=thingsos_webp_calloc(3u*1024u,1024);CHECK(a && !((unsigned char*)a)[0]);
    CHECK(!thingsos_webp_malloc(1));thingsos_webp_free(a);
    largest=65536;CHECK(!thingsos_webp_malloc(65536));
    largest=8u*1024u*1024u;available=WEBP_RESERVE+1024;
    CHECK(!thingsos_webp_malloc(1024));
    available=WEBP_RESERVE-1;CHECK(!thingsos_webp_malloc(1));
    CHECK(!thingsos_webp_calloc(SIZE_MAX,2));thingsos_webp_free(NULL);
    CHECK(!webp_live);
    puts("Pico: no large SRAM fallback; WebP total budget, fragmented blocks, reserve and overflow passed");return 0;
}
