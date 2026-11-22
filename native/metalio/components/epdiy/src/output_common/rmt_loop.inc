/* SPDX-License-Identifier: LGPL-3.0-or-later */
void IRAM_ATTR rmt_compat_tx_start(rmt_compat_channel_t channel) {
    rmt_ll_tx_reset_pointer(&RMT, channel);
    rmt_ll_tx_start(&RMT, channel);
}
void IRAM_ATTR rmt_compat_tx_stop(rmt_compat_channel_t channel) {
    rmt_ll_tx_stop(&RMT, channel);
}

void IRAM_ATTR rmt_compat_tx_reset_mem(rmt_compat_channel_t channel) {
    rmt_ll_tx_reset_pointer(&RMT, channel);
}

void IRAM_ATTR rmt_compat_tx_configure_finite_loop(rmt_compat_channel_t channel, uint32_t count) {
    /* A preceding LCD frame can finish before its extra CKV tail pulses.
       Stop the transmitter before touching its loop counter or read pointer. */
    rmt_compat_tx_stop(channel);
#if defined(SOC_RMT_SUPPORT_TX_LOOP_COUNT) && SOC_RMT_SUPPORT_TX_LOOP_COUNT
#if defined(SOC_RMT_SUPPORT_TX_LOOP_AUTO_STOP) && SOC_RMT_SUPPORT_TX_LOOP_AUTO_STOP
    rmt_ll_tx_enable_loop_autostop(&RMT, channel, true);
#endif
    rmt_ll_tx_reset_loop_count(&RMT, channel);
    rmt_ll_tx_enable_loop_count(&RMT, channel, true);
    rmt_ll_tx_set_loop_count(&RMT, channel, count);
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#error "ESP32-S3 LCD output requires RMT TX loop count support"
#else
    (void)channel;
    (void)count;
#endif
}

