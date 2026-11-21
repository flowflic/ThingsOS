# epdiy fork changes for Metalio E-Ink4-Plus (ESP32-S31)

Base: the trimmed Read Pico epdiy fork in
`references/read-pico-official/components/epdiy` (MindReset/read_pico_firmware
`ed1e9331077451a0391769cd6c191568f7901807`, LGPL-3.0-or-later). The reference
checkout is unchanged; this copy carries the following changes. S3 builds keep
the original behaviour.

| File | Change |
| --- | --- |
| `src/highlevel_bands.inc`, `src/highlevel.c/.h` | Submit up to two portrait full-width row bands in one scan. Exact column masks exclude vector alignment padding and gaps. Publish only successfully scanned pixels to the back buffer. |
| `src/output_common/render_method.h` | Accept ESP32-S31; `EPD_OUTPUT_REVERSE_PIXELS` on S31. |
| `src/output_common/lut.c` | S31: 1ppB lookup over a four-page, pre-shifted 1 KiB table (`lut_1ppb_pages.h`) that emits panel order directly, so the feeder needs neither index scaling nor `epd_output_reverse_pixels()`. S3 keeps `lut.S` and the 256×4-byte table. `epd_output_reverse_pixels()` (2-bit group reversal, S31 only) stays for the 2ppB/8ppB and push-pixels paths. C versions of `epd_interlace_4bpp_line_VE` and `epd_apply_line_mask_VE` for targets without the S3 PIE vector unit. |
| `src/output_common/lut_1ppb_pages.h` | Pure C builder and line converter for the page layout; the host test `native/tests/lut_1ppb_test.c` checks it against the old lookup plus reversal. |
| `src/output_common/lut.h` | `LutFunctionPair.output_panel_order`: tells the feeder whether the lookup already wrote panel order. Declares `epd_output_reverse_pixels()`. |
| `src/output_common/render_context.c/.h` | `epd_line_mask_is_full()` and `RenderContext_t.line_mask_full` / `.lut_output_panel_order`. |
| `src/output_lcd/lcd_driver.c` | S31 branch: AXI-GDMA channel and 8-byte descriptors, DMA-owned descriptors handed back in the EOF ISR, cache-synced 64-byte-aligned bounce buffers of 8 lines, data pins through the LCD IOMUX, LCD clock gate/divider and trans-buffer setup, data-phase-only output, frame end by line count when VSYNC is late, RMT CKV divider derived from the clock tree. |
| `src/output_lcd/lcd_dma_events.inc`, `lcd_driver.c/.h` (2026-11-16/17) | S31 descriptors occupy separate 64-byte cache lines. Validate hardware EOF descriptor identity before refill; latch EOF/descriptor faults with address diagnostics, stop the current scan and propagate failure through the synchronous renderer. Retain the SDK RGB cyclic-bounce DMA strategy: the first candidate's owner-check/TX-writeback change failed its device startup gate and was withdrawn. Validation: `native/docs/WEREAD-LOADING-STRIPES-20261116.md`. |
| `src/output_lcd/lcd_driver.c` (2026-11-20) | LCD GDMA channel allocated with `intr_priority = 3` and `isr_cache_safe` (was all-zero: lowest free level, not IRAM). A 2026-11-20 capture during Cubox image downloads showed `eof != expected` while both feeders were blocked on full queues, i.e. the EOF refill interrupt on CPU0 was late by more than one 8-line bounce buffer (136 us at 20 MHz). The EOF path (handler, refill, line callbacks, line queues, descriptors, bounce buffers) is IRAM/internal RAM only; `scan_adapter.c` allocates the push-pixels line in internal RAM for the same reason. Device acceptance pending. |
| `src/output_lcd/lcd_dma_events.inc`, `lcd_driver.c/.h` (2026-11-20, diagnostics) | The EOF handler records the largest EOF-to-EOF gap per frame (`epd_lcd_take_eof_gap_max_us`). During investigation `scan_adapter.c` wrapped `esp_cache_msync` (`-Wl,--wrap`) and logged `epd_irq` near misses. 0.2.1 removed the wrapper and the near-miss logs; the gap, handler time and catch-up count now feed only the failure record (diagnostics kind 11 stage 5/6) plus one warning line per failed scan. The firmware also enables `CONFIG_ESP_MM_CACHE_MSYNC_C2M_CHUNKED_OPS` (4 KiB). Reason: the level-3 IRAM interrupt still showed `eof != expected` with no cache-disable during the scan, and every msync holds a cross-core spinlock with interrupts off, including the one in the EOF handler. |
| `src/output_lcd/lcd_driver.c`, `lcd_dma_events.inc` (2026-11-20, 3-buffer ring) | S31 uses `LCD_BB_COUNT = 3` bounce buffers (8 lines each) in the DMA ring instead of 2. When one EOF interrupt reports up to `LCD_BB_COUNT-1` completed descriptors (coalesced EOF), the handler refills them in ring order. The descriptor identity check stays: the previous descriptor again, a lapped ring or an unknown address still stops the scan. The frame finishes after `display_lines + LCD_BB_COUNT * BOUNCE_BUF_LINES` lines are fed. Evidence: `epd_irq` showed EOF gaps of 200-253 us in normal use and exactly 272 us (one lost EOF) at the underrun, with no long msync. Host test `lcd_dma_events_test.c` covers catch-up, lapping and finishing mid-catch-up. Device acceptance pending. |
| `src/output_common/rmt_compat.c` | Force RMT memory power on S31. |
| `src/output_common/rmt_loop.inc`, `src/output_lcd/lcd_clock_check.inc`, LCD driver (2026-11-17) | Stop CKV before rearm/item changes and at frame completion. S31 PCNT monitors actual CKV pin edges per phase; LCD hardware underrun is enabled and propagated. See `native/docs/WEREAD-CKV-RESTART-20261117.md` for evidence, tests and pending optical acceptance. |
| `src/output_lcd/render_lcd.c` | No S3 DCache preload on S31; pixel order reversal after masking. |
| `src/board/epd_board.c` | `esp_cpu_get_cycle_count()` instead of the Xtensa CCOUNT macro. |
| `src/epd_board.h` | Xtensa header only when present. |
| `src/displays.c`, `src/epd_display.h` | `E0470_DISPLAY_8BIT`: the same 684×1216 panel on an 8-bit bus at 20 MHz. |
| `CMakeLists.txt` | Assembly sources only on S3; extra IDF dependencies (`esp_mm`, `esp_hal_i2s`). |

Outside this component, `native/firmware/components/thingsos_board/scan_adapter.c`
(the ThingsOS replacement for `render_lcd.c`) adds the edge border hold
(width `METALIO_WHITE_BORDER_PIXELS`, currently 0 = off) and
the bounded per-line ready check before a scan starts, both from the vendor
2.0.76-debug snapshot for the E0470A14-AF panel.

The S31 register values and the pixel order follow CloudZao/Metalio-E-INK4-Plus
`main/boards/metalio-e-ink4-plus/display/output_lcd/lcd_driver.c` and
`epdiy_riscv_ve_stub.c` (MIT, Copyright (c) 2025 Shenzhen Xinzhi Future
Technology Co., Ltd.). None of this has been validated on hardware yet.
