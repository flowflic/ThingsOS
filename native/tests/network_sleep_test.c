// SPDX-License-Identifier: Apache-2.0
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"WiFi wake line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_WIFI_NOT_STARTED 1
#define WIFI_MODE_APSTA 3
#define portMAX_DELAY 1000
#define portENTER_CRITICAL(guard) ((void)(guard))
#define portEXIT_CRITICAL(guard) ((void)(guard))
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#include "../firmware/components/thingsos_net/wifi_join.inc"
static bool wifi_suspended,network_ready,radio_up,radio_allowed,user_disconnected,reconnect_enabled,connected;
static bool rescue_mode,scan_active,scan_ready;
static unsigned starts,stops,connects,ap_modes;
static int64_t clock_us;
static wifi_join_t station_join;
static int wifi_control=1,state_guard,scan_status;
static int start_error,stop_error;
static char saved_ssid[33],wifi_failure[96];
static const char* esp_err_to_name(int error){(void)error;return "test error";}
static int xSemaphoreTake(int lock,int ticks){CHECK(lock==wifi_control && ticks==portMAX_DELAY);return 1;}
static void xSemaphoreGive(int lock){CHECK(lock==wifi_control);}
static bool wifi_station_has_address(void){return connected;}
static int esp_wifi_stop(void){++stops;return stop_error;}
static int esp_wifi_set_mode(int mode){CHECK(mode==WIFI_MODE_APSTA);++ap_modes;return ESP_OK;}
static int wifi_radio_start(void){
    CHECK(radio_allowed && !wifi_suspended && !radio_up);++starts;
    if(start_error)return start_error;
    radio_up=true;return ESP_OK;
}
static void wifi_station_cancel(void){station_join=(wifi_join_t){0};}
static void wifi_station_request(void){
    CHECK(radio_up && !wifi_suspended && reconnect_enabled && !user_disconnected);
    wifi_join_begin(&station_join,clock_us);
    wifi_failure[0]=0;
    ++connects;
}
#include "../firmware/components/thingsos_net/network_sleep.inc"
static void reset(void){
    wifi_suspended=false;network_ready=radio_up=radio_allowed=reconnect_enabled=true;
    rescue_mode=scan_active=scan_ready=user_disconnected=connected=false;
    clock_us=10000;
    station_join=(wifi_join_t){.deadline=20000,.retry_at=9999,.attempts=4,.retry=true};
    starts=stops=connects=ap_modes=0;start_error=stop_error=scan_status=0;
    strcpy(saved_ssid,"saved network");strcpy(wifi_failure,"old timeout");
}
static void join_timing(void) {
    wifi_join_t join={0};
    /* Starting/scanning the radio spends no connection time. A new request
       made after a long password entry gets its own complete window. */
    int64_t now=60000000;
    CHECK(wifi_join_step(&join,now,true)==WIFI_JOIN_WAIT && !join.deadline);
    wifi_join_begin(&join,now);
    CHECK(wifi_join_step(&join,now,true)==WIFI_JOIN_WAIT && !join.attempts);
    CHECK(wifi_join_step(&join,now,false)==WIFI_JOIN_TRY && join.attempts==1);
    CHECK(!wifi_join_got_ip(&join));
    wifi_join_disconnected(&join,now+100000,202);
    CHECK(wifi_join_step(&join,now+100000,false)==WIFI_JOIN_WAIT);
    CHECK(wifi_join_step(&join,now+WIFI_TRY_INTERVAL_US,false)==WIFI_JOIN_TRY && join.attempts==2);
    join.associated=true;
    CHECK(wifi_join_step(&join,now+7000000,false)==WIFI_JOIN_WAIT);
    CHECK(wifi_join_got_ip(&join) && join.has_ip && !join.deadline);
    wifi_join_disconnected(&join,now+8000000,200);
    CHECK(!join.has_ip && join.deadline && join.retry && !join.attempts);
    now=join.deadline;
    CHECK(wifi_join_step(&join,now,false)==WIFI_JOIN_END && !join.deadline);
    CHECK(wifi_join_step(&join,now+1,false)==WIFI_JOIN_WAIT && !wifi_join_got_ip(&join));
    wifi_join_begin(&join,now);
    for(unsigned attempt=0;attempt<WIFI_TRY_LIMIT;++attempt) {
        CHECK(wifi_join_step(&join,now,false)==WIFI_JOIN_TRY);
        wifi_join_disconnected(&join,now,202);
        now+=WIFI_TRY_INTERVAL_US;
    }
    CHECK(wifi_join_step(&join,now,false)==WIFI_JOIN_END && join.attempts==WIFI_TRY_LIMIT);
}
int main(void){
    join_timing();
    /* Resume after an hour: the pre-sleep failure latch and deadline cannot
       suppress the new connection window. Repeat to catch one-shot recovery. */
    reset();
    for(unsigned i=0;i<3;++i){
        thingsos_network_sleep();CHECK(wifi_suspended && !station_join.deadline && !station_join.retry_at);
        clock_us+=3600000000LL;
        CHECK(thingsos_network_wake()==ESP_OK && !wifi_suspended && radio_up && !wifi_failure[0]);
        CHECK(starts==i+1 && connects==i+1 && !stops);
        CHECK(station_join.deadline==clock_us+WIFI_CONNECT_WINDOW_US);
    }
    reset();radio_allowed=false;thingsos_network_sleep();CHECK(thingsos_network_wake()==ESP_OK);
    CHECK(!starts && !connects && stops==1 && !radio_up && !radio_allowed);
    reset();user_disconnected=true;thingsos_network_sleep();CHECK(thingsos_network_wake()==ESP_OK);
    CHECK(!starts && !connects && stops==1 && user_disconnected && reconnect_enabled);
    reset();saved_ssid[0]=0;thingsos_network_sleep();CHECK(thingsos_network_wake()==ESP_OK);
    CHECK(!starts && !connects && stops==1);
    reset();reconnect_enabled=false;thingsos_network_sleep();CHECK(thingsos_network_wake()==ESP_OK);
    CHECK(!starts && !connects && stops==1 && !reconnect_enabled);
    reset();connected=true;thingsos_network_sleep();CHECK(thingsos_network_wake()==ESP_OK);
    CHECK(radio_up && starts==1 && !stops && !connects && station_join.has_ip && !station_join.deadline);
    reset();start_error=-7;thingsos_network_sleep();CHECK(thingsos_network_wake()==-7);
    CHECK(!radio_up && starts==1 && !connects && wifi_failure[0]);
    start_error=0;CHECK(thingsos_network_wake()==ESP_OK && connects==1);
    reset();radio_allowed=false;stop_error=ESP_ERR_WIFI_NOT_STARTED;
    thingsos_network_sleep();CHECK(thingsos_network_wake()==ESP_OK && !radio_up && !connects);
    reset();radio_allowed=false;stop_error=-9;
    thingsos_network_sleep();CHECK(thingsos_network_wake()==-9 && !connects);
    reset();network_ready=false;thingsos_network_sleep();CHECK(thingsos_network_wake()==ESP_OK);
    CHECK(!starts && !stops && !connects && !wifi_suspended);
    reset();rescue_mode=scan_active=true;saved_ssid[0]=0;
    thingsos_network_sleep();CHECK(scan_ready && scan_status==ESP_FAIL);
    CHECK(thingsos_network_wake()==ESP_OK && ap_modes==1 && starts==1 && !stops && !connects);
    puts("PASS: fresh connection deadline, delayed retries, DHCP, wake policy and rescue AP");
    return 0;
}
