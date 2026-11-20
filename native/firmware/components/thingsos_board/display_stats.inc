/* SPDX-License-Identifier: Apache-2.0 */
/* UI-task owned. A skipped/failed presentation never replaces the last success. */
static uint32_t display_last_ms;
static bool display_last_valid;
static bool display_last_failed;
static void display_record_success(int64_t began) {
    int64_t end=esp_timer_get_time();
    if(end<began)return;
    uint64_t ms=(uint64_t)(end-began)/1000;
    display_last_ms=ms>UINT32_MAX?UINT32_MAX:(uint32_t)ms;
    display_last_valid=true;
}
void thingsos_board_display_stats(thingsos_system_display_t* out) {
    if(!out)return;
    int mhz=metalio_epd_pclk_mhz();
    EpdUnderrunStats faults=epd_underrun_stats();
    *out=(thingsos_system_display_t){.pclk_mhz=mhz>0?(uint32_t)mhz:0,
        .last_refresh_ms=display_last_ms,.refresh_valid=display_last_valid,
        .underrun_count=faults.count,.underrun_last_seconds=faults.last_seconds,
        .underrun_during_clear=faults.during_clear,.last_present_failed=display_last_failed};
}
