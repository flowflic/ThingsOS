// SPDX-License-Identifier: Apache-2.0
/* Single UI-owner decoder budget. No allocation may spill into internal SRAM. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
enum { WEBP_BUDGET = 3u*1024u*1024u, WEBP_RESERVE = 512u*1024u };
/* A 16-byte header preserves malloc alignment on both Pico and the host. */
typedef union { long double alignment; void* pointer; size_t bytes; unsigned char padding[16]; } webp_allocation_t;
static size_t webp_live;
void* thingsos_webp_malloc(size_t bytes) {
    if(!bytes || bytes>WEBP_BUDGET-webp_live || bytes>SIZE_MAX-sizeof(webp_allocation_t))return NULL;
    size_t request=sizeof(webp_allocation_t)+bytes;
#ifdef ESP_PLATFORM
    size_t available=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if(available<WEBP_RESERVE || request>available-WEBP_RESERVE)return NULL;
    webp_allocation_t* p=heap_caps_malloc(request,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    webp_allocation_t* p=malloc(request);
#endif
    if(!p)return NULL;
    p->bytes=bytes;webp_live+=bytes;return p+1;
}
void* thingsos_webp_calloc(size_t count,size_t bytes) {
    if(bytes && count>SIZE_MAX/bytes)return NULL;
    size_t total=count*bytes;void* p=thingsos_webp_malloc(total);
    if(p)memset(p,0,total);
    return p;
}
void thingsos_webp_free(void* pointer) {
    if(!pointer)return;
    webp_allocation_t* p=(webp_allocation_t*)pointer-1;
    webp_live-=p->bytes;free(p);
}
