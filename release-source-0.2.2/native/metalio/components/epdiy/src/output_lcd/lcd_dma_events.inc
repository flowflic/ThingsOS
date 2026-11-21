/* SPDX-License-Identifier: LGPL-3.0-or-later
 * A delayed/coalesced EOF must never refill the buffer still being transmitted.
 * Keep the failure latched until the next frame; the renderer converts it to
 * its existing underrun error, so the board can repair the optical baseline. */
/* EOF interrupt spacing (CPU0 cycles). A normal gap is one bounce buffer
 * (8 lines); anything beyond is interrupt-entry latency. Reset per frame so the
 * gap across frames is not counted; the task takes the maximum per refresh. */
static uint32_t eof_prev_cycles, eof_gap_max_cycles;
/* Longest handler run (cycles) and coalesced EOFs caught up, per refresh. */
static uint32_t eof_isr_max_cycles, eof_catchups;
#ifndef LCD_BB_COUNT
#define LCD_BB_COUNT 2
#endif

static IRAM_ATTR bool lcd_rgb_panel_dma_error(
    gdma_channel_handle_t dma_chan, gdma_event_data_t* event_data, void* user_data
) {
    (void)dma_chan;
    (void)event_data;
    (void)user_data;
    if (!lcd.frame_finishing) {
        lcd.frame_error = 2;
        lcd.fault_eof = 0;
        lcd.fault_expected = (uintptr_t)LCD_DMA_NODE(lcd.bb_eof_count % LCD_BB_COUNT);
        lcd_finish_frame_from_isr();
    }
    return false;
}

static IRAM_ATTR bool lcd_rgb_panel_eof_handler(
    gdma_channel_handle_t dma_chan, gdma_event_data_t* event_data, void* user_data
) {
    (void)dma_chan;
    (void)user_data;
    const uint32_t now = esp_cpu_get_cycle_count();
    if (eof_prev_cycles && now - eof_prev_cycles > eof_gap_max_cycles)
        eof_gap_max_cycles = now - eof_prev_cycles;
    eof_prev_cycles = now;
    if (lcd.frame_finishing) return false;
    /* With LCD_BB_COUNT buffers in the ring, up to LCD_BB_COUNT-1 of them may
       have completed when one interrupt arrives late (the EOF status
       coalesces). Refill each in ring order: the DMA is still on a buffer that
       was refilled earlier. The completed descriptor must be one of those;
       anything else (including the previous one again, or the ring lapped)
       means a buffer may already have gone out stale, so the scan stops. */
    unsigned completed = 1;
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    /* A callback count is not proof of the actual completed descriptor. */
    completed = 0;
    if (event_data) {
        for (unsigned j = 0; j + 1 < LCD_BB_COUNT; j++) {
            if (event_data->tx_eof_desc_addr
                == (uintptr_t)LCD_DMA_NODE((lcd.bb_eof_count + j) % LCD_BB_COUNT)) {
                completed = j + 1;
                break;
            }
        }
    }
    if (!completed) {
        lcd.frame_error = 1;
        lcd.fault_eof = event_data ? event_data->tx_eof_desc_addr : 0;
        lcd.fault_expected = (uintptr_t)LCD_DMA_NODE(lcd.bb_eof_count % LCD_BB_COUNT);
        lcd_finish_frame_from_isr();
        return false;
    }
    if (completed > 1) eof_catchups += completed - 1;
#else
    (void)event_data;
#endif
    bool awoken = false;
    while (completed-- && !lcd.frame_finishing) {
        int bb = lcd.bb_eof_count % LCD_BB_COUNT;
        lcd.bb_eof_count++;
        awoken |= fill_bounce_buffer(lcd.bounce_buffer[bb]);
#if defined(CONFIG_IDF_TARGET_ESP32S31)
        if (!lcd.frame_finishing) {
            LCD_DMA_NODE(bb)->dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
            if (lcd.bb_behind_cache) {
                esp_cache_msync(&lcd.dma_nodes[bb], sizeof(lcd.dma_nodes[bb]),
                                ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
            }
        }
#endif
    }
    const uint32_t ran = esp_cpu_get_cycle_count() - now;
    if (ran > eof_isr_max_cycles) eof_isr_max_cycles = ran;
    return awoken;
}
