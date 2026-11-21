/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"keyboard present line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define ESP_LOGI(...) ((void)0)
typedef struct {int x,y,width,height;} EpdRect;
enum EpdDrawError {EPD_DRAW_SUCCESS=0,EPD_DRAW_FAILURE=1,EPD_DRAW_INVALID_CROP=64};
enum EpdDrawMode {MODE_DU=1,MODE_GC16=2,MODE_GL16=5,MODE_PACKING_1PPB_DIFFERENCE=64};
typedef struct {
    uint8_t *front_fb,*back_fb,*difference_fb,*dirty_columns;
    bool* dirty_lines;
    const int* waveform;
} EpdiyHighlevelState;
static EpdiyHighlevelState hl;
static int source_wave,fast_wave,changed_waveform;
static const int* dark_refresh_wave=&changed_waveform;
static const int* panel_wave=&source_wave;
static const int* expected_wave;
static enum EpdDrawMode expected_mode;
static const int* thingsos_panel_fast_waveform(void){return &fast_wave;}
static unsigned calls,diffs;
static bool fail_scan;
static EpdRect expected[2];
static size_t expected_count;
static int64_t tick;
static int epd_width(void){return 1216;}
static int epd_height(void){return 684;}
static EpdRect epd_full_screen(void){return (EpdRect){0,0,1216,684};}
/* Existing board rotation. The production function under test owns masks. */
static EpdRect _inverse_rotated_area(int x,int y,int width,int height){return (EpdRect){y,684-x-width,height,width};}
static int64_t esp_timer_get_time(void){return ++tick*1000;}
static void hl_record_timing(int diff,int draw,int copy){CHECK(diff>=0 && draw>=0 && copy>=0);}
static void epd_hl_waveform(EpdiyHighlevelState* state,const int* wave){state->waveform=wave;}
static unsigned pixel(const uint8_t* data,int x,int y){return (data[(size_t)y*608+x/2]>>(4*(x&1)))&15;}
static bool selected(int x){
    for(size_t i=0;i<expected_count;++i)
        if(x>=expected[i].y && x<expected[i].y+expected[i].height)return true;
    return false;
}
static EpdRect epd_difference_image_cropped(const uint8_t* to,const uint8_t* from,EpdRect area,
    uint8_t* diff,bool* lines,uint8_t* columns){
    ++diffs;
    /* Real vector diff rounds outwards. Include padding and the button gap
       here so the scan test catches any leak in the production masks. */
    int left=area.x&~31,right=(area.x+area.width+31)&~31;
    memset(lines,1,684*sizeof(bool));memset(columns,255,608);
    for(int y=0;y<684;++y)for(int x=left;x<right;++x)
        diff[(size_t)y*1216+x]=(uint8_t)(pixel(to,x,y)|(pixel(from,x,y)<<4));
    return area;
}
static enum EpdDrawError epd_draw_base(EpdRect area,const uint8_t* diff,EpdRect crop,
    int mode,int temperature,const bool* lines,const uint8_t* columns,const int* wave){
    ++calls;
    CHECK(area.width==1216 && crop.height==684 && temperature==25);
    CHECK(mode==(expected_mode|MODE_PACKING_1PPB_DIFFERENCE) && wave==expected_wave);
    for(int y=0;y<684;++y){
        CHECK(lines[y]);
        for(int x=0;x<1216;++x){
            unsigned mask=(columns[x/2]>>(4*(x&1)))&15;
            CHECK(mask==(selected(x)?15u:0u));
            if(mask)CHECK(diff[(size_t)y*1216+x]==(pixel(hl.front_fb,x,y)|(pixel(hl.back_fb,x,y)<<4)));
        }
    }
    return fail_scan?EPD_DRAW_FAILURE:EPD_DRAW_SUCCESS;
}
#include "../metalio/components/epdiy/src/highlevel_bands.inc"
#include "../firmware/components/thingsos_board/keyboard_present.inc"
static void reset_buffers(void){
    memset(hl.front_fb,0x36,608u*684);memset(hl.back_fb,0xa5,608u*684);
    calls=diffs=0;fail_scan=false;
}
static void check_back(bool success){
    for(int y=0;y<684;++y)for(int x=0;x<1216;++x)
        CHECK(pixel(hl.back_fb,x,y)==(success && selected(x)?pixel(hl.front_fb,x,y):(x&1?10u:5u)));
}
int main(void){
    hl.front_fb=malloc(608u*684);hl.back_fb=malloc(608u*684);hl.difference_fb=malloc(1216u*684);
    hl.dirty_columns=malloc(608);hl.dirty_lines=malloc(684*sizeof(bool));
    CHECK(hl.front_fb && hl.back_fb && hl.difference_fb && hl.dirty_columns && hl.dirty_lines);
    for(unsigned layout=0;layout<2;++layout){
        expected_count=2;expected[0]=(EpdRect){0,300,684,254};expected[1]=(EpdRect){0,638,684,layout?440:116};
        expected_mode=layout?MODE_GL16:MODE_DU;expected_wave=layout?panel_wave:&fast_wave;
        reset_buffers();CHECK(keyboard_present(layout!=0,25,false,false)==EPD_DRAW_SUCCESS && calls==1 && diffs==1);check_back(true);
        reset_buffers();fail_scan=true;CHECK(keyboard_present(layout!=0,25,false,false)==EPD_DRAW_FAILURE && calls==1);check_back(false);
        expected_mode=MODE_GC16;expected_wave=&changed_waveform;
        reset_buffers();CHECK(keyboard_present(layout!=0,25,true,false)==EPD_DRAW_SUCCESS && calls==1 && diffs==1);check_back(true);
    }
    /* PIN typing drives only dots/notice and the submit state. The header,
       lock icon, numeric keys and intervening gap must all remain held. */
    expected[0]=(EpdRect){0,320,684,158};expected[1]=(EpdRect){0,1090,684,76};
    expected_mode=MODE_DU;expected_wave=&fast_wave;
    reset_buffers();CHECK(keyboard_present(false,25,false,true)==EPD_DRAW_SUCCESS && calls==1 && diffs==1);check_back(true);
    reset_buffers();fail_scan=true;CHECK(keyboard_present(false,25,false,true)==EPD_DRAW_FAILURE && calls==1);check_back(false);
    expected_mode=MODE_GC16;expected_wave=&changed_waveform;
    reset_buffers();CHECK(keyboard_present(false,25,true,true)==EPD_DRAW_SUCCESS && calls==1 && diffs==1);check_back(true);
    /* Odd endpoints must preserve the neighbour nibbles as well as the gap. */
    expected_mode=MODE_GL16;expected_wave=panel_wave;epd_hl_waveform(&hl,panel_wave);
    expected[0]=(EpdRect){0,301,684,252};expected[1]=(EpdRect){0,639,684,114};
    reset_buffers();CHECK(epd_hl_update_bands_full(&hl,MODE_GL16,25,expected,2)==EPD_DRAW_SUCCESS && calls==1);check_back(true);
    reset_buffers();CHECK(epd_hl_update_bands_full(&hl,MODE_GL16,25,NULL,2)==EPD_DRAW_INVALID_CROP && !calls && !diffs);
    CHECK(epd_hl_update_bands_full(&hl,MODE_GL16,25,expected,0)==EPD_DRAW_INVALID_CROP && !calls);
    CHECK(epd_hl_update_bands_full(&hl,MODE_GL16,25,expected,3)==EPD_DRAW_INVALID_CROP && !calls);
    expected[0].x=1;CHECK(epd_hl_update_bands_full(&hl,MODE_GL16,25,expected,2)==EPD_DRAW_INVALID_CROP && !calls);
    expected[0]=(EpdRect){0,1200,684,30};CHECK(epd_hl_update_bands_full(&hl,MODE_GL16,25,expected,2)==EPD_DRAW_INVALID_CROP && !calls);
    free(hl.front_fb);free(hl.back_fb);free(hl.difference_fb);free(hl.dirty_columns);free(hl.dirty_lines);
    puts("Single DU input / GL16 layout scan, exact gap/padding masks and atomic back-buffer publication passed");return 0;
}
