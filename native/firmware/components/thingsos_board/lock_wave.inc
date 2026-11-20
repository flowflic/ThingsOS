/* SPDX-License-Identifier: Apache-2.0 */
static enum EpdDrawError panel_redraw_from_white(int temperature) {
    enum EpdDrawError e=epd_clear_checked();
    if(e!=EPD_DRAW_SUCCESS)return e;
    return epd_hl_update_screen_from_white(&hl,MODE_GC16,temperature);
}
/* Retained wallpaper transitions need the complete vendor GC16 sequence,
   including equal-white/equal-gray conditioning. Lock and wake share this
   cleanup, independently of the reader's low-flash waveform and UI switch. */
static enum EpdDrawError lock_wave_turn(int temperature,bool forward,bool* ripple_applied){
    *ripple_applied=false;
    bool attempted=false;
    enum EpdDrawError e=stagger_turn(temperature,forward,true,&attempted);
    if(!attempted){
        ESP_LOGW("ThingsOS_wave","Lock/wake full ripple unavailable; clear then GC16 redraw");
        return panel_redraw_from_white(temperature);
    }
    if(e!=EPD_DRAW_SUCCESS){
        /* No partial animation may become the retained sleep image. */
        if(e&EPD_DRAW_EMPTY_LINE_QUEUE)clock_underrun();
        metalio_epd_use_scan(METALIO_EPD_SCAN_FULL);epd_lcd_set_prefill_lines(127);
        e=panel_redraw_from_white(temperature);
    } else *ripple_applied=true;
    return e;
}
