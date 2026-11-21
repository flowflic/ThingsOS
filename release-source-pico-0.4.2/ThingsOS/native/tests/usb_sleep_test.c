// SPDX-License-Identifier: Apache-2.0
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"USB sleep line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_SLEEP_WAKEUP_TIMER 1
#define pdMS_TO_TICKS(ms) (ms)
static bool host_connected;
static unsigned timer_calls,sleep_calls,disable_calls,delay_calls;
static int sleep_result;
static bool usb_serial_jtag_is_connected(void){return host_connected;}
static void vTaskDelay(unsigned ticks){CHECK(ticks==200);++delay_calls;}
static int esp_sleep_enable_timer_wakeup(uint64_t us){CHECK(us==200000);++timer_calls;return 0;}
static int esp_light_sleep_start(void){++sleep_calls;return sleep_result;}
static int esp_sleep_disable_wakeup_source(int source){CHECK(source==ESP_SLEEP_WAKEUP_TIMER);++disable_calls;return 0;}
#include "../firmware/components/thingsos_board/sleep_wait.inc"
int main(void) {
    host_connected=true;
    for(unsigned i=0;i<10;++i)thingsos_board_sleep_wait();
    CHECK(delay_calls==10 && !sleep_calls && !timer_calls && !disable_calls);
    host_connected=false;thingsos_board_sleep_wait();
    CHECK(sleep_calls==1 && timer_calls==1 && disable_calls==1 && delay_calls==10);
    host_connected=true;thingsos_board_sleep_wait();
    CHECK(sleep_calls==1 && delay_calls==11); /* No latched USB policy after reconnect. */
    host_connected=false;sleep_result=-1;thingsos_board_sleep_wait();
    CHECK(sleep_calls==2 && timer_calls==2 && disable_calls==2 && delay_calls==12);
    return 0;
}
