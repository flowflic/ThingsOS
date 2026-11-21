/* SPDX-License-Identifier: Apache-2.0 */
/* A failed regional scan makes the entire optical state uncertain. Rebuild
 * the complete target, then make exactly one synchronous safe-clock attempt.
 * The caller retains recovery_pending on failure; no cache state is touched. */
static enum EpdDrawError loading_recover(const thingsos_canvas_t* canvas,uint8_t* fb,int temperature) {
    clock_underrun();
    metalio_epd_use_scan(METALIO_EPD_SCAN_FULL);
    epd_lcd_set_prefill_lines(127);
    if(!thingsos_panel_pack(canvas,fb,THINGSOS_PANEL_BYTES,false)) {
        ESP_LOGE("ThingsOS_board","Loading recovery: full target packing failed");
        return EPD_DRAW_FAILED_ALLOC;
    }
    enum EpdDrawError result=panel_redraw_from_white(temperature);
    ESP_LOGW("ThingsOS_board","Loading full recovery result=0x%x at %d MHz",(unsigned)result,metalio_epd_pclk_mhz());
    return result;
}
