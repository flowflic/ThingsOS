/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "../firmware/components/thingsos_weidu/weidu_device.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
typedef int esp_err_t;
typedef enum { WIFI_PS_NONE, WIFI_PS_MIN_MODEM, WIFI_PS_MAX_MODEM } wifi_ps_type_t;
#define ESP_OK 0
static wifi_ps_type_t mode;
static int get_calls, set_calls, get_failure_call, set_error, warnings, delays;
static bool ignore_set;
static char last_exchange[240];
static esp_err_t esp_wifi_get_ps(wifi_ps_type_t* out) {
    if (++get_calls == get_failure_call) return -1;
    *out = mode;
    return ESP_OK;
}
static esp_err_t esp_wifi_set_ps(wifi_ps_type_t value) {
    ++set_calls;
    if (set_error) return set_error;
    if (!ignore_set) mode = value;
    return ESP_OK;
}
static esp_err_t esp_wifi_get_max_tx_power(int8_t* power) { *power = 80; return ESP_OK; }
static void log_info(const char* tag, const char* format, ...) {
    (void)tag;
    if (!strstr(format,"tx_qdbm=")) return;
    va_list args;
    va_start(args,format);
    vsnprintf(last_exchange,sizeof(last_exchange),format,args);
    va_end(args);
}
#define ESP_LOGI(...) log_info(__VA_ARGS__)
#define ESP_LOGW(...) (++warnings)
#define pdMS_TO_TICKS(ms) (ms)
static void vTaskDelay(unsigned ticks) { CHECK(ticks==100); ++delays; }
#include "../firmware/components/thingsos_weidu/radio_policy.inc"
static void reset(wifi_ps_type_t initial) {
    mode=initial;
    get_calls=set_calls=get_failure_call=set_error=warnings=delays=0;
    ignore_set=false;
    last_exchange[0]=0;
}
int main(void) {
    /* Login, first shelf, covers, chapter download, notes and MP data all use
       the same worker policy; local-only restore and logout must not wake it. */
    for (int action=WD_DEVICE_LOGIN; action<=WD_DEVICE_NOTE_WRITE; ++action) {
        CHECK(radio_needs_awake((thingsos_weidu_action_t)action,true)==(action!=WD_DEVICE_LOGOUT));
        CHECK(radio_needs_awake((thingsos_weidu_action_t)action,false)==
              (action!=WD_DEVICE_LOGOUT && action!=WD_DEVICE_RESTORE));
    }
    for (int previous=WIFI_PS_MIN_MODEM; previous<=WIFI_PS_MAX_MODEM; ++previous) {
        reset((wifi_ps_type_t)previous);
        weidu_radio_t state=radio_awake();
        CHECK(state.changed && state.previous==previous && mode==WIFI_PS_NONE);
        CHECK(delays==1 && !warnings && strstr(last_exchange,"actual=0 readback=0 tx_qdbm=80"));
        /* The same cleanup runs for success, failure and cancellation. */
        radio_restore(&state);
        CHECK(mode==previous && !state.changed && set_calls==2 && !warnings);
        radio_restore(&state);
        CHECK(set_calls==2);
    }
    reset(WIFI_PS_NONE);
    weidu_radio_t state=radio_awake();
    CHECK(!state.changed && !delays);
    radio_restore(&state);
    CHECK(mode==WIFI_PS_NONE && set_calls==1);
    reset(WIFI_PS_MIN_MODEM); get_failure_call=1;
    state=radio_awake(); radio_restore(&state);
    CHECK(mode==WIFI_PS_MIN_MODEM && !set_calls && warnings==1);
    reset(WIFI_PS_MIN_MODEM); set_error=-2;
    state=radio_awake(); radio_restore(&state);
    CHECK(mode==WIFI_PS_MIN_MODEM && !state.changed && set_calls==1 && warnings==1);
    reset(WIFI_PS_MIN_MODEM); get_failure_call=2;
    state=radio_awake();
    CHECK(state.changed && warnings==1);
    radio_restore(&state);
    CHECK(mode==WIFI_PS_MIN_MODEM);
    reset(WIFI_PS_MIN_MODEM); ignore_set=true;
    state=radio_awake();
    CHECK(mode==WIFI_PS_MIN_MODEM && warnings==1);
    radio_restore(&state);
    reset(WIFI_PS_MIN_MODEM);
    state=radio_awake(); set_error=-3;
    radio_restore(&state);
    CHECK(mode==WIFI_PS_NONE && warnings==1);
    puts("WeRead radio policy and restoration passed");
    return 0;
}
