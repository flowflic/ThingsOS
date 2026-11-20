/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"clock:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define METALIO_EPD_PCLK_MIN_MHZ 10
#define METALIO_EPD_PCLK_MAX_MHZ 20
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
static int pclk=20;
static int64_t now;
static unsigned switches, prefill;
static int64_t esp_timer_get_time(void){return now;}
static void metalio_epd_set_pclk(int mhz){CHECK(mhz==16 || mhz==20);pclk=mhz;++switches;}
static int metalio_epd_pclk_mhz(void){return pclk;}
static void epd_lcd_set_prefill_lines(unsigned lines){prefill=lines;}
#include "../firmware/components/thingsos_board/clock_recovery.inc"
#define ESP_LOGE(...) ((void)0)
#define METALIO_EPD_SCAN_FULL 1
#define THINGSOS_PANEL_BYTES 4
typedef struct {uint8_t pixels[4];} thingsos_canvas_t;
enum EpdDrawError {EPD_DRAW_SUCCESS=0,EPD_DRAW_FAILED_ALLOC=0x10,EPD_DRAW_EMPTY_LINE_QUEUE=0x400};
static bool pack_ok=true;
static unsigned redraws;
static enum EpdDrawError redraw_result;
static void metalio_epd_use_scan(int scan){CHECK(scan==METALIO_EPD_SCAN_FULL);}
static bool thingsos_panel_pack(const thingsos_canvas_t* c,uint8_t* fb,unsigned size,bool mono){
    CHECK(size==4 && !mono && pclk==16 && prefill==127);
    if(!pack_ok)return false;
    for(unsigned i=0;i<4;++i)fb[i]=c->pixels[i];
    return true;
}
static enum EpdDrawError panel_redraw_from_white(int temperature){
    CHECK(temperature==25 && pclk==16);++redraws;return redraw_result;
}
#include "../firmware/components/thingsos_board/loading_recovery.inc"

static void probe_after_safe_redraw(unsigned delay_s) {
    CHECK(pclk==16);
    /* A failed safe redraw must never arm the fast clock, even after sleep. */
    clock_note_result(false);now+=3600000000LL;clock_retry_step();CHECK(pclk==16);
    clock_note_result(true);
    now+=(int64_t)delay_s*1000000-1;clock_retry_step();CHECK(pclk==16);
    /* Another successful low-clock page must not postpone recovery forever. */
    clock_note_result(true);++now;clock_retry_step();CHECK(pclk==20 && prefill==127);
    unsigned before=switches;clock_retry_step();CHECK(switches==before);
}
int main(void) {
    clock_retry_step();clock_note_result(true);CHECK(pclk==20 && !switches);
    /* No permanent latch, including many consecutive failures at the cap. */
    const unsigned delay[]={2,10,30,60,60,60,60,60};
    for(unsigned i=0;i<sizeof(delay)/sizeof(delay[0]);++i) {
        clock_underrun();CHECK(pclk==16 && clock_retry_delay_s()==delay[i]);
        probe_after_safe_redraw(delay[i]);
    }
    /* A short-lived successful probe does not discard the backoff. */
    clock_note_result(true);clock_note_result(true);clock_underrun();
    CHECK(clock_retry_delay_s()==60);probe_after_safe_redraw(60);
    /* Three consecutive full-clock successes restore the initial delay. */
    clock_note_result(true);clock_note_result(true);clock_note_result(true);
    CHECK(clock_recovery.failures==0);
    clock_underrun();probe_after_safe_redraw(2);
    /* A non-underrun failure breaks the healthy presentation streak. */
    clock_note_result(true);clock_note_result(true);clock_note_result(false);
    clock_note_result(true);clock_note_result(true);CHECK(clock_recovery.failures==1);
    clock_note_result(true);CHECK(clock_recovery.failures==0);
    /* A later failed 16 MHz presentation invalidates an already armed probe. */
    clock_underrun();clock_note_result(true);clock_note_result(false);
    now+=120000000;clock_retry_step();CHECK(pclk==16);
    probe_after_safe_redraw(2);
    /* Regional recovery replaces stale pixels outside the animation too. */
    thingsos_canvas_t canvas={{1,2,3,4}};uint8_t fb[4]={9,9,9,9};
    redraw_result=EPD_DRAW_SUCCESS;
    CHECK(loading_recover(&canvas,fb,25)==EPD_DRAW_SUCCESS && redraws==1);
    for(unsigned i=0;i<4;++i)CHECK(fb[i]==canvas.pixels[i]);
    redraw_result=EPD_DRAW_EMPTY_LINE_QUEUE;
    CHECK(loading_recover(&canvas,fb,25)==EPD_DRAW_EMPTY_LINE_QUEUE && redraws==2);
    clock_note_result(false);now+=120000000;clock_retry_step();CHECK(pclk==16);
    pack_ok=false;
    CHECK(loading_recover(&canvas,fb,25)==EPD_DRAW_FAILED_ALLOC && redraws==2);
    puts("PASS: clock recovery; full loading target; bounded failed redraw; packing failure skips scan");
    return 0;
}
