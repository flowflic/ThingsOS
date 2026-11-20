/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
static int set_error, get_error, sets, gets, errors;
static int8_t readback, requested;
static const char* esp_err_to_name(int error) { (void)error; return "test error"; }
static int esp_wifi_set_max_tx_power(int8_t power) { ++sets; requested=power; return set_error; }
static int esp_wifi_get_max_tx_power(int8_t* power) { ++gets; *power=readback; return get_error; }
static void log_error(const char* tag, const char* format, ...) { (void)tag; (void)format; ++errors; }
static void log_info(const char* tag, const char* format, ...) { (void)tag; (void)format; }
#define ESP_LOGE(...) log_error(__VA_ARGS__)
#define ESP_LOGI(...) log_info(__VA_ARGS__)
#include "../firmware/components/thingsos_net/wifi_tx_power.inc"
static void reset(int8_t actual) { set_error=get_error=sets=gets=errors=0; readback=actual; requested=0; }
int main(void) {
    /* Accept SDK quantization to 11 dBm, an exact 12 dBm result, and a
       lower country/rate cap. Reapplying after a restart must issue it again. */
    for (int i=0;i<3;++i) {
        reset(i==0?44:i==1?48:20);
        CHECK(wifi_tx_power_limit()==ESP_OK && requested==48 && sets==1 && gets==1 && !errors);
        CHECK(wifi_tx_power_limit()==ESP_OK && requested==48 && sets==2 && gets==2 && !errors);
    }
    reset(80);
    CHECK(wifi_tx_power_limit()==ESP_FAIL && errors==1);
    reset(0);
    CHECK(wifi_tx_power_limit()==ESP_FAIL && errors==1);
    reset(44); set_error=-7;
    CHECK(wifi_tx_power_limit()==-7 && sets==1 && !gets && errors==1);
    reset(44); get_error=-8;
    CHECK(wifi_tx_power_limit()==-8 && sets==1 && gets==1 && errors==1);
    puts("WiFi 12 dBm request, quantization and failure handling passed");
    return 0;
}
