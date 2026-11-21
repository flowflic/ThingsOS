/* SPDX-License-Identifier: LGPL-3.0-or-later
 * Derived from MindReset/read_pico_firmware@ed1e9331077451a0391769cd6c191568f7901807
 * components/epdiy/src/output_lcd/render_lcd.c. Upstream epdiy LGPL-3.0 applies.
 * Local extension: select an immutable 1 KiB LUT per physical row during a
 * synchronous scan. All scan timing, queues and error paths remain upstream.
 * No source from kiikoread is used. See scan_adapter.md.
 * Metalio (ESP32-S31): no S3 DCache preload; output bytes are converted to
 * panel pixel order by epd_output_reverse_pixels() after masking. See
 * native/metalio/components/epdiy/FORK-METALIO.md.
 */
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include "esp_rom_sys.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_cpu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#if defined(CONFIG_IDF_TARGET_ESP32S31)
#include "esp32s31/rom/cache.h"
#include "hal/cache_ll.h"
#include "soc/soc.h"
#endif

#include "output_common/render_method.h"

#ifdef RENDER_METHOD_LCD

#include <esp_idf_version.h>
#include <esp_log.h>
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#if __has_include(<rom/cache.h>)
#include <rom/cache.h>
#elif __has_include(<esp32s3/rom/cache.h>)
#include <esp32s3/rom/cache.h>
#endif
#endif

#include "epd_internals.h"
#include "output_common/line_queue.h"
#include "output_common/lut.h"
#include "output_common/render_context.h"
#include "epd_board.h"
#include "epdiy.h"
#include "output_lcd/lcd_driver.h"
#include "output_lcd/render_lcd.h"
#include "scan_fault.h"

/* Cause of the last failed refresh, for the diagnostics journal. Producers on
   both cores OR their reasons in; the main task fills and takes the record. */
static _Atomic uint32_t scan_fault_flags;
static thingsos_scan_fault_t scan_fault_record;
static bool scan_fault_pending;
bool thingsos_scan_take_fault(thingsos_scan_fault_t* out) {
    if (!scan_fault_pending || !out) return false;
    *out = scan_fault_record;
    scan_fault_pending = false;
    return true;
}
static uint32_t lcd_fault_flag(unsigned frame_error) {
    switch (frame_error) {
    case 0: return 0;
    case 1: return SCAN_FAULT_EOF_MISMATCH;
    case 2: return SCAN_FAULT_DESCRIPTOR;
    case 3: return SCAN_FAULT_LCD_UNDERRUN;
    case 4: return SCAN_FAULT_CKV;
    default: return SCAN_FAULT_OTHER;
    }
}
static void irq_latency_report(const RenderContext_t* ctx, bool failed) {
    const uint32_t gap = epd_lcd_take_eof_gap_max_us();
    uint32_t isr_us = 0, catchups = 0;
    epd_lcd_take_eof_stats(&isr_us, &catchups);
    if (!failed) return;
    /* Failure only: the cause goes to the diagnostics journal (board.c). */
    uint32_t flags = atomic_load(&scan_fault_flags) | lcd_fault_flag(epd_lcd_frame_error());
    scan_fault_record = (thingsos_scan_fault_t){
        .flags = flags ? flags : SCAN_FAULT_OTHER,
        .phase = (uint32_t)ctx->current_frame,
        .consumed = (uint32_t)ctx->lines_consumed,
        .gap_us = gap,
        .isr_us = isr_us,
        .catchups = catchups,
    };
    scan_fault_pending = true;
    ESP_LOGW("epd_irq", "underrun flags=0x%x eof_gap_max=%u us isr_max=%u us catchups=%u",
             (unsigned)scan_fault_record.flags, (unsigned)gap, (unsigned)isr_us, (unsigned)catchups);
}

static const uint8_t* const* thingsos_row_tables;
void thingsos_scan_row_tables(const uint8_t* const* tables) {
    /* Set/clear only between complete synchronous scans. */
    thingsos_row_tables=tables;
}

/* Metalio E0470A14-AF: the outermost pixels flash black on full refreshes
   unless they are held. After the first clean frame the last output stage
   forces a border of this many physical pixels to 00 (hold, no drive) on every
   scan, as the vendor 2.0.70-debug firmware does. Multiple of 4 (one byte). */
static int border_pixels;
void thingsos_scan_border_hold(int pixels) {
    border_pixels=pixels>0?pixels&~3:0;
}
static inline void IRAM_ATTR border_hold(uint8_t* buf,int row,int width,int height) {
    const int bytes=border_pixels/4,line=width/4;
    if(!bytes)return;
    if(row<border_pixels || row>=height-border_pixels){memset(buf,0,line);return;}
    memset(buf,0,bytes);memset(buf+line-bytes,0,bytes);
}

/* Each scan starts once the first prefill lines are queued. Two producers on
   both cores claim lines alternately, so the line that triggers the scan does
   not prove that every earlier line has been committed; the ISR would then
   underrun (EPD_DRAW_EMPTY_LINE_QUEUE). Track commits per line and let the
   trigger wait, bounded, for the earlier ones (vendor 2.0.66-debug finding). */
#define READY_LINES 1024
/* One phase scan takes about 12 ms; 2 s means the scan is stuck. */
#define SCAN_TIMEOUT_MS 2000
static _Atomic uint8_t line_ready[READY_LINES];
static inline void IRAM_ATTR mark_ready(int line) {
    if(line>=0 && line<READY_LINES)atomic_store_explicit(&line_ready[line],1,memory_order_release);
}
/* Underrun diagnostics in the shape the vendor 2.0.7x build logs them
   (phase, prefill, first missing row, reason, consumed/total). One report per
   phase keeps a failing scan from flooding the console. */
static int s_reported_phase = -1;
static int s_prefill_lines; /* defined below */
/* Line-path cycle accounting: lut (pixel conversion), mask, reverse. Printed
   once per frame so the feeder budget can be compared with one scan line. */
/* Line-path diagnostics, per producer and phase: `touch` reads one word per
   64-byte cache line of the input row (the PSRAM fill cost), `lut` converts the
   now-cached row, `rest` covers mask/border/commit plus any interrupt that
   landed there. Minimums approximate an undisturbed line; average minus minimum
   is interference (interrupts, shared-cache or PSRAM contention). */
typedef struct {
    uint32_t lines, touch_min, lut_min;
    uint64_t touch, lut, rest;
} feed_stat_t;
static unsigned feed_blocks[NUM_RENDER_THREADS];
static feed_stat_t feed_stat[NUM_RENDER_THREADS];
static feed_stat_t feed_total[NUM_RENDER_THREADS];
static unsigned feed_frames;
static uintptr_t feed_input_addr;
/* PSRAM row fill is ~35-40% of a feeder line (measured: touch ~2.7-3.7k of
   ~9k cycles; input buffer at 0x5027xxxx). The S31 L1 DCache is shared by both
   cores and has a region autoload (hardware sequential prefetch) that ESP-IDF
   leaves unconfigured. Point region 0 at the 1ppB difference image the feeders
   stream row by row, so a row miss pulls the following lines in while the
   lookup runs. Only fill behaviour changes; the buffer is CPU-written only. */
static unsigned feed_autoload_restores;
static bool feed_autoload_on(void) {
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    return cache_ll_l1_is_dcache_autoload_enabled(CACHE_LL_ID_ALL);
#else
    return false;
#endif
}
static void feed_autoload_setup(const RenderContext_t* ctx) {
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    static const uint8_t* configured;
    /* Re-arm when something (flash/cache maintenance) switched autoload off;
       a measured second refresh lost the gain (lut 5.6k -> 8.0-8.8k). */
    if (!ctx->data_ptr) return;
    if (ctx->data_ptr == configured) {
        if (feed_autoload_on()) return;
        ++feed_autoload_restores;
    }
    const uintptr_t base = (uintptr_t)ctx->data_ptr & ~(uintptr_t)63;
    const uintptr_t end = ((uintptr_t)ctx->data_ptr
        + (size_t)ctx->display_width * (size_t)ctx->display_height + 63) & ~(uintptr_t)63;
    const bool was = cache_ll_l1_is_dcache_autoload_enabled(CACHE_LL_ID_ALL);
    if (base < SOC_EXTRAM_LOW || end > SOC_EXTRAM_HIGH) {
        ESP_LOGW("epd_feed", "autoload skipped: input %p outside PSRAM (autoload was %d)", (void*)base, (int)was);
        configured = ctx->data_ptr;
        return;
    }
    const struct l1_dcache_autoload_config config = {
        .order = 0,
        .trigger = CACHE_AUTOLOAD_BOTH_TRIGGER,
        .ena0 = 1,
        .addr0 = (uint32_t)base,
        .size0 = (uint32_t)(end - base),
    };
    Cache_Disable_L1_DCache_Autoload();
    Cache_Config_L1_DCache_Autoload(&config);
    Cache_Enable_L1_DCache_Autoload();
    configured = ctx->data_ptr;
    if (feed_autoload_restores <= 3)
        ESP_LOGI("epd_feed", "DCache autoload region %p+%u (was enabled=%d, now=%d, restores=%u)", (void*)base,
                 (unsigned)(end - base), (int)was, (int)feed_autoload_on(), feed_autoload_restores);
#else
    (void)ctx;
#endif
}
static void feed_stat_clear(feed_stat_t* st) {
    for (int i = 0; i < NUM_RENDER_THREADS; ++i)
        st[i] = (feed_stat_t){.touch_min = UINT32_MAX, .lut_min = UINT32_MAX};
}
static void feed_stat_add(feed_stat_t* to, const feed_stat_t* from) {
    for (int i = 0; i < NUM_RENDER_THREADS; ++i) {
        to[i].lines += from[i].lines; to[i].touch += from[i].touch;
        to[i].lut += from[i].lut; to[i].rest += from[i].rest;
        if (from[i].touch_min < to[i].touch_min) to[i].touch_min = from[i].touch_min;
        if (from[i].lut_min < to[i].lut_min) to[i].lut_min = from[i].lut_min;
    }
}
static void feed_stat_log(const char* what, const feed_stat_t* st, unsigned frames) {
    for (int i = 0; i < NUM_RENDER_THREADS; ++i) {
        const uint32_t n = st[i].lines ? st[i].lines : 1;
        ESP_LOGI("epd_feed", "%s core%d frames=%u lines=%u touch=%u/min%u lut=%u/min%u rest=%u cyc/line input=%p blocked=%u autoload=%d restores=%u",
                 what, i, frames, (unsigned)st[i].lines,
                 (unsigned)(st[i].touch / n), (unsigned)(st[i].lines ? st[i].touch_min : 0),
                 (unsigned)(st[i].lut / n), (unsigned)(st[i].lines ? st[i].lut_min : 0),
                 (unsigned)(st[i].rest / n), (void*)feed_input_addr, feed_blocks[i],
                 (int)feed_autoload_on(), feed_autoload_restores);
    }
}
static void report_underrun(RenderContext_t* ctx, const char* reason, int row, uint32_t flag) {
    atomic_fetch_or(&scan_fault_flags, flag);
    int phase = ctx->current_frame;
    if (phase == s_reported_phase) return;
    s_reported_phase = phase;
    ESP_LOGE("epd_feed", "phase=%d prefill=%d first_missing_row=%d reason=%s consumed=%d/%d",
             phase, s_prefill_lines, row, reason, ctx->lines_consumed, ctx->lines_total);
}
/* Producer back-pressure without spinning. Same design as the Metalio vendor
   2.0.79+ feeders (independently written here): a producer whose line queue is
   full publishes feed_waiting, re-checks the queue, then blocks on its binary
   semaphore; the line ISR gives it right after taking a line from that queue.
   Frame end, errors and timeouts release both, so no producer is stranded. The
   old vTaskDelay(0) loop never yielded to lower priorities, so a full queue
   kept a maximum-priority feeder busy on each core for the whole scan. */
static StaticSemaphore_t feed_space_storage[NUM_RENDER_THREADS];
static SemaphoreHandle_t feed_space[NUM_RENDER_THREADS];
static _Atomic bool feed_waiting[NUM_RENDER_THREADS];
static void feed_space_init(void) {
    for (int i = 0; i < NUM_RENDER_THREADS; ++i) {
        if (!feed_space[i]) feed_space[i] = xSemaphoreCreateBinaryStatic(&feed_space_storage[i]);
        atomic_store_explicit(&feed_waiting[i], false, memory_order_relaxed);
        xSemaphoreTake(feed_space[i], 0);
    }
}
static inline void IRAM_ATTR feed_wake_isr(int thread, BaseType_t* awoken) {
    if (atomic_load_explicit(&feed_waiting[thread], memory_order_acquire) &&
        atomic_exchange_explicit(&feed_waiting[thread], false, memory_order_acq_rel))
        xSemaphoreGiveFromISR(feed_space[thread], awoken);
}
static void IRAM_ATTR feed_release_all_isr(BaseType_t* awoken) {
    for (int i = 0; i < NUM_RENDER_THREADS; ++i) {
        atomic_store_explicit(&feed_waiting[i], false, memory_order_release);
        if (feed_space[i]) xSemaphoreGiveFromISR(feed_space[i], awoken);
    }
}
static void feed_release_all(void) {
    for (int i = 0; i < NUM_RENDER_THREADS; ++i) {
        atomic_store_explicit(&feed_waiting[i], false, memory_order_release);
        if (feed_space[i]) xSemaphoreGive(feed_space[i]);
    }
}
/* Next free slot of this producer's queue; NULL after an error or a 1 s stall
   (elapsed time, not a poll count). */
static uint8_t* IRAM_ATTR feed_wait_buffer(RenderContext_t* ctx, LineQueue_t* lq, int thread, int line) {
    int64_t start = 0;
    for (;;) {
        if (ctx->error & EPD_DRAW_EMPTY_LINE_QUEUE) {
            report_underrun(ctx, "isr-empty", ctx->lines_consumed, SCAN_FAULT_FEED_EMPTY);
            return NULL;
        }
        uint8_t* buf = lq_current(lq);
        if (buf != NULL) return buf;
        const int64_t now = esp_timer_get_time();
        if (!start) start = now;
        if (now - start >= 1000000) {
            ctx->error |= EPD_DRAW_EMPTY_LINE_QUEUE;
            report_underrun(ctx, "queue-full", line, SCAN_FAULT_FEED_FULL);
            return NULL;
        }
        /* Publish, then re-check: a line taken between the two checks has
           already seen feed_waiting and given (or will give) the semaphore. */
        atomic_store_explicit(&feed_waiting[thread], true, memory_order_release);
        buf = lq_current(lq);
        if (buf != NULL) {
            atomic_store_explicit(&feed_waiting[thread], false, memory_order_release);
            return buf;
        }
        ++feed_blocks[thread];
        xSemaphoreTake(feed_space[thread], pdMS_TO_TICKS(2) + 1);
        atomic_store_explicit(&feed_waiting[thread], false, memory_order_release);
    }
}
/* The whole prefill prefix must be committed before the scan starts: claiming
   row N does not prove the other core committed the rows before it (vendor
   2.0.66-debug finding). Bounded at 2 s; on timeout the frame is reported as an
   underrun instead of starting with holes. */
static bool wait_prefix(RenderContext_t* ctx,int first,int line) {
    const int64_t deadline=esp_timer_get_time()+2000000;
    for(int i=first;i<line && i<READY_LINES;++i) {
        while(!atomic_load_explicit(&line_ready[i],memory_order_acquire)) {
            if(ctx->error & EPD_DRAW_EMPTY_LINE_QUEUE) {
                report_underrun(ctx,"prefix-abort",i,SCAN_FAULT_PREFIX);
                return false;
            }
            if(esp_timer_get_time()>deadline){
                ctx->error|=EPD_DRAW_EMPTY_LINE_QUEUE;
                report_underrun(ctx,"prefix-timeout",i,SCAN_FAULT_PREFIX);
                return false;
            }
            esp_rom_delay_us(1);
        }
    }
    return true;
}

// declare vector optimized line mask application.
void epd_apply_line_mask_VE(uint8_t* line, const uint8_t* mask, int mask_len);

__attribute__((optimize("O3"))) static bool IRAM_ATTR
retrieve_line_isr(RenderContext_t* ctx, uint8_t* buf) {
    if (ctx->lines_consumed >= ctx->lines_total) {
        return false;
    }
    int thread = ctx->line_threads[ctx->lines_consumed];
    if (thread >= NUM_RENDER_THREADS) {
        ctx->error |= EPD_DRAW_EMPTY_LINE_QUEUE;
        memset(buf, 0x00, ctx->display_width / 4);
        ctx->lines_consumed += 1;
        return pdFALSE;
    }

    LineQueue_t* lq = &ctx->line_queues[thread];

    BaseType_t awoken = pdFALSE;

    if (lq_read(lq, buf) != 0) {
        ctx->error |= EPD_DRAW_EMPTY_LINE_QUEUE;
        memset(buf, 0x00, ctx->display_width / 4);
        feed_release_all_isr(&awoken);
    } else {
        feed_wake_isr(thread, &awoken);
    }

    if (ctx->lines_consumed >= ctx->display_height) {
        memset(buf, 0x00, ctx->display_width / 4);
    }
    ctx->lines_consumed += 1;
    return awoken;
}

/// start the next frame in the current update cycle
static void IRAM_ATTR handle_lcd_frame_done(RenderContext_t* ctx) {
    epd_lcd_frame_done_cb(NULL, NULL);
    epd_lcd_line_source_cb(NULL, NULL);

    // 扫描先于供数结束时解除生产者等待，禁止残留行进入下一相位。
    // Release producers if scan ends before consumption; never carry stale lines into the next phase.
    if (ctx->lines_consumed < ctx->lines_total || epd_lcd_frame_error()) {
        ctx->error |= EPD_DRAW_EMPTY_LINE_QUEUE;
    }

    BaseType_t task_awoken = pdFALSE;
    feed_release_all_isr(&task_awoken);
    xSemaphoreGiveFromISR(ctx->frame_done, &task_awoken);

    portYIELD_FROM_ISR();
}

void lcd_do_update(RenderContext_t* ctx) {
    epd_set_mode(1);
    feed_stat_clear(feed_total);
    feed_frames = 0;
    feed_autoload_setup(ctx);
    feed_input_addr = (uintptr_t)ctx->data_ptr;
    (void)epd_lcd_take_eof_gap_max_us();
    epd_lcd_take_eof_stats(NULL, NULL);
    atomic_store(&scan_fault_flags, 0);
    scan_fault_pending = false;
    for (int i = 0; i < NUM_RENDER_THREADS; ++i) feed_blocks[i] = 0;

    for (uint8_t k = 0; k < ctx->cycle_frames; k++) {
        epd_lcd_frame_done_cb((frame_done_func_t)handle_lcd_frame_done, ctx);
        prepare_context_for_next_frame(ctx);
        for (int i = 0; i < READY_LINES; i++) atomic_store_explicit(&line_ready[i], 0, memory_order_relaxed);
        atomic_thread_fence(memory_order_release);

        feed_space_init();
        feed_stat_clear(feed_stat);
        // Start both feeders together (scheduler suspended only around the two
        // notifications) so neither fills its queue while the other still waits.
        vTaskSuspendAll();
        xTaskNotifyGive(ctx->feed_tasks[!xPortGetCoreID()]);
        xTaskNotifyGive(ctx->feed_tasks[xPortGetCoreID()]);
        xTaskResumeAll();

        // transmission is started in renderer threads, now wait util it's done.
        // A scan that never starts or never ends is an underrun: stop the LCD
        // and let the caller power the panel down instead of hanging with the
        // rails on (Metalio vendor firmware uses the same 2 s bound).
        if (xSemaphoreTake(ctx->frame_done, pdMS_TO_TICKS(SCAN_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGE("epd_lcd", "LCD frame %u/%u timeout", (unsigned)k + 1, (unsigned)ctx->cycle_frames);
            atomic_fetch_or(&scan_fault_flags, SCAN_FAULT_FRAME_TIMEOUT);
            ctx->error |= EPD_DRAW_EMPTY_LINE_QUEUE;
            epd_lcd_line_source_cb(NULL, NULL);
            epd_lcd_abort_frame();
            xSemaphoreTake(ctx->frame_done, 0);
        }
        epd_lcd_check_frame_clock();
        if(epd_lcd_frame_error())ctx->error|=EPD_DRAW_EMPTY_LINE_QUEUE;
        if(ctx->error)feed_release_all();

        for (int i = 0; i < NUM_RENDER_THREADS; i++) {
            if (xSemaphoreTake(ctx->feed_done_smphr[i], pdMS_TO_TICKS(SCAN_TIMEOUT_MS)) != pdTRUE) {
                ESP_LOGE("epd_lcd", "feed %d timeout at frame %u", i, (unsigned)k);
                atomic_fetch_or(&scan_fault_flags, SCAN_FAULT_FEED_TIMEOUT);
                ctx->error |= EPD_DRAW_EMPTY_LINE_QUEUE;
            }
        }

        if (ctx->error) feed_stat_log("underrun-phase", feed_stat, 1);
        feed_stat_add(feed_total, feed_stat);
        ++feed_frames;

        if (epd_lcd_frame_error()) {
            epd_lcd_log_frame_error();
            ESP_LOGE("epd_lcd", "DMA fault=%u at phase=%u consumed=%d/%d",
                     epd_lcd_frame_error(), (unsigned)k, ctx->lines_consumed, ctx->lines_total);
        }

        // DMA 回调和生产者均已停止后再清队列，由调用方执行欠载恢复。
        // Reset only after DMA callbacks and producers stop; let the caller recover from underrun.
        if (ctx->error) {
            for (int i = 0; i < NUM_RENDER_THREADS; i++) {
                lq_reset(&ctx->line_queues[i]);
            }
            break;
        }

        ctx->current_frame++;

        // make the watchdog happy.
        vTaskDelay(0);
    }

    epd_lcd_line_source_cb(NULL, NULL);
    epd_lcd_frame_done_cb(NULL, NULL);

    epd_set_mode(0);
    if (epd_lcd_frame_error())
        ESP_LOGW("epd_lcd","CKV final phase pulses=%d fault=%u",epd_lcd_frame_pulses(),epd_lcd_frame_error());
    irq_latency_report(ctx, ctx->error != 0);
}

__attribute__((optimize("O3"))) static bool IRAM_ATTR
push_pixels_isr(RenderContext_t* ctx, uint8_t* buf) {
    // Output no-op outside of drawn area
    if (ctx->lines_consumed < ctx->area.y) {
        memset(buf, 0, ctx->display_width / 4);
    } else if (ctx->lines_consumed >= ctx->area.y + ctx->area.height) {
        memset(buf, 0, ctx->display_width / 4);
    } else {
        memcpy(buf, ctx->static_line_buffer, ctx->display_width / 4);
        border_hold(buf, ctx->lines_consumed, ctx->display_width, ctx->display_height);
    }
    ctx->lines_consumed += 1;
    return pdFALSE;
}

/**
 * Populate the line mask for use in epd_push_pixels.
 */
static bool push_pixels_populate_line(RenderContext_t* ctx, int color) {
    // Select fill pattern by draw color
    int fill_byte = 0;
    switch (color) {
        case 0:
            fill_byte = DARK_BYTE;
            break;
        case 1:
            fill_byte = CLEAR_BYTE;
            break;
        default:
            fill_byte = 0x00;
    }

    // Compute a line mask based on the drawn area
    uint8_t* dirtyness = malloc(ctx->display_width / 2);
    if (!dirtyness) return false;

    memset(dirtyness, 0, ctx->display_width / 2);

    for (int i = 0; i < ctx->display_width; i++) {
        if ((i >= ctx->area.x) && (i < ctx->area.x + ctx->area.width)) {
            dirtyness[i / 2] |= i % 2 ? 0xF0 : 0x0F;
        }
    }
    epd_populate_line_mask(ctx->line_mask, dirtyness, ctx->display_width / 4);

    // mask the line pattern with the populated mask
    memset(ctx->static_line_buffer, fill_byte, ctx->display_width / 4);
    epd_apply_line_mask(ctx->static_line_buffer, ctx->line_mask, ctx->display_width / 4);
    epd_output_reverse_pixels(ctx->static_line_buffer, ctx->display_width / 4);

    free(dirtyness);
    return true;
}

void epd_push_pixels_lcd(RenderContext_t* ctx, short time, int color) {
    ctx->current_frame = 0;
    ctx->lines_total = ctx->display_height;
    ctx->lines_consumed = 0;
    /* Read by the cache-safe LCD EOF interrupt: keep it out of PSRAM. */
    ctx->static_line_buffer = heap_caps_malloc(ctx->display_width / 4, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!ctx->static_line_buffer || !push_pixels_populate_line(ctx, color)) {
        free(ctx->static_line_buffer);
        ctx->static_line_buffer = NULL;
        ctx->error = EPD_DRAW_FAILED_ALLOC;
        return;
    }

    epd_lcd_frame_done_cb((frame_done_func_t)handle_lcd_frame_done, ctx);
    epd_lcd_line_source_cb((line_cb_func_t)&push_pixels_isr, ctx);

    epd_set_mode(1);
    epd_lcd_start_frame();
    if (xSemaphoreTake(ctx->frame_done, pdMS_TO_TICKS(SCAN_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE("epd_lcd", "push pixels frame timeout");
        ctx->error |= EPD_DRAW_EMPTY_LINE_QUEUE;
        epd_lcd_line_source_cb(NULL, NULL);
        epd_lcd_abort_frame();
        xSemaphoreTake(ctx->frame_done, 0);
    }
    epd_lcd_check_frame_clock();
    if(epd_lcd_frame_error())ctx->error|=EPD_DRAW_EMPTY_LINE_QUEUE;
    epd_set_mode(0);

    if (epd_lcd_frame_error()) {
        epd_lcd_log_frame_error();
        ESP_LOGE("epd_lcd", "DMA fault=%u during physical clear consumed=%d/%d",
                 epd_lcd_frame_error(), ctx->lines_consumed, ctx->lines_total);
    }
    free(ctx->static_line_buffer);
    ctx->static_line_buffer = NULL;
}

#define int_min(a, b) (((a) < (b)) ? (a) : (b))

/* Prefill before the scan starts. The vendor raised this to 192 rows for S31;
   measured here it costs ~48 ms extra per page turn and does not change the
   20 MHz outcome (the feeder converts ~34 px/us and 20 MHz needs ~62 px/us, so
   any prefill depth drains within a frame). Kept at the vendor's validated
   127-row value until the line path itself is faster; fixed row ownership and
   the full-prefix commit below stay. The two producers own alternating rows, so
   each queue must hold at least half of the prefill (2 x 128 slots in render.c). */
#define THINGSOS_PREFILL_LINES 192
static int s_prefill_lines = THINGSOS_PREFILL_LINES;

void epd_lcd_set_prefill_lines(int lines) {
    if (lines < 4) lines = 4;
    s_prefill_lines = lines;
}

int epd_lcd_prefill_lines(void) {
    return s_prefill_lines;
}
__attribute__((optimize("O3"))) void IRAM_ATTR
lcd_calculate_frame(RenderContext_t* ctx, int thread_id) {
    assert(ctx->lut_lookup_func != NULL);
    uint8_t* input_line = ctx->feed_line_buffers[thread_id];

    LineQueue_t* lq = &ctx->line_queues[thread_id];
    int l = 0;

    // 另一生产者可能已启动并报告欠载；不得再次启动同一帧。
    // The other producer may have started and underrun already; never start the frame twice.
    if (ctx->error) return;

    // line must be able to hold 2-pixel-per-byte or 1-pixel-per-byte data
    memset(input_line, 0x00, ctx->display_width);

    EpdRect area = ctx->area;
    int min_y, max_y, bytes_per_line, _ppB;
    const uint8_t* ptr_start;
    get_buffer_params(ctx, &bytes_per_line, &ptr_start, &min_y, &max_y, &_ppB);

    assert(area.width == ctx->display_width && area.x == 0);

    /* Mask and pixel order are frame-wide facts. A full-screen frame (every
       column dirty, as stagger.inc forces for turns) has an all-0xFF mask, so
       the per-line AND is a no-op; and the 1ppB page lookup (S31) already
       produces panel order, so there is nothing left to reverse. */
    const bool mask_full = epd_line_mask_is_full(ctx->line_mask, ctx->display_width / 4);
    const bool reverse_pixels = EPD_OUTPUT_REVERSE_PIXELS && !ctx->lut_output_panel_order;

    // index of the line that triggers the frame output when processed
    const int trigger_line = int_min(s_prefill_lines, ctx->lines_total - 1);

    /* Fixed ownership: producer `thread_id` owns every NUM_RENDER_THREADS-th
       row. Dynamic claims let one core fill its queue before a deeper prefill
       could start, which is exactly the underrun the vendor removed in 2.0.7x. */
    for (l = thread_id; l < ctx->lines_total; l += NUM_RENDER_THREADS) {
        ctx->line_threads[l] = (uint8_t)thread_id;

        // queue is sufficiently filled to fill both bounce buffers, frame
        // can begin
        if (l == trigger_line) {
            if (!wait_prefix(ctx, 0, l)) return;
            epd_lcd_line_source_cb((line_cb_func_t)&retrieve_line_isr, ctx);
            epd_lcd_start_frame();
        }

        if (l < min_y || l >= max_y
            || (ctx->drawn_lines != NULL && !ctx->drawn_lines[l - area.y])) {
            uint8_t* buf = feed_wait_buffer(ctx, lq, thread_id, l);
            if (buf == NULL) { lq_reset(lq); return; }
            memset(buf, 0x00, lq->element_size);
            lq_commit(lq);
            mark_ready(l);
            continue;
        }

        uint32_t* lp = (uint32_t*)input_line;
        const uint8_t* ptr = ptr_start + bytes_per_line * (l - min_y);

#if defined(CONFIG_IDF_TARGET_ESP32S3)
        Cache_Start_DCache_Preload((uint32_t)ptr, ctx->display_width, 0);
#endif

        lp = (uint32_t*)ptr;

        uint8_t* buf = feed_wait_buffer(ctx, lq, thread_id, l);
        if (buf == NULL) { lq_reset(lq); return; }
        const uint8_t* table=thingsos_row_tables ? thingsos_row_tables[l] : NULL;
        const uint32_t t_start = esp_cpu_get_cycle_count();
        /* No separate row touch: with the DCache autoload streaming the row,
           the lookup overlaps with the fill. `touch` stays 0 and `lut`
           includes any remaining fill stalls (compare 8.3k before autoload). */
        const uint32_t t_convert = esp_cpu_get_cycle_count();
        ctx->lut_lookup_func(lp, buf, table ? table : ctx->conversion_lut, ctx->display_width);
        const uint32_t t_lut = esp_cpu_get_cycle_count();

        // apply the line mask
        if(!mask_full) epd_apply_line_mask_VE(buf, ctx->line_mask, ctx->display_width / 4);
#if EPD_OUTPUT_REVERSE_PIXELS
        if(reverse_pixels) epd_output_reverse_pixels(buf, ctx->display_width / 4);
#endif
        border_hold(buf, l, ctx->display_width, ctx->display_height);

        lq_commit(lq);
        mark_ready(l);
        const uint32_t t_end = esp_cpu_get_cycle_count();
        feed_stat_t* st = &feed_stat[thread_id];
        const uint32_t touch = t_convert - t_start, lut = t_lut - t_convert;
        ++st->lines; st->touch += touch; st->lut += lut; st->rest += t_end - t_lut;
        if (touch < st->touch_min) st->touch_min = touch;
        if (lut < st->lut_min) st->lut_min = lut;
    }
}

#endif
