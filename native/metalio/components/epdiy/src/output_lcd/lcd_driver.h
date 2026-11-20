#pragma once

#include "epd_lcd.h"

#include <stdbool.h>
#include <stdint.h>

typedef bool (*line_cb_func_t)(void*, uint8_t*);
typedef void (*frame_done_func_t)(void*);

void epd_lcd_frame_done_cb(frame_done_func_t, void* payload);
void epd_lcd_line_source_cb(line_cb_func_t, void* payload);
void epd_lcd_start_frame();
/// 超时路径：停 LCD 与 DMA，不调用帧完成回调。/ Timeout path: stop LCD and DMA without the frame-done callback.
void epd_lcd_abort_frame(void);
/// Latched DMA fault: 1 = EOF sequence mismatch, 2 = hardware descriptor error.
unsigned epd_lcd_frame_error(void);
/// Task-context diagnostics after the frame and producer joins.
void epd_lcd_log_frame_error(void);
/// Task context after frame completion; verifies observed CKV pin edges.
void epd_lcd_check_frame_clock(void);
int epd_lcd_frame_pulses(void);
/// Largest EOF-to-EOF interrupt gap since the last call (us); resets it.
uint32_t epd_lcd_take_eof_gap_max_us(void);
/// Longest EOF handler run (us) and coalesced EOFs caught up since the last call; resets both.
void epd_lcd_take_eof_stats(uint32_t* isr_max_us, uint32_t* catchups);
