/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Record each failed synchronous scan once, after producers/ISR have stopped.
 * The same failure's upper-layer retries must not increment this counter. */
static EpdUnderrunStats underrun_stats;
static void record_scan_error(enum EpdDrawError error,bool clear) {
    if(!(error & EPD_DRAW_EMPTY_LINE_QUEUE))return;
    if(underrun_stats.count<UINT32_MAX)++underrun_stats.count;
    underrun_stats.last_seconds=(uint64_t)esp_timer_get_time()/1000000;
    underrun_stats.during_clear=clear;
}
EpdUnderrunStats epd_underrun_stats(void) {return underrun_stats;}
