/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"loading:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define ESP_LOGI(...) ((void)0)
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int width,height;} thingsos_canvas_t;
enum EpdDrawMode {MODE_DU,MODE_GL16,MODE_GC16};
enum EpdDrawError {EPD_DRAW_SUCCESS,EPD_DRAW_FAILED};
static EpdRect loading_area,submitted;
static int hl,E0470_FOLLOW_WAVEFORM;
static unsigned differential,full,follow;
static enum EpdDrawError outcome;
static enum EpdDrawMode submitted_mode;
static void epd_hl_waveform(int* h,int* w){CHECK(h==&hl && w==&E0470_FOLLOW_WAVEFORM);++follow;}
static enum EpdDrawError epd_hl_update_area(int* h,enum EpdDrawMode m,int t,EpdRect a){
    CHECK(h==&hl && t==25);++differential;submitted=a;submitted_mode=m;return outcome;
}
static enum EpdDrawError epd_hl_update_area_full(int* h,enum EpdDrawMode m,int t,EpdRect a){
    CHECK(h==&hl && t==25);++full;submitted=a;submitted_mode=m;return outcome;
}
#include "../firmware/components/thingsos_board/loading_present.inc"
int main(void){
    thingsos_canvas_t c={684,1216};enum EpdDrawMode mode;
    loading_area=(EpdRect){0,0,684,1216};
    CHECK(loading_present(&c,false,25,&mode)==EPD_DRAW_SUCCESS);
    CHECK(mode==MODE_GL16 && submitted_mode==MODE_GL16 && !differential && full==1 && !follow);
    CHECK(submitted.x==0 && submitted.y==0 && submitted.width==684 && submitted.height==1216);
    c=(thingsos_canvas_t){1216,684};loading_area=(EpdRect){0,0,1216,684};
    CHECK(loading_present(&c,true,25,&mode)==EPD_DRAW_SUCCESS);
    CHECK(mode==MODE_GL16 && !differential && full==2 && !follow);
    CHECK(submitted.x==0 && submitted.y==0 && submitted.width==684 && submitted.height==1216);
    loading_area=(EpdRect){332,117,552,450};
    CHECK(loading_present(&c,true,25,&mode)==EPD_DRAW_SUCCESS);
    CHECK(mode==MODE_GL16 && !differential && full==3 && !follow);
    CHECK(submitted.x==117 && submitted.y==332 && submitted.width==450 && submitted.height==552);
    loading_area=(EpdRect){576,164,64,66};
    CHECK(loading_present(&c,true,25,&mode)==EPD_DRAW_SUCCESS);
    CHECK(mode==MODE_DU && differential==1 && full==3 && follow==1);
    CHECK(submitted.x==454 && submitted.y==576 && submitted.width==66 && submitted.height==64);
    c=(thingsos_canvas_t){684,1216};loading_area=(EpdRect){0,0,684,74};
    CHECK(loading_present(&c,false,25,&mode)==EPD_DRAW_SUCCESS);
    CHECK(mode==MODE_GL16 && differential==1 && full==4 && follow==1);
    CHECK(submitted.x==0 && submitted.y==0 && submitted.width==684 && submitted.height==74);
    c=(thingsos_canvas_t){1216,684};
    outcome=EPD_DRAW_FAILED;loading_area=(EpdRect){0,0,1216,684};
    CHECK(loading_present(&c,true,25,&mode)==EPD_DRAW_FAILED);
    CHECK(mode==MODE_GL16 && differential==1 && full==5 && follow==1);
    puts("PASS: loading entry/exit delivers GL16 equal-white maintenance without GC16; card/spinner and rotated geometry retained; errors propagate");
    return 0;
}
