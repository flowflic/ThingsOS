/* SPDX-License-Identifier: LGPL-3.0-or-later */
void epd_lcd_check_frame_clock(void) {
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    esp_err_t e=pcnt_unit_get_count(ckv_counter,&ckv_pulses);
    if(!lcd.frame_error && (e!=ESP_OK || ckv_pulses<lcd.display_lines || ckv_pulses>lcd.display_lines+5)) {
        lcd.frame_error=4;
        ESP_LOGE(TAG,"CKV pulse fault count=%d expected=%d..%d read=0x%x",ckv_pulses,
                 lcd.display_lines,lcd.display_lines+5,(unsigned)e);
    }
#endif
}
