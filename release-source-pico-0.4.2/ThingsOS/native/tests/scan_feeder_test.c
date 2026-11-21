/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <windows.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"feeder:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define THINGSOS_SCAN_HOST
#ifdef _MSC_VER
#define __attribute__(x)
#endif
#define IRAM_ATTR
#define NUM_RENDER_THREADS 2
#define EPD_DRAW_EMPTY_LINE_QUEUE 0x400
#define atomic_fetch_add(p,v) InterlockedExchangeAdd((volatile LONG*)(p),(v))
#define Cache_Start_DCache_Preload(a,b,c) CHECK(0) /* Never reprogram the SDK's global cache engine. */
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int size,element_size;volatile LONG current,last;uint8_t data[64][304];} LineQueue_t;
typedef struct {
    volatile unsigned error;int display_width,display_height,lines_total;volatile LONG lines_prepared;
    int lines_consumed;volatile uint8_t line_threads[684];LineQueue_t line_queues[2];
    uint8_t* feed_line_buffers[2];const uint8_t* conversion_lut;const bool* drawn_lines;
    EpdRect area;void (*lut_lookup_func)(const uint32_t*,uint8_t*,const uint8_t*,int);
} RenderContext_t;
static RenderContext_t ctx;
static uint8_t source[684][1216],expected[684][304],input[2][1216],lut[16];
static bool drawn[684],dma,failed;
static volatile LONG starts;static unsigned ready_at_start,checks;
static HANDLE scan_started;
static void consume(void) {
    if(!dma || ctx.lines_consumed==ctx.lines_total)return;
    ULONGLONG deadline=GetTickCount64()+2000;
    unsigned lane;
    for(;;) {
        lane=ctx.line_threads[ctx.lines_consumed];
        if(lane<2) {
            LineQueue_t* q=&ctx.line_queues[lane];
            if(InterlockedCompareExchange(&q->current,0,0)!=q->last)break;
        }
        CHECK(GetTickCount64()<deadline);SwitchToThread();
    }
    LineQueue_t* q=&ctx.line_queues[lane];
    CHECK(!memcmp(q->data[q->last],expected[ctx.lines_consumed],304));
    InterlockedExchange(&q->last,(q->last+1)%q->size);++ctx.lines_consumed;++checks;
}
static uint8_t* lq_current(LineQueue_t* q) {
    LONG current=InterlockedCompareExchange(&q->current,0,0),last=InterlockedCompareExchange(&q->last,0,0);
    if((current+1)%q->size==last){SwitchToThread();return NULL;}
    return q->data[current];
}
static void lq_commit(LineQueue_t* q){InterlockedExchange(&q->current,(q->current+1)%q->size);}
static void lq_reset(LineQueue_t* q){InterlockedExchange(&q->current,0);InterlockedExchange(&q->last,0);}
static void epd_lcd_line_source_cb(void* fn,void* arg){(void)fn;(void)arg;}
static bool retrieve_line_isr(RenderContext_t* c,uint8_t* p){(void)c;(void)p;return false;}
typedef void (*line_cb_func_t)(void);
static void epd_lcd_start_frame(void) {
    CHECK(InterlockedIncrement(&starts)==1);dma=true;
    for(unsigned i=0;i<2;++i)ready_at_start+=(ctx.line_queues[i].current-ctx.line_queues[i].last+64)%64;
    if(failed)ctx.error=EPD_DRAW_EMPTY_LINE_QUEUE;
    SetEvent(scan_started);
}
static void get_buffer_params(RenderContext_t* c,int* bytes,const uint8_t** begin,int* min_y,int* max_y,int* packing) {
    *bytes=1216;*min_y=c->area.y;*max_y=c->area.y+c->area.height;
    *begin=source[*min_y];*packing=1;
}
static void lookup(const uint32_t* p,uint8_t* out,const uint8_t* table,int width) {
    const uint8_t* in=(const uint8_t*)p;
    for(int x=0;x<width/4;++x)out[x]=in[x*4]^in[x*4+3]^table[x%16];
}
static void epd_apply_line_mask_VE(uint8_t* out,const uint8_t* mask,int bytes) {
    (void)mask;for(int x=0;x<bytes;++x)out[x]&=0x7f;
}
/* The adapter reads this mask member only for real pixel rows. */
#define line_mask conversion_lut
#include "../firmware/components/thingsos_board/scan_adapter.c"
static DWORD WINAPI producer(void* arg) {
    unsigned lane=(unsigned)(uintptr_t)arg;
    if(!lane)Sleep(2); /* CPU0 can start later; output must not depend on its schedule. */
    lcd_calculate_frame(&ctx,(int)lane);return 0;
}
static void run(int top,int height,bool sparse,bool error) {
    memset(&ctx,0,sizeof(ctx));ctx.display_width=1216;ctx.display_height=ctx.lines_total=684;
    ctx.area=(EpdRect){0,top,1216,height};ctx.conversion_lut=lut;ctx.lut_lookup_func=lookup;
    ctx.feed_line_buffers[0]=input[0];ctx.feed_line_buffers[1]=input[1];
    ctx.drawn_lines=sparse?drawn:NULL;
    memset(expected,0,sizeof(expected));
    for(int y=top;y<top+height;++y)if(!sparse || drawn[y-top]) {
        lookup((const uint32_t*)source[y],expected[y],lut,1216);
        epd_apply_line_mask_VE(expected[y],lut,304);
    }
    for(unsigned i=0;i<2;++i){ctx.line_queues[i].size=64;ctx.line_queues[i].element_size=304;}
    dma=false;failed=error;starts=ready_at_start=checks=0;
    memset((void*)ctx.line_threads,255,sizeof(ctx.line_threads));
    scan_started=CreateEvent(NULL,TRUE,FALSE,NULL);CHECK(scan_started);
    HANDLE workers[2]={CreateThread(NULL,0,producer,(void*)0,0,NULL),CreateThread(NULL,0,producer,(void*)1,0,NULL)};
    CHECK(workers[0] && workers[1]);
    CHECK(WaitForSingleObject(scan_started,3000)==WAIT_OBJECT_0);
    CHECK(starts==1 && ready_at_start<=126);
    if(!error) {
        while(ctx.lines_consumed<ctx.lines_total)consume();
        CHECK(checks==684 && !ctx.error);
        for(int y=0;y<684;++y)CHECK(ctx.line_threads[y]<2);
    } else CHECK(ctx.error==EPD_DRAW_EMPTY_LINE_QUEUE);
    CHECK(WaitForMultipleObjects(2,workers,TRUE,3000)==WAIT_OBJECT_0);
    for(unsigned i=0;i<2;++i)CloseHandle(workers[i]);CloseHandle(scan_started);
    CHECK(starts==1);
}
int main(void) {
    for(int y=0;y<684;++y)for(int x=0;x<1216;++x)source[y][x]=(uint8_t)(y*17+x);
    for(int i=0;i<16;++i)lut[i]=(uint8_t)(i*13);
    for(int i=0;i<684;++i)drawn[i]=i%3!=0;
    for(int top=0;top<=600;top+=200)for(int height=1;height<=684-top;height+=137)
        for(unsigned sparse=0;sparse<2;++sparse)run(top,height,sparse,false);
    run(0,684,false,true);run(80,300,true,true);
    puts("PASS: delayed dual producers, bounded prefill, exact full/cropped/sparse output, single start, underrun exit; no SDK cache override");
    return 0;
}
