/* SPDX-License-Identifier: Apache-2.0 */
/* Main-task ownership. Change PCLK only between complete synchronous scans.
 * A successful safe redraw is required before probing the validated clock.
 * Repeated failures back off, but never permanently latch the low clock.
 * User policy (2026-11-20): the underrun fallback is 16 MHz, not the BSP's
 * 10 MHz floor. The BSP solver keeps the same spec blanking/CKV limits there
 * (line 21 us, frame ~14.7 ms >= the 12 ms phase floor). */
#define THINGSOS_PANEL_SAFE_PCLK_MHZ 16
#if THINGSOS_PANEL_SAFE_PCLK_MHZ<METALIO_EPD_PCLK_MIN_MHZ || THINGSOS_PANEL_SAFE_PCLK_MHZ>=METALIO_EPD_PCLK_MAX_MHZ
#error "safe panel clock must lie inside the BSP range, below the probe clock"
#endif
static struct {
    unsigned failures, healthy_frames;
    bool pending, safe_ready;
    int64_t retry_at;
} clock_recovery;

static unsigned clock_retry_delay_s(void) {
    static const unsigned delays[]={2,10,30,60};
    unsigned step=clock_recovery.failures;
    return delays[step?step-1:0];
}

static void clock_underrun(void) {
    if(clock_recovery.failures<4)++clock_recovery.failures;
    clock_recovery.healthy_frames=0;
    clock_recovery.pending=true;
    clock_recovery.safe_ready=false;
    metalio_epd_set_pclk(THINGSOS_PANEL_SAFE_PCLK_MHZ);
    ESP_LOGW("ThingsOS_board","Panel underrun: %d MHz; retry %d MHz after successful redraw + %u s",
             metalio_epd_pclk_mhz(),METALIO_EPD_PCLK_MAX_MHZ,clock_retry_delay_s());
}

static void clock_note_result(bool success) {
    if(!success) {
        clock_recovery.healthy_frames=0;
        if(clock_recovery.pending)clock_recovery.safe_ready=false;
        return;
    }
    if(metalio_epd_pclk_mhz()==METALIO_EPD_PCLK_MAX_MHZ) {
        if(clock_recovery.failures && ++clock_recovery.healthy_frames>=3) {
            clock_recovery.failures=0;
            clock_recovery.healthy_frames=0;
            ESP_LOGI("ThingsOS_board","Panel 20 MHz stable for 3 presentations; retry backoff reset");
        }
    } else if(clock_recovery.pending && !clock_recovery.safe_ready) {
        clock_recovery.retry_at=esp_timer_get_time()+(int64_t)clock_retry_delay_s()*1000000;
        clock_recovery.safe_ready=true;
    }
}

static void clock_retry_step(void) {
    if(!clock_recovery.pending || !clock_recovery.safe_ready ||
       esp_timer_get_time()<clock_recovery.retry_at)return;
    clock_recovery.pending=false;
    clock_recovery.safe_ready=false;
    metalio_epd_set_pclk(METALIO_EPD_PCLK_MAX_MHZ);
    epd_lcd_set_prefill_lines(127);
    ESP_LOGI("ThingsOS_board","Panel clock back to %d MHz after safe-clock recovery (backoff=%u s)",
             metalio_epd_pclk_mhz(),clock_retry_delay_s());
}
