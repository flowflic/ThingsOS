/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "e0470_waveform_trim.h"
#include "e0470_epaper_waveform.h"
#include "epdiy.h"
#include "esp_heap_caps.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"wave workspace:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
int thingsos_trim_reference(const EpdWaveformPhases*,const e0470_trim_t*,uint8_t (*)[16][4]);
static bool psram=true,oom;
static unsigned calls,live,last_caps;
bool esp_psram_is_initialized(void){return psram;}
void* heap_caps_malloc(size_t bytes,unsigned caps){
    CHECK(bytes==17408);++calls;last_caps=caps;
    if(oom)return NULL;
    void* p=malloc(bytes);CHECK(p);++live;return p;
}
void heap_caps_free(void* p){CHECK(p && live==1);--live;free(p);}
static void compare(const EpdWaveformPhases* src,const e0470_trim_t* trim){
    uint8_t want[64][16][4],got[64][16][4];
    memset(want,0xa5,sizeof(want));memset(got,0xa5,sizeof(got));
    int phases=thingsos_trim_reference(src,trim,want);
    CHECK(e0470_waveform_trim(src,trim,got)==phases && !live);
    CHECK(!memcmp(got,want,sizeof(got)));
    CHECK(last_caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
}
int main(void){
    e0470_waveform_init();
    const EpdWaveformPhases* du=e0470_waveform_phases(&E0470_WAVEFORM,MODE_DU);
    CHECK(du && du->phases==E0470_FULL_DU_FRAMES);
    for(unsigned from=0;from<16;++from) {
        bool dark_drive=false;
        for(unsigned phase=0;phase<(unsigned)du->phases;++phase) {
            unsigned action=(du->luts[phase*64+from/4]>>(6-2*(from%4)))&3;
            CHECK(action<=1); /* Driving any gray to black has no white pulse. */
            dark_drive|=action==1;
        }
        CHECK(!from || dark_drive);
    }
    const e0470_trim_t trims[]={{11,5,0,3},{0,0,0,1},{64,0,0,3},{4,2,1,3}};
    const int modes[]={MODE_GC16,MODE_GL16};
    for(unsigned m=0;m<2;++m){
        const EpdWaveformPhases* src=e0470_waveform_phases(&E0470_FULL_WAVEFORM,modes[m]);
        CHECK(src && src->phases==48);
        for(unsigned t=0;t<4;++t)compare(src,&trims[t]);
        uint8_t dst[64][16][4],before[sizeof(dst)];memset(dst,0x5a,sizeof(dst));memcpy(before,dst,sizeof(dst));
        oom=true;CHECK(e0470_waveform_trim(src,&trims[0],dst)==0 && !live);
        CHECK(!memcmp(dst,before,sizeof(dst)));oom=false;
        psram=false;unsigned old_calls=calls;
#ifdef THINGSOS_TRIM_RESCUE
        CHECK(e0470_waveform_trim(src,&trims[0],dst)>0 && calls==old_calls+1 && !live);
        CHECK(last_caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
#else
        CHECK(e0470_waveform_trim(src,&trims[0],dst)==0 && calls==old_calls && !live);
#endif
        psram=true;compare(src,&trims[0]);
    }
    unsigned old_calls=calls;uint8_t dst[64][16][4];
    CHECK(!e0470_waveform_trim(NULL,&trims[0],dst) && calls==old_calls);
    puts("PASS: complete GC16/GL16 byte equivalence; phase/hold preservation; temporary workspace freed on success/overflow; OOM retry; main/rescue allocation policy");
    return 0;
}
