/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "e0470_epaper_waveform.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"stagger:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define THINGSOS_STAGGER_HOST
#define THINGSOS_PANEL_BYTES (1216u*684u/2)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
#define EPD_ROT_INVERTED_PORTRAIT 3
enum EpdDrawMode {MODE_GC16=2,MODE_GL16=5,MODE_PACKING_1PPB_DIFFERENCE=64};
#define ESP_LOGI(...) ((void)0)
typedef struct {int x,y,width,height;} EpdRect;
enum EpdDrawError {EPD_DRAW_SUCCESS,EPD_DRAW_FAIL};
static struct {uint8_t *front_fb,*back_fb,*difference_fb,*dirty_columns;bool *dirty_lines;} hl;
static const uint8_t* const* row_tables;
static unsigned scans,fail_at,allocations,phase_hits[684][37],white_hits[684];
static int64_t wall;
static unsigned scan_us=11090;
static bool oom,unchanged,expected_forward,expected_clean;
static size_t internal_free=4*1024*1024;
static unsigned phase_count(void){return E0470_GC16_FRAMES;}
static const EpdWaveformPhases* reference(void){return e0470_waveform_phases(&E0470_WAVEFORM,MODE_GC16);}
static size_t heap_caps_get_free_size(int caps){CHECK(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));return internal_free;}
static void* heap_caps_aligned_alloc(int alignment,size_t n,int caps){CHECK(alignment==16 && n==8192 && caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));if(oom)return NULL;++allocations;return malloc(n);}
static void heap_caps_free(void* p){if(p)--allocations;free(p);}
static int epd_get_rotation(void){return EPD_ROT_INVERTED_PORTRAIT;}
static int epd_width(void){return 1216;}
static int epd_height(void){return 684;}
static void epd_build_1ppB_lut_1k(uint8_t* lut,const EpdWaveformPhases* p,int phase){
    /* Real pinned BSP tables: equal-white actions matter too, not just the
       240 changed transitions tested by the previous hold-mask oracle. */
    const EpdWaveformPhases* source=reference();
    int white=-1;for(int f=source->phases-1;f>=0;--f)if(e0470_phase_action(source,f,15,15)==2){white=f;break;}
    for(unsigned to=0;to<16;++to)for(unsigned from=0;from<16;++from){
        unsigned at=(unsigned)phase*64+to*4+from/4,shift=6-2*(from%4);
        unsigned want=(source->luts[at]>>shift)&3;
        if(p->luts!=source->luts && to==from)want=to==15 && phase==white?2:0;
        CHECK(((p->luts[at]>>shift)&3)==want);
    }
    memset(lut,phase,1024);
    lut[1]=e0470_phase_action(p,phase,15,15)==2;
    lut[2]=p->luts==source->luts;
}
static EpdRect epd_full_screen(void){return (EpdRect){0,0,1216,684};}
static EpdRect epd_difference_image_cropped(const uint8_t* to,const uint8_t* from,EpdRect r,
    uint8_t* diff,bool* rows,uint8_t* cols){
    (void)to;(void)from;(void)diff;
    /* Unchanged rows and columns must still receive the white correction. */
    memset(rows,0,684);memset(cols,0,608);
    if(!unchanged){rows[342]=true;cols[300]=255;}
    return unchanged?(EpdRect){0,0,0,0}:r;
}
static int64_t esp_timer_get_time(void){return wall;}
static void esp_rom_delay_us(uint32_t us){wall+=us;}
static void epd_leading_skip_discard(void){}
static void thingsos_scan_row_tables(const uint8_t* const* p){row_tables=p;}
static enum EpdDrawError epd_draw_base(EpdRect a,const uint8_t* pixels,EpdRect crop,int mode,
    int temp,const bool* rows,const uint8_t* cols,const EpdWaveform* wave){
    (void)a;(void)pixels;(void)crop;(void)temp;
    CHECK(mode==(MODE_PACKING_1PPB_DIFFERENCE|MODE_GC16));
    CHECK(wave->mode_data[0]->range_data[0]->phases==1 && row_tables);
    for(unsigned x=0;x<608;++x)CHECK(cols[x]==255);
    for(unsigned y=0;y<684;++y){
        unsigned band=y*8/684;if(!expected_forward)band=7-band;
        int phase=(int)scans-(int)band;
        CHECK(rows[y]==(phase>=0 && phase<(int)phase_count()));
        if(rows[y]){
            CHECK(row_tables[y] && row_tables[y][0]==phase);++phase_hits[y][phase];
            CHECK(row_tables[y][2]==expected_clean);
            if(row_tables[y][1])++white_hits[y];
        }else CHECK(!row_tables[y]);
    }
    ++scans;wall+=scan_us;
    return scans==fail_at?EPD_DRAW_FAIL:EPD_DRAW_SUCCESS;
}
#include "../firmware/components/thingsos_board/stagger.inc"
static void reset(void){scans=0;wall=0;row_tables=NULL;memset(phase_hits,0,sizeof(phase_hits));memset(white_hits,0,sizeof(white_hits));memset(hl.back_fb,255,THINGSOS_PANEL_BYTES);}
int main(void){
    e0470_waveform_init();
    hl.front_fb=calloc(1,THINGSOS_PANEL_BYTES);hl.back_fb=malloc(THINGSOS_PANEL_BYTES);
    hl.difference_fb=malloc(1216*684);hl.dirty_columns=malloc(608);hl.dirty_lines=malloc(684*sizeof(bool));
    CHECK(hl.front_fb && hl.back_fb && hl.difference_fb && hl.dirty_columns && hl.dirty_lines);
    for(unsigned clean=0;clean<2;++clean){expected_clean=clean!=0;
      unsigned ticks=phase_count()+7;
      for(unsigned dir=0;dir<2;++dir){expected_forward=dir!=0;
        for(unsigned slow=0;slow<2;++slow){reset();scan_us=slow?18000:7000;bool attempted=false;
            CHECK(stagger_turn(25,expected_forward,expected_clean,&attempted)==EPD_DRAW_SUCCESS && attempted);
            CHECK(scans==ticks && wall==(int64_t)ticks*(slow?18000:11090));
            CHECK(!memcmp(hl.front_fb,hl.back_fb,THINGSOS_PANEL_BYTES) && !row_tables && !allocations);
            for(unsigned y=0;y<684;++y)for(unsigned p=0;p<phase_count();++p)CHECK(phase_hits[y][p]==1);
        }
        for(fail_at=1;fail_at<=ticks;++fail_at){reset();bool attempted=false;
            CHECK(stagger_turn(25,expected_forward,expected_clean,&attempted)==EPD_DRAW_FAIL && attempted);
            CHECK(scans==fail_at && !row_tables && !allocations);
            for(unsigned i=0;i<THINGSOS_PANEL_BYTES;++i)CHECK(hl.back_fb[i]==255);
        }fail_at=0;
      }
      reset();oom=true;bool attempted=true;
      CHECK(stagger_turn(25,true,expected_clean,&attempted)==EPD_DRAW_SUCCESS && !attempted && !scans);oom=false;
      internal_free=20*1024;attempted=true;
      CHECK(stagger_turn(25,true,expected_clean,&attempted)==EPD_DRAW_SUCCESS && !attempted && !scans && !allocations);
      internal_free=4*1024*1024;
      unchanged=true;expected_forward=true;
      for(unsigned turn=0;turn<5;++turn){reset();attempted=false;
        CHECK(stagger_turn(25,true,expected_clean,&attempted)==EPD_DRAW_SUCCESS && attempted && scans==ticks && !allocations);
        if(!expected_clean)for(unsigned y=0;y<684;++y)CHECK(white_hits[y]==1);
      }
      unchanged=false;
    }
    puts("PASS: original GC16 text/full waveform, both directions, every phase and physical row; failure and allocation safety");
    free(hl.front_fb);free(hl.back_fb);free(hl.difference_fb);free(hl.dirty_columns);free(hl.dirty_lines);return 0;
}
