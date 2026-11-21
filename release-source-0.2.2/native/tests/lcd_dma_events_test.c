/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"DMA events:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define IRAM_ATTR
#define CONFIG_IDF_TARGET_ESP32S31 1
#define LCD_BB_COUNT 3
#define DMA_DESCRIPTOR_BUFFER_OWNER_DMA 1
#define ESP_CACHE_MSYNC_FLAG_DIR_C2M 1
#define ESP_CACHE_MSYNC_FLAG_UNALIGNED 2
typedef void* gdma_channel_handle_t;
typedef struct {uintptr_t tx_eof_desc_addr;} gdma_event_data_t;
typedef struct {struct {unsigned owner;} dw0;} node_t;
static struct {
    bool frame_finishing,bb_behind_cache;
    unsigned frame_error,bb_eof_count;
    uintptr_t fault_eof,fault_expected;
    node_t dma_nodes[LCD_BB_COUNT];
    uint8_t bounce_buffer[LCD_BB_COUNT][8];
} lcd;
#define LCD_DMA_NODE(idx) (&lcd.dma_nodes[idx])
static unsigned finishes,refills,syncs,last_buffer;
static bool finish_during_refill;
static void lcd_finish_frame_from_isr(void) {lcd.frame_finishing=true;++finishes;}
static bool fill_bounce_buffer(uint8_t* buffer) {
    ++refills;last_buffer=(unsigned)((buffer-lcd.bounce_buffer[0])/8);
    if(finish_during_refill)lcd_finish_frame_from_isr();
    return true;
}
static void esp_cache_msync(void* p,size_t n,int flags) {
    CHECK(p>=(void*)LCD_DMA_NODE(0) && p<=(void*)LCD_DMA_NODE(LCD_BB_COUNT-1) && n==sizeof(node_t) && flags==3);++syncs;
}
static uint32_t host_cycles;
static uint32_t esp_cpu_get_cycle_count(void){return host_cycles+=100;}
#include "../metalio/components/epdiy/src/output_lcd/lcd_dma_events.inc"
static void reset(void) {
    memset(&lcd,0,sizeof(lcd));lcd.bb_behind_cache=true;
    finishes=refills=syncs=0;finish_during_refill=false;
}
static bool eof(unsigned index) {
    gdma_event_data_t e={.tx_eof_desc_addr=(uintptr_t)LCD_DMA_NODE(index)};
    return lcd_rgb_panel_eof_handler(NULL,&e,NULL);
}
int main(void) {
    reset();
    for(unsigned i=0;i<100;++i) {
        CHECK(eof(i%3) && last_buffer==i%3 && refills==i+1 && syncs==i+1);
        CHECK(!lcd.frame_error && !finishes && LCD_DMA_NODE(i%3)->dw0.owner==1);
    }
    CHECK(!eof_catchups);
    /* A late interrupt that reports the next descriptor too: both completed
       buffers are refilled in ring order; the DMA is still on the third. */
    reset();eof_catchups=0;CHECK(eof(1) && refills==2 && syncs==2 && last_buffer==1 && lcd.bb_eof_count==2);
    CHECK(!lcd.frame_error && !finishes && eof_catchups==1);
    CHECK(eof(2) && refills==3 && last_buffer==2);CHECK(eof(1) && refills==5 && last_buffer==1 && eof_catchups==2);
    /* Beyond the window (ring lapped onto a buffer not yet refilled) stops. */
    reset();CHECK(!eof(2) && lcd.frame_error==1 && finishes==1 && !refills && !syncs);
    CHECK(lcd.fault_eof==(uintptr_t)LCD_DMA_NODE(2) && lcd.fault_expected==(uintptr_t)LCD_DMA_NODE(0));
    CHECK(!eof(0));lcd_rgb_panel_dma_error(NULL,NULL,NULL);CHECK(finishes==1);
    /* Duplicate and invalid EOFs are faults too. */
    reset();CHECK(eof(0));CHECK(!eof(0) && lcd.frame_error==1 && refills==1 && finishes==1);
    CHECK(lcd.fault_expected==(uintptr_t)LCD_DMA_NODE(1));
    reset();CHECK(!lcd_rgb_panel_eof_handler(NULL,NULL,NULL) && lcd.frame_error==1 && !refills);
    reset();gdma_event_data_t bad={.tx_eof_desc_addr=1};
    CHECK(!lcd_rgb_panel_eof_handler(NULL,&bad,NULL) && lcd.frame_error==1 && !refills);
    reset();CHECK(!lcd_rgb_panel_dma_error(NULL,NULL,NULL) && lcd.frame_error==2 && finishes==1);
    CHECK(lcd.fault_eof==0 && lcd.fault_expected==(uintptr_t)LCD_DMA_NODE(0));
    CHECK(!eof(0) && !refills);
    /* Normal final refill stops the frame without publishing another owner,
       also in the middle of a catch-up. */
    reset();finish_during_refill=true;
    CHECK(eof(0) && finishes==1 && !lcd.frame_error && !syncs && LCD_DMA_NODE(0)->dw0.owner==0);
    reset();finish_during_refill=true;
    CHECK(eof(1) && refills==1 && finishes==1 && !lcd.frame_error && !syncs && LCD_DMA_NODE(1)->dw0.owner==0);
    puts("PASS: 3-buffer DMA ring; coalesced EOF catch-up; lapped/duplicate/invalid EOF; descriptor fault; finish once");
    return 0;
}
