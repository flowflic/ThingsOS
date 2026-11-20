/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "e0470_epaper_waveform.h"
#include "panel_waveform.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"stagger:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define THINGSOS_STAGGER_HOST
#define THINGSOS_PANEL_BYTES (1216u*684u/2)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
#define EPD_ROT_INVERTED_PORTRAIT 3
enum EpdDrawMode {MODE_DU=1,MODE_GC16=2,MODE_GL16=5,MODE_PACKING_1PPB_DIFFERENCE=64};
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define EPD_DRAW_EMPTY_LINE_QUEUE 2
#define METALIO_EPD_PCLK_MIN_MHZ 10
#define METALIO_EPD_PCLK_MAX_MHZ 20
#define METALIO_EPD_SCAN_FULL 1
typedef struct {int x,y,width,height;} EpdRect;
enum EpdDrawError {EPD_DRAW_SUCCESS,EPD_DRAW_FAIL};
static struct {uint8_t *front_fb,*back_fb,*difference_fb,*dirty_columns;bool *dirty_lines;} hl;
static const uint8_t* const* row_tables;
static unsigned scans,fail_at,allocations,phase_hits[684][64],white_hits[684];
static const uint8_t* table_ptr[1024];static unsigned table_phase[1024],table_count;
/* The newest build wins: a band table is rebuilt with a new phase every tick. */
static int phase_of(const uint8_t* t){for(unsigned i=table_count;i-->0;)if(table_ptr[i]==t)return (int)table_phase[i];return -1;}
static int64_t wall;
static unsigned scan_us=11090;
static enum EpdDrawError fail_kind=EPD_DRAW_FAIL;
static bool oom,unchanged,expected_forward,expected_clean;
static size_t internal_free=4*1024*1024;
static const EpdWaveformPhases* reference(void){return e0470_waveform_phases(thingsos_panel_waveform(),MODE_GC16);}
static unsigned phase_count(void){const EpdWaveformPhases* p=reference();return p?(unsigned)p->phases:0;}
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
        if(!expected_clean && to==from && to!=0)want=to==15 && phase==white?2:0;
        CHECK(((p->luts[at]>>shift)&3)==want);
    }
    /* Mirror the production page layout (page p = phase << 2p) from the table
       the board actually passed in, and remember which phase each pointer is so
       the row assertions do not depend on table bytes. */
    for(unsigned to=0;to<16;++to)for(unsigned from=0;from<16;++from){
        unsigned at=(unsigned)phase*64+to*4+from/4,shift=6-2*(from%4);
        unsigned value=(p->luts[at]>>shift)&3;
        unsigned index=(to<<4)|from;
        lut[index]=(uint8_t)value;lut[256+index]=(uint8_t)(value<<2);
        lut[512+index]=(uint8_t)(value<<4);lut[768+index]=(uint8_t)(value<<6);
    }
    CHECK(table_count<1024);table_ptr[table_count]=lut;table_phase[table_count]=phase;++table_count;
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
        int phase=(int)scans-(int)band*(expected_clean?2:1);
        CHECK(rows[y]==(phase>=0 && phase<(int)phase_count()));
        if(rows[y]){
            CHECK(row_tables[y] && phase_of(row_tables[y])==phase);++phase_hits[y][phase];
            if(row_tables[y][768+0xFF]==(2<<6))++white_hits[y];
        }else CHECK(!row_tables[y]);
    }
    ++scans;wall+=scan_us;
    return scans==fail_at?fail_kind:EPD_DRAW_SUCCESS;
}
#include "../firmware/components/thingsos_board/stagger.inc"
static unsigned clears,fulls,from_whites,caps;
static enum EpdDrawError recovery_result,clear_result;
static enum EpdDrawError epd_clear_checked(void){++clears;return clear_result;}
static enum EpdDrawError epd_hl_update_screen_full(void* h,int mode,int temperature){
    (void)h;(void)temperature;CHECK(mode==MODE_GC16);++fulls;
    if(!recovery_result)memcpy(hl.back_fb,hl.front_fb,THINGSOS_PANEL_BYTES);
    return recovery_result;
}
static enum EpdDrawError epd_hl_update_screen_from_white(void* h,int mode,int temperature){
    (void)h;(void)temperature;CHECK(mode==MODE_GC16);++from_whites;
    if(!recovery_result)memcpy(hl.back_fb,hl.front_fb,THINGSOS_PANEL_BYTES);
    return recovery_result;
}
static int current_pclk=20;
static void metalio_epd_set_pclk(unsigned value){CHECK(value==16 || value==20);current_pclk=(int)value;if(value==16)++caps;}
static int metalio_epd_pclk_mhz(void){return current_pclk;}
static void metalio_epd_use_scan(unsigned value){CHECK(value==METALIO_EPD_SCAN_FULL);}
static void epd_lcd_set_prefill_lines(unsigned value){CHECK(value==127);}
#include "../firmware/components/thingsos_board/clock_recovery.inc"
#include "../firmware/components/thingsos_board/lock_wave.inc"
static void reset(void){scans=0;wall=0;row_tables=NULL;table_count=0;memset(phase_hits,0,sizeof(phase_hits));memset(white_hits,0,sizeof(white_hits));memset(hl.back_fb,255,THINGSOS_PANEL_BYTES);}
static void lock_reset(void){reset();clears=fulls=from_whites=caps=0;recovery_result=clear_result=EPD_DRAW_SUCCESS;
    memset(&clock_recovery,0,sizeof(clock_recovery));current_pclk=20;
    expected_clean=true;expected_forward=true;fail_at=0;oom=false;fail_kind=EPD_DRAW_FAIL;}
static void lock_wave_test(void){
    bool ripple_applied=false;
    CHECK(phase_count()==48);
    unsigned white_phases=0;
    for(unsigned p=0;p<48;++p)white_phases+=e0470_phase_action(reference(),p,15,15)==2;
    CHECK(white_phases>1);
    for(unsigned dir=0;dir<2;++dir){lock_reset();expected_forward=dir!=0;
        CHECK(lock_wave_turn(25,expected_forward,&ripple_applied)==EPD_DRAW_SUCCESS && ripple_applied);
        CHECK(scans==62&&!clears&&!fulls&&!from_whites&&!allocations);
        for(unsigned y=0;y<684;++y)for(unsigned p=0;p<48;++p)CHECK(phase_hits[y][p]==1);
        for(unsigned y=0;y<684;++y)CHECK(white_hits[y]==white_phases);
    }
    /* An unchanged software-white row can still carry optical wallpaper.
       Every row must receive the complete source sequence in either direction. */
    for(unsigned dir=0;dir<2;++dir){lock_reset();unchanged=true;expected_forward=dir!=0;
        CHECK(lock_wave_turn(25,expected_forward,&ripple_applied)==EPD_DRAW_SUCCESS && ripple_applied);
        CHECK(scans==62);
        for(unsigned y=0;y<684;++y)CHECK(white_hits[y]==white_phases);
        unchanged=false;
    }
    lock_reset();oom=true;CHECK(lock_wave_turn(25,true,&ripple_applied)==EPD_DRAW_SUCCESS && !ripple_applied);
    CHECK(!scans&&clears==1&&from_whites==1&&!fulls);
    lock_reset();oom=true;CHECK(lock_wave_turn(25,false,&ripple_applied)==EPD_DRAW_SUCCESS && !ripple_applied);
    CHECK(!scans&&clears==1&&from_whites==1&&!fulls);
    lock_reset();fail_at=3;CHECK(lock_wave_turn(25,true,&ripple_applied)==EPD_DRAW_SUCCESS && !ripple_applied);
    CHECK(scans==3&&clears==1&&from_whites==1&&!allocations&&!row_tables);
    lock_reset();fail_at=1;fail_kind=(enum EpdDrawError)EPD_DRAW_EMPTY_LINE_QUEUE;
    CHECK(lock_wave_turn(25,true,&ripple_applied)==EPD_DRAW_SUCCESS&&caps==1&&from_whites==1 && !ripple_applied);
    CHECK(clock_recovery.pending && !clock_recovery.safe_ready && current_pclk==16);
    clock_note_result(true);wall+=2000000;clock_retry_step();
    CHECK(current_pclk==20 && !clock_recovery.pending);
    lock_reset();fail_at=1;recovery_result=EPD_DRAW_FAIL;expected_forward=false;
    CHECK(lock_wave_turn(25,false,&ripple_applied)==EPD_DRAW_FAIL&&!allocations && !ripple_applied);
    for(unsigned i=0;i<THINGSOS_PANEL_BYTES;++i)CHECK(hl.back_fb[i]==255);
    lock_reset();fail_at=1;clear_result=EPD_DRAW_EMPTY_LINE_QUEUE;
    CHECK(lock_wave_turn(25,true,&ripple_applied)==EPD_DRAW_EMPTY_LINE_QUEUE && !ripple_applied);
    CHECK(clears==1 && !from_whites && !fulls);
    for(unsigned i=0;i<THINGSOS_PANEL_BYTES;++i)CHECK(hl.back_fb[i]==255);
    lock_reset();
}
static void reader_black_correction(void) {
    const EpdWaveformPhases* source=e0470_waveform_phases(thingsos_panel_turn_waveform(),MODE_GC16);
    CHECK(source && source->phases==36 && !source->phase_times);
    uint8_t original[36*64],baseline[36*64],candidate[36*64+2];
    memcpy(original,source->luts,sizeof(original));
    CHECK(thingsos_panel_text_wave(source->luts,36,baseline,sizeof(baseline)));
    memset(candidate,0xa5,sizeof(candidate));
    CHECK(thingsos_panel_reader_text_wave(source->luts,36,candidate+1,sizeof(baseline)));
    unsigned black=0,white=0,hold=0,changed=0;
    for(int p=0;p<36;++p)for(unsigned to=0;to<16;++to)for(unsigned from=0;from<16;++from) {
        unsigned at=(unsigned)p*64+to*4+from/4,shift=6-2*(from%4);
        unsigned before=(baseline[at]>>shift)&3,after=(candidate[1+at]>>shift)&3;
        unsigned want=to==0 && from==0?(source->luts[at]>>shift)&3:before;
        CHECK(after==want);
        changed+=after!=before;
        if(!to && !from){black+=after==1;white+=after==2;hold+=after==0;}
    }
    CHECK(black==12 && white==11 && hold==13 && changed==23);
    CHECK(candidate[0]==0xa5 && candidate[sizeof(candidate)-1]==0xa5);
    CHECK(!memcmp(original,source->luts,sizeof(original)));
    CHECK(!thingsos_panel_reader_text_wave(source->luts,36,candidate,sizeof(baseline)-1));
    CHECK(!thingsos_panel_reader_text_wave(source->luts,36,(uint8_t*)source->luts,sizeof(baseline)));
    CHECK(!thingsos_panel_reader_text_wave(NULL,36,candidate,sizeof(baseline)));
    CHECK(!thingsos_panel_reader_text_wave(source->luts,36,NULL,sizeof(baseline)));
    CHECK(!thingsos_panel_reader_text_wave(source->luts,48,candidate,sizeof(candidate)));
    /* The UI view must be the same corrected table: home icons and labels have
       the same unchanged-black problem as reader text. */
    uint8_t ui[36*64];
    CHECK(thingsos_panel_ui_text_wave(source->luts,36,ui,sizeof(ui)));
    CHECK(!memcmp(ui,candidate+1,sizeof(ui)));
    CHECK(!thingsos_panel_ui_text_wave(source->luts,48,ui,sizeof(ui)));
    CHECK(!thingsos_panel_ui_text_wave(NULL,36,ui,sizeof(ui)));
    puts("PASS: reader 36-phase black correction: 12 black/11 white/13 hold; only 23 black-to-black actions differ");
}
static void reader_ripple_black_correction(void) {
    const EpdWaveformPhases* source=reference();
    CHECK(source && source->phases==48 && !source->phase_times);
    uint8_t original[48*64],baseline[48*64],candidate[48*64+2];
    memcpy(original,source->luts,sizeof(original));
    CHECK(thingsos_panel_text_wave(source->luts,48,baseline,sizeof(baseline)));
    memset(candidate,0xa5,sizeof(candidate));
    CHECK(thingsos_panel_reader_ripple_wave(source->luts,48,candidate+1,sizeof(baseline)));
    unsigned black=0,white=0,hold=0,changed=0;
    for(int p=0;p<48;++p)for(unsigned to=0;to<16;++to)for(unsigned from=0;from<16;++from) {
        unsigned at=(unsigned)p*64+to*4+from/4,shift=6-2*(from%4);
        unsigned before=(baseline[at]>>shift)&3,after=(candidate[1+at]>>shift)&3;
        unsigned want=to==0 && from==0?(source->luts[at]>>shift)&3:before;
        CHECK(after==want);changed+=after!=before;
        if(!to && !from){black+=after==1;white+=after==2;hold+=after==0;}
    }
    CHECK(black && white && black+white+hold==48 && changed==black+white);
    /* Inline image pages rely on the 48-phase ripple/corrected wave preserving
       every untrimmed black saturation phase from white (15->to) before white pull-up. */
    for(unsigned to=0;to<15;++to){
        unsigned img_black=0,src_black=0;
        for(int p=0;p<48;++p){
            unsigned at=(unsigned)p*64+to*4+15/4,shift=6-2*(15%4);
            img_black+=((candidate[1+at]>>shift)&3)==1;
            src_black+=((source->luts[at]>>shift)&3)==1;
        }
        CHECK(img_black==src_black && img_black>=16);
    }
    CHECK(candidate[0]==0xa5 && candidate[sizeof(candidate)-1]==0xa5);
    CHECK(!memcmp(original,source->luts,sizeof(original)));
    CHECK(!thingsos_panel_reader_ripple_wave(source->luts,48,candidate,sizeof(baseline)-1));
    CHECK(!thingsos_panel_reader_ripple_wave(source->luts,48,(uint8_t*)source->luts,sizeof(baseline)));
    CHECK(!thingsos_panel_reader_ripple_wave(NULL,48,candidate,sizeof(baseline)));
    CHECK(!thingsos_panel_reader_ripple_wave(source->luts,48,NULL,sizeof(baseline)));
    CHECK(!thingsos_panel_reader_ripple_wave(source->luts,36,candidate,sizeof(candidate)));
    printf("PASS: ripple 48-phase black correction: %u black/%u white/%u hold; only %u black-to-black actions differ\n",black,white,hold,changed);
}
int main(void){
    e0470_waveform_init();
    reader_black_correction();
    reader_ripple_black_correction();
    /* The board must resolve the vendor-aligned, untrimmed tables. */
    CHECK(thingsos_panel_waveform_is_backed());
    CHECK(reference() && reference()->luts && !reference()->phase_times);
    CHECK(phase_count()==THINGSOS_PANEL_GC16_PHASES);
    /* A complete UI refresh must retain all 48 source phases and every one
       of the 256 transitions, including stable white. Reader/key trim stays. */
    const EpdWaveform* ui_wave=thingsos_panel_navigation_waveform(false,false);
    const EpdWaveformPhases* ui=e0470_waveform_phases(ui_wave,MODE_GC16);
    const EpdWaveformPhases* full=e0470_waveform_phases(&E0470_FULL_WAVEFORM,MODE_GC16);
    CHECK(ui&&full&&ui->phases==48&&full->phases==48);
    CHECK(!memcmp(ui->luts,full->luts,48*64));
    const EpdWaveformPhases* menu = e0470_waveform_phases(thingsos_panel_menu_waveform(), MODE_GC16);
    CHECK(menu && menu->phases == full->phases);
    unsigned menu_white_pulses = 0;
    for (int p = 0; p < menu->phases; ++p) {
        int stable_white = e0470_phase_action(menu, p, 15, 15);
        CHECK(stable_white == 0 || (stable_white == 2 && e0470_phase_action(full, p, 15, 15) == 2));
        menu_white_pulses += stable_white == 2;
        for (int from = 0; from < 15; ++from) {
            CHECK(e0470_phase_action(menu, p, 15, from) == e0470_phase_action(full, p, 15, from));
        }
    }
    CHECK(menu_white_pulses == 2);
    CHECK(thingsos_panel_navigation_waveform(true,false)==thingsos_panel_turn_waveform());
    CHECK(thingsos_panel_navigation_waveform(false,true)==thingsos_panel_turn_waveform());
    CHECK(e0470_waveform_phases(thingsos_panel_waveform(),MODE_GL16)!=NULL);
    CHECK(e0470_waveform_phases(thingsos_panel_waveform(),MODE_DU)!=NULL);
    /* Real DU regression: the original diff path cannot drive stable white.
       The reader view adds only two white ticks, never a black inversion. */
    const EpdWaveformPhases* du=e0470_waveform_phases(thingsos_panel_waveform(),MODE_DU);
    const EpdWaveform* fast_wave=thingsos_panel_fast_waveform();
    const EpdWaveformPhases* fast=e0470_waveform_phases(fast_wave,MODE_DU);
    CHECK(fast && fast!=du && fast->phases==20 && fast->phase_times==du->phase_times);
    unsigned white_ticks=0;
    for(int p=0;p<du->phases;++p){
        bool source_white=false;
        for(int from=0;from<15;++from)source_white|=e0470_phase_action(du,p,15,from)==2;
        CHECK(e0470_phase_action(du,p,15,15)==0);
        int stable=e0470_phase_action(fast,p,15,15);
        CHECK(stable==0 || (stable==2 && source_white));
        white_ticks+=stable==2;
        for(int to=0;to<16;++to)for(int from=0;from<16;++from)
            if(to!=15 || from!=15)CHECK(e0470_phase_action(fast,p,to,from)==e0470_phase_action(du,p,to,from));
    }
    CHECK(white_ticks==2 && thingsos_panel_fast_waveform()==fast_wave);
    CHECK(e0470_waveform_phases(fast_wave,MODE_GC16)==reference());
    const EpdWaveform* dark_wave=thingsos_panel_dark_waveform();
    CHECK(dark_wave && dark_wave==thingsos_panel_dark_waveform());
    CHECK(dark_wave->temp_intervals==thingsos_panel_waveform()->temp_intervals);
    const EpdWaveformPhases* dark_gc=e0470_waveform_phases(dark_wave,MODE_GC16);
    const EpdWaveformPhases* dark_du=e0470_waveform_phases(dark_wave,MODE_DU);
    CHECK(dark_gc && dark_gc->phases==full->phases && dark_gc->phase_times==full->phase_times);
    CHECK(dark_du && dark_du->phases==du->phases && dark_du->phase_times==du->phase_times);
    unsigned erase_ticks=0,black_ticks=0;
    for(int p=0;p<dark_gc->phases;++p) {
        erase_ticks+=e0470_phase_action(dark_gc,p,0,15)==1;
        for(int to=0;to<16;++to)for(int from=0;from<16;++from)
            CHECK(e0470_phase_action(dark_gc,p,to,from)==(to==from?0:e0470_phase_action(full,p,to,from)));
    }
    CHECK(erase_ticks==17);
    for(int p=du->phases-1;p>=0;--p) {
        int stable_black=black_ticks<2 && e0470_phase_action(du,p,0,15)==1;
        black_ticks+=stable_black;
        for(int to=0;to<16;++to)for(int from=0;from<16;++from) {
            int action=e0470_phase_action(dark_du,p,to,from);
            CHECK(action==(to==0 && from==0?stable_black:e0470_phase_action(du,p,to,from)));
            if(to==0)CHECK(action<=1);
        }
    }
    CHECK(black_ticks==2);
    hl.front_fb=calloc(1,THINGSOS_PANEL_BYTES);hl.back_fb=malloc(THINGSOS_PANEL_BYTES);
    hl.difference_fb=malloc(1216*684);hl.dirty_columns=malloc(608);hl.dirty_lines=malloc(684*sizeof(bool));
    CHECK(hl.front_fb && hl.back_fb && hl.difference_fb && hl.dirty_columns && hl.dirty_lines);
    for(unsigned clean=0;clean<2;++clean){expected_clean=clean!=0;
      unsigned ticks=phase_count()+7*(expected_clean?2:1);
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
        unsigned white_count=0;
        for(unsigned p=0;p<phase_count();++p)white_count+=e0470_phase_action(reference(),p,15,15)==2;
        for(unsigned y=0;y<684;++y)CHECK(white_hits[y]==(expected_clean?white_count:1));
      }
      unchanged=false;
    }
    lock_wave_test();
    puts("PASS: corrected reader actions, lock/wake complete 48-phase/62-tick GC16 ripple including equal-white rows, scan/OOM recovery and frame history safety");
    free(hl.front_fb);free(hl.back_fb);free(hl.difference_fb);free(hl.dirty_columns);free(hl.dirty_lines);return 0;
}
