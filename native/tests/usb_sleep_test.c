// SPDX-License-Identifier: Apache-2.0
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"USB sleep line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_SLEEP_WAKEUP_TIMER 1
#define ESP_SLEEP_WAKEUP_GPIO 2
#define GPIO_INTR_LOW_LEVEL 4
#define METALIO_IOE_INT_GPIO 2
#define pdMS_TO_TICKS(ms) (ms)
static bool host_connected,host_lock_busy,host_locked;
static unsigned timer_calls,sleep_calls,disable_calls,delay_calls,gpio_wake_calls,lock_calls;
static int sleep_result;
static bool usb_serial_jtag_is_connected(void){return host_connected;}
static void vTaskDelay(unsigned ticks){CHECK(ticks==200);++delay_calls;}
static bool metalio_i2c_lock(unsigned ms){CHECK(ms==200);++lock_calls;if(host_lock_busy)return false;CHECK(!host_locked);host_locked=true;return true;}
static void metalio_i2c_unlock(void){CHECK(host_locked);host_locked=false;}
static int gpio_wakeup_enable(int gpio,int level){CHECK(gpio==METALIO_IOE_INT_GPIO && level==GPIO_INTR_LOW_LEVEL);++gpio_wake_calls;return 0;}
static int gpio_wakeup_disable(int gpio){CHECK(gpio==METALIO_IOE_INT_GPIO);return 0;}
static int esp_sleep_enable_gpio_wakeup(void){return 0;}
static int esp_sleep_enable_timer_wakeup(uint64_t us){CHECK(us==2000000);++timer_calls;return 0;}
/* Every I2C transaction stays outside the sleep window. */
static int esp_light_sleep_start(void){CHECK(host_locked);++sleep_calls;return sleep_result;}
static int esp_sleep_disable_wakeup_source(int source){CHECK(source==ESP_SLEEP_WAKEUP_TIMER || source==ESP_SLEEP_WAKEUP_GPIO);++disable_calls;return 0;}
#define DIAG_WAKE 5
static int64_t esp_timer_get_time(void){return (int64_t)sleep_calls*2000000;}
static uint32_t esp_sleep_get_wakeup_causes(void){return 1u<<ESP_SLEEP_WAKEUP_TIMER;}
static void thingsos_diag_event(unsigned k,unsigned s,unsigned a,unsigned b,int c,int e){(void)k;(void)s;(void)a;(void)b;(void)c;(void)e;CHECK(!host_locked);}
#include "../firmware/components/thingsos_board/sleep_wait.inc"
int main(void) {
    host_connected=true;
    for(unsigned i=0;i<10;++i)thingsos_board_sleep_wait();
    CHECK(delay_calls==10 && !sleep_calls && !timer_calls && !disable_calls && !lock_calls);
    host_connected=false;thingsos_board_sleep_wait();
    CHECK(sleep_calls==1 && timer_calls==1 && gpio_wake_calls==1 && disable_calls==2 && delay_calls==10 && !host_locked);
    host_connected=true;thingsos_board_sleep_wait();
    CHECK(sleep_calls==1 && delay_calls==11); /* No latched USB policy after reconnect. */
    host_connected=false;sleep_result=-1;thingsos_board_sleep_wait();
    CHECK(sleep_calls==2 && timer_calls==2 && disable_calls==4 && delay_calls==12 && !host_locked);
    /* A busy bus defers sleep instead of cutting a transaction. */
    host_lock_busy=true;sleep_result=0;thingsos_board_sleep_wait();
    CHECK(sleep_calls==2 && delay_calls==13);
    return 0;
}
