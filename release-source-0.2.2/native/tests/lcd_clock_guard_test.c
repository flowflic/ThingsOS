/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"CKV:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define IRAM_ATTR
#define SOC_RMT_SUPPORT_TX_LOOP_COUNT 1
#define SOC_RMT_SUPPORT_TX_LOOP_AUTO_STOP 1
#define CONFIG_IDF_TARGET_ESP32S31 1
typedef int rmt_compat_channel_t;
static int RMT;
static bool active,autostop,loop_enabled;
static unsigned stops,starts,loop_count;
static void rmt_ll_tx_stop(int* r,int ch){CHECK(r==&RMT && ch==1);active=false;++stops;}
static void rmt_ll_tx_reset_pointer(int* r,int ch){CHECK(r==&RMT && ch==1 && !active);}
static void rmt_ll_tx_start(int* r,int ch){CHECK(r==&RMT && ch==1 && !active && autostop && loop_enabled);active=true;++starts;}
static void rmt_ll_tx_enable_loop_autostop(int* r,int ch,bool on){CHECK(!active);autostop=on;}
static void rmt_ll_tx_reset_loop_count(int* r,int ch){CHECK(!active);loop_count=0;}
static void rmt_ll_tx_enable_loop_count(int* r,int ch,bool on){CHECK(!active);loop_enabled=on;}
static void rmt_ll_tx_set_loop_count(int* r,int ch,unsigned n){CHECK(!active);loop_count=n;}
void rmt_compat_tx_stop(rmt_compat_channel_t);
#include "../metalio/components/epdiy/src/output_common/rmt_loop.inc"

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_LOGE(...) ((void)0)
static int ckv_counter,ckv_pulses,observed,read_error;
static struct {unsigned frame_error;int display_lines;} lcd;
static int pcnt_unit_get_count(int unit,int* out){*out=observed;return read_error;}
#include "../metalio/components/epdiy/src/output_lcd/lcd_clock_check.inc"
int main(void) {
    /* Simulate the previous scan's still-running five-pulse tail. Reprogramming
       its counter/read pointer while active is invalid; exercise the real wrapper. */
    active=true;rmt_compat_tx_configure_finite_loop(1,693);
    CHECK(!active && stops==1 && loop_count==693);
    rmt_compat_tx_reset_mem(1);rmt_compat_tx_start(1);CHECK(active && starts==1);
    rmt_compat_tx_configure_finite_loop(1,693);
    CHECK(!active && stops==2 && loop_count==693);
    rmt_compat_tx_start(1);rmt_compat_tx_stop(1);CHECK(!active && starts==2);
    lcd.display_lines=688;
    for(observed=688;observed<=693;++observed){lcd.frame_error=0;epd_lcd_check_frame_clock();CHECK(!lcd.frame_error && ckv_pulses==observed);}
    int bad[]={0,1,687,694,32767};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i){observed=bad[i];lcd.frame_error=0;epd_lcd_check_frame_clock();CHECK(lcd.frame_error==4);}
    observed=690;read_error=1;lcd.frame_error=0;epd_lcd_check_frame_clock();CHECK(lcd.frame_error==4);
    lcd.frame_error=2;observed=0;epd_lcd_check_frame_clock();CHECK(lcd.frame_error==2);
    puts("PASS: active CKV tail stopped before rearm; pulse loss/excess/read failure detected; prior DMA fault preserved");
    return 0;
}
