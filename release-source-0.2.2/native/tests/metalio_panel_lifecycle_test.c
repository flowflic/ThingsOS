/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"panel lifecycle:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define EPD_XOE 0
#define EPD_MODE 1
#define METALIO_IO_VCOM_CTRL 0
#define METALIO_IO_PWRUP 1
#define METALIO_IO_WAKEUP 2
#define BIT_OF(io) (1u<<(io))
#define VCOM_SETTLE_US 2000
#define TCA_REG_OUT0 2
#define ESP_LOGI(...) ((void)0)
typedef struct {bool ep_output_enable,ep_mode;} epd_ctrl_state_t;
static bool s_ioe_ok=true,s_rails_on;
static uint8_t s_p0,actual_p0;
static unsigned clock_us,last_xoe_low,commits,downs;
static int gpio[2],s_tca,write_error,read_error;
static void gpio_set_level(int pin,int level){gpio[pin]=level;if(pin==EPD_XOE && !level)last_xoe_low=clock_us;}
static int gpio_get_level(int pin){return gpio[pin];}
static void esp_rom_delay_us(unsigned us){clock_us+=us;}
static esp_err_t tca_commit(void){++commits;if(!write_error)actual_p0=s_p0;return write_error;}
static void tps_rails_down(void){
    CHECK(!gpio[EPD_XOE] && !gpio[EPD_MODE] && clock_us-last_xoe_low>=12);
    ++downs;s_p0&=~7u;actual_p0=s_p0;
}
static esp_err_t metalio_i2c_read(int device,uint8_t reg,uint8_t* out,size_t size){
    CHECK(device==s_tca && reg==TCA_REG_OUT0 && size==1);
    if(!read_error)*out=actual_p0;return read_error;
}
#include "../metalio/components/metalio_bsp/metalio_panel_lifecycle.inc"

#define ESP_LOGE(...) ((void)0)
typedef struct {int width,height;} EpdRect;
enum EpdDrawError {EPD_DRAW_SUCCESS=0,EPD_DRAW_FAILED_ALLOC=0x10,EPD_DRAW_EMPTY_LINE_QUEUE=0x400};
typedef struct {uint32_t count;uint64_t last_seconds;bool during_clear;} EpdUnderrunStats;
static int64_t esp_timer_get_time(void){return clock_us;}
#include "../metalio/components/epdiy/src/underrun_stats.inc"
static struct {enum EpdDrawError error;} render_context;
static unsigned clear_phases, fail_phase;
static enum EpdDrawError clear_error;
static void epd_push_pixels(EpdRect area,short time,int color) {
    CHECK(area.width==1216 && area.height==684 && time==15);
    unsigned phase=clear_phases++%26;
    CHECK(color==(phase<10?0:phase<23?1:2));
    render_context.error=clear_phases==fail_phase?clear_error:EPD_DRAW_SUCCESS;
}
#include "../metalio/components/epdiy/src/clear_scan.inc"

int main(void){
    CHECK(epd_underrun_stats().count==0);
    record_scan_error(EPD_DRAW_SUCCESS,false);record_scan_error(EPD_DRAW_FAILED_ALLOC,false);
    CHECK(epd_underrun_stats().count==0);
    clock_us=7000000;record_scan_error(EPD_DRAW_EMPTY_LINE_QUEUE,false);
    CHECK(epd_underrun_stats().count==1 && epd_underrun_stats().last_seconds==7 && !epd_underrun_stats().during_clear);
    clock_us=9000000;record_scan_error(EPD_DRAW_EMPTY_LINE_QUEUE,true);
    CHECK(epd_underrun_stats().count==2 && epd_underrun_stats().last_seconds==9 && epd_underrun_stats().during_clear);
    record_scan_error(EPD_DRAW_SUCCESS,false);CHECK(epd_underrun_stats().count==2);
    underrun_stats.count=UINT32_MAX;record_scan_error(EPD_DRAW_EMPTY_LINE_QUEUE,false);
    CHECK(epd_underrun_stats().count==UINT32_MAX);clock_us=0;
    epd_ctrl_state_t state={0},mask={true,true};
    s_p0=0xa6;actual_p0=s_p0;s_rails_on=true;
    CHECK(panel_bias_ready(&state)==ESP_OK);
    CHECK(commits==1 && clock_us==2000 && gpio[EPD_XOE] && !gpio[EPD_MODE]);
    CHECK(actual_p0==0xa7); /* Other expander outputs are preserved. */
    for(unsigned phase=0;phase<48;++phase){
        state.ep_output_enable=state.ep_mode=true;board_set_ctrl(&state,&mask);
        state.ep_output_enable=state.ep_mode=false;board_set_ctrl(&state,&mask);
        CHECK(actual_p0==0xa7 && commits==1); /* No VCOM toggles per phase. */
    }
    board_poweroff(&state);
    CHECK(downs==1 && !s_rails_on && actual_p0==0xa0);
    CHECK(metalio_panel_idle_verify()==ESP_OK);
    actual_p0|=1;CHECK(metalio_panel_idle_verify()==ESP_ERR_INVALID_STATE);
    actual_p0&=~1u;gpio[EPD_XOE]=1;CHECK(metalio_panel_idle_verify()==ESP_ERR_INVALID_STATE);
    gpio[EPD_XOE]=0;gpio[EPD_MODE]=1;CHECK(metalio_panel_idle_verify()==ESP_ERR_INVALID_STATE);
    gpio[EPD_MODE]=0;read_error=17;CHECK(metalio_panel_idle_verify()==17);read_error=0;
    write_error=23;clock_us=0;CHECK(panel_bias_ready(&state)==23);
    CHECK(!gpio[EPD_XOE] && !gpio[EPD_MODE] && clock_us==0);
    EpdRect panel={1216,684};
    fail_phase=11;clear_error=EPD_DRAW_EMPTY_LINE_QUEUE;
    CHECK(clear_area_cycles(panel,3,15)==EPD_DRAW_EMPTY_LINE_QUEUE && clear_phases==11);
    clear_phases=0;fail_phase=1;clear_error=EPD_DRAW_FAILED_ALLOC;
    CHECK(clear_area_cycles(panel,3,15)==EPD_DRAW_FAILED_ALLOC && clear_phases==1);
    clear_phases=fail_phase=0;
    CHECK(clear_area_cycles(panel,3,15)==EPD_DRAW_SUCCESS && clear_phases==78);
    puts("PASS: bias settles with outputs disabled; 48 phases keep stable VCOM; XOE safety delay precedes rail shutdown; unrelated outputs preserved; real output mismatch and I2C failures detected");
    return 0;
}
