// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "image_platform/esp_heap_caps.h"
/* A board with absent PSRAM must still accept the rescue HTTP control body. */
void* heap_caps_malloc(size_t n,unsigned caps){(void)n;(void)caps;return NULL;}
void* heap_caps_calloc(size_t n,size_t b,unsigned caps){(void)n;(void)b;(void)caps;return NULL;}
void* heap_caps_realloc(void* p,size_t n,unsigned caps){(void)p;(void)n;(void)caps;return NULL;}
#define ESP_PLATFORM
#define CONFIG_SPIRAM_IGNORE_NOTFOUND 1
#include "thingsos/mem.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Rescue memory line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void) {
    unsigned char* p=thingsos_psram_alloc(15001);CHECK(p);memset(p,0x5a,15001);
    unsigned char* grown=thingsos_psram_realloc(p,32768);CHECK(grown);
    for(unsigned i=0;i<15001;++i)CHECK(grown[i]==0x5a);
    free(grown);
    p=thingsos_psram_calloc(64,256);CHECK(p);
    for(unsigned i=0;i<16384;++i)CHECK(!p[i]);free(p);
    puts("Rescue: absent PSRAM retains allocation, zeroed allocation and growth fallback");return 0;
}
