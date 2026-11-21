#include "lcd_driver.h"
#include "epdiy.h"

#include "../output_common/render_method.h"
#include "../output_common/rmt_compat.h"
#include "esp_heap_caps.h"
#include "esp_intr_alloc.h"
#include "hal/gpio_types.h"

#ifdef RENDER_METHOD_LCD

#include <assert.h>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <esp_private/periph_ctrl.h>
#include <soc/clk_tree_defs.h>
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
#include <esp_clk_tree.h>
// esp-idf-configdep may strip esp_private headers; declare the needed API locally.
esp_err_t esp_clk_tree_enable_src(soc_module_clk_t clk_src, bool enable);
#endif

#include <driver/gpio.h>
#include <esp_check.h>
#include <esp_err.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_private/gdma.h>
#include <esp_timer.h>
#include <esp_cpu.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <hal/dma_types.h>
#include <hal/gdma_ll.h>
#include <hal/gpio_hal.h>
#include <hal/lcd_hal.h>
#include <hal/lcd_ll.h>

#if defined(CONFIG_IDF_TARGET_ESP32S31)
// S31：LCD 挂在 AXI-GDMA，数据脚走 IOMUX，bounce buffer 经 cache。
// 寄存器取值沿用 CloudZao/Metalio-E-INK4-Plus 已在本机型上验证过的 lcd_driver.c（MIT）。
// / S31: LCD sits on AXI-GDMA, data pins use IOMUX, bounce buffers are cached.
// Register values follow CloudZao/Metalio-E-INK4-Plus lcd_driver.c (MIT), validated on this board.
#include <esp_cache.h>
#include <driver/pulse_cnt.h>
#include <esp_private/gpio.h>
#include <hal/lcd_periph.h>
#include <soc/hp_sys_clkrst_struct.h>
#define LCD_PERIPH_SIG(member) soc_lcd_rgb_signals[0].member
#define LCD_GDMA_NEW_CHANNEL gdma_new_axi_channel
#define LCD_GDMA_DESC_ALIGN 64
#define LCD_BB_ALIGN 64
typedef dma_descriptor_align8_t lcd_dma_desc_t;
typedef struct __attribute__((aligned(64))) {
    lcd_dma_desc_t desc;
} lcd_dma_node_t;
#define LCD_DMA_NODE(idx) (&lcd.dma_nodes[idx].desc)
#elif ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
#if __has_include(<rom/cache.h>)
#include <rom/cache.h>
#elif __has_include(<esp32s3/rom/cache.h>)
#include <esp32s3/rom/cache.h>
#endif
typedef struct {
    const shared_periph_module_t module;
    const int irq_id;
    const int data_sigs[LCD_LL_GET(RGB_BUS_WIDTH)];
    const int hsync_sig;
    const int vsync_sig;
    const int pclk_sig;
    const int de_sig;
    const int disp_sig;
} soc_lcd_rgb_signal_desc_t;
extern const soc_lcd_rgb_signal_desc_t soc_lcd_rgb_signals[LCD_LL_GET(RGB_PANEL_NUM)];
#define LCD_PERIPH_SIG(member) soc_lcd_rgb_signals[0].member
#define LCD_GDMA_NEW_CHANNEL gdma_new_ahb_channel
#define LCD_GDMA_DESC_ALIGN 4
#define LCD_BB_ALIGN 4
typedef dma_descriptor_t lcd_dma_desc_t;
typedef lcd_dma_desc_t lcd_dma_node_t;
#define LCD_DMA_NODE(idx) (&lcd.dma_nodes[idx])
#else
#include <rom/cache.h>
#include <soc/lcd_periph.h>
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 2)
#define LCD_PERIPH_SIG(member) lcd_periph_rgb_signals.panels[0].member
#else
#define LCD_PERIPH_SIG(member) lcd_periph_signals.panels[0].member
#endif
#define LCD_GDMA_DESC_ALIGN 4
#define LCD_BB_ALIGN 4
typedef dma_descriptor_t lcd_dma_desc_t;
typedef lcd_dma_desc_t lcd_dma_node_t;
#define LCD_DMA_NODE(idx) (&lcd.dma_nodes[idx])
#endif

#include "hal/gpio_hal.h"

gpio_hal_context_t hal = { .dev = GPIO_HAL_GET_HW(GPIO_PORT_0) };

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
#undef __DECLARE_RCC_ATOMIC_ENV
#endif

#define TAG "epdiy"

static inline int min(int x, int y) {
    return x < y ? x : y;
}
static inline int max(int x, int y) {
    return x > y ? x : y;
}

#define S3_LCD_PIN_NUM_BK_LIGHT -1
// #define S3_LCD_PIN_NUM_MODE           4

#define LINE_BATCH 1000
#if defined(CONFIG_IDF_TARGET_ESP32S31)
// AXI EOF 中断开销更大，每个 bounce 装 8 行，688 行仍能整除。
// / AXI EOF interrupts cost more; 8 lines per bounce buffer, still dividing 688.
#define BOUNCE_BUF_LINES 8
/* Three bounce buffers in the DMA ring: a coalesced EOF (one refill interrupt
   late by up to one more buffer, 136 us at 20 MHz) is caught up instead of
   aborting the scan. 2026-11-20 captures showed EOF gaps of 200-253 us in
   normal use and exactly 272 us (one EOF lost) at each underrun. */
#define LCD_BB_COUNT 3
#else
#define BOUNCE_BUF_LINES 4
#define LCD_BB_COUNT 2
#endif

#define RMT_CKV_CHAN RMT_COMPAT_CHANNEL_1

#if defined(CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE)
#define EPDIY_DATA_CACHE_LINE_SIZE CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE
#elif defined(CONFIG_DATA_CACHE_LINE_SIZE)
#define EPDIY_DATA_CACHE_LINE_SIZE CONFIG_DATA_CACHE_LINE_SIZE
#else
#define EPDIY_DATA_CACHE_LINE_SIZE 64
#endif

// spinlock for protecting the critical section at frame start
static portMUX_TYPE frame_start_spinlock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    lcd_hal_context_t hal;
    intr_handle_t vsync_intr;
    intr_handle_t done_intr;

    frame_done_func_t frame_done_cb;
    line_cb_func_t line_source_cb;
    void* line_cb_payload;
    void* frame_cb_payload;

    int line_length_us;
    int line_time_01us;
    int line_cycles;
    int lcd_res_h;

    /// 实际发给 RMT 的 CKV 高电平时长，单位 0.1us。行时间会随 pclk 变化，
    /// 所以不能直接用 config 里那个按默认 pclk 标定的固定值。
    int ckv_high_time;

    LcdEpdConfig_t config;

    uint8_t* bounce_buffer[LCD_BB_COUNT];
    // size of a single bounce buffer
    size_t bb_size;
    size_t bb_eof_count;
    size_t batches;

    /// 本帧已装入 bounce 的行数；S31 上 VSYNC 迟到时据此收尾。
    /// / Lines loaded into bounce buffers this frame; ends the frame on S31 if VSYNC is late.
    size_t lines_fed;
    /// 防止 VSYNC 与行计数重复收尾。/ Prevents a double finish from VSYNC and the line count.
    bool frame_finishing;
    volatile unsigned frame_error;
    uintptr_t fault_eof, fault_expected;
    /// bounce buffer 经 cache 时补数后要 msync。/ Cached bounce buffers need msync after refill.
    bool bb_behind_cache;

    // Number of DMA descriptors that used to carry the frame buffer
    size_t num_dma_nodes;
    // DMA channel handle
    gdma_channel_handle_t dma_chan;
    // DMA descriptors pool
    lcd_dma_node_t* dma_nodes;

    /// LCD peripheral source clock frequency (Hz), from clock tree when available.
    uint32_t src_clk_hz;

    /// The number of bytes in a horizontal display register line.
    int line_bytes;

    // With 8 bit bus width, we need a dummy cycle before the actual data,
    // because the LCD peripheral behaves weirdly.
    // Also see:
    // https://blog.adafruit.com/2022/06/14/esp32uesday-hacking-the-esp32-s3-lcd-peripheral/
    int dummy_bytes;

    /// The number of lines of the display
    int display_lines;
} s3_lcd_t;

static s3_lcd_t lcd = { 0 };
#if defined(CONFIG_IDF_TARGET_ESP32S31)
static pcnt_unit_handle_t ckv_counter;
static pcnt_channel_handle_t ckv_counter_channel;
#endif
static int ckv_pulses;

static esp_err_t init_ckv_counter(void) {
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    pcnt_unit_config_t unit = {.low_limit=-32768,.high_limit=32767};
    ESP_RETURN_ON_ERROR(pcnt_new_unit(&unit,&ckv_counter),TAG,"CKV counter unit");
    pcnt_chan_config_t channel = {.edge_gpio_num=lcd.config.bus.ckv,.level_gpio_num=-1};
    ESP_RETURN_ON_ERROR(pcnt_new_channel(ckv_counter,&channel,&ckv_counter_channel),TAG,"CKV counter channel");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(ckv_counter_channel,PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                                   PCNT_CHANNEL_EDGE_ACTION_HOLD),TAG,"CKV edges");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(ckv_counter_channel,PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                                    PCNT_CHANNEL_LEVEL_ACTION_KEEP),TAG,"CKV level");
    ESP_RETURN_ON_ERROR(pcnt_unit_enable(ckv_counter),TAG,"CKV counter enable");
    ESP_RETURN_ON_ERROR(pcnt_unit_start(ckv_counter),TAG,"CKV counter start");
    ESP_LOGI(TAG,"CKV hardware pulse monitor on GPIO%d",lcd.config.bus.ckv);
#endif
    return ESP_OK;
}
#include "lcd_clock_check.inc"

int epd_lcd_frame_pulses(void) {return ckv_pulses;}


unsigned IRAM_ATTR epd_lcd_frame_error(void) {
    return lcd.frame_error;
}
void epd_lcd_log_frame_error(void) {
    if (lcd.frame_error)
        ESP_LOGE(TAG, "DMA fault=%u eof=%p expected=%p completions=%u lines=%u",
                 lcd.frame_error, (void*)lcd.fault_eof, (void*)lcd.fault_expected,
                 (unsigned)lcd.bb_eof_count, (unsigned)lcd.lines_fed);
}

void IRAM_ATTR epd_lcd_line_source_cb(line_cb_func_t line_source, void* payload) {
    lcd.line_source_cb = line_source;
    lcd.line_cb_payload = payload;
}

void IRAM_ATTR epd_lcd_frame_done_cb(frame_done_func_t cb, void* payload) {
    lcd.frame_done_cb = cb;
    lcd.frame_cb_payload = payload;
}

static IRAM_ATTR void lcd_finish_frame_from_isr(void) {
    if (lcd.frame_finishing) {
        return;
    }
    lcd.frame_finishing = true;
    lcd_ll_stop(lcd.hal.dev);
    rmt_compat_tx_stop(RMT_CKV_CHAN);
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    lcd.line_source_cb = NULL;
    gdma_stop(lcd.dma_chan);
#endif
    if (lcd.frame_done_cb != NULL) {
        (*lcd.frame_done_cb)(lcd.frame_cb_payload);
    }
}

void epd_lcd_abort_frame(void) {
    // Task context: drop the ISR-only frame-done callback before stopping.
    lcd.frame_done_cb = NULL;
    lcd.line_source_cb = NULL;
    lcd_finish_frame_from_isr();
    gdma_stop(lcd.dma_chan);
}

static IRAM_ATTR bool fill_bounce_buffer(uint8_t* buffer) {
    bool task_awoken = false;

    for (int i = 0; i < BOUNCE_BUF_LINES; i++) {
        // 8-bit needs a true dummy byte in FIFO; 16-bit still needs a dummy cycle but the
        // first FIFO byte is already correct (read_pico / epdiy historical behavior).
        int buffer_offset = i * (lcd.line_bytes + lcd.dummy_bytes) + (lcd.dummy_bytes % 2);
        if (lcd.line_source_cb != NULL) {
            task_awoken |= lcd.line_source_cb(lcd.line_cb_payload, &buffer[buffer_offset]);
        } else {
            memset(&buffer[buffer_offset], 0x00, lcd.line_bytes);
        }
        lcd.lines_fed += 1;
    }

#if defined(CONFIG_IDF_TARGET_ESP32S31)
    if (lcd.bb_behind_cache) {
        esp_cache_msync(buffer, lcd.bb_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    }
    // 含最后数据行的 bounce 发完后才收尾（预装 LCD_BB_COUNT 个），VSYNC 先到则由它收尾。
    // / Finish once the bounce buffer holding the last data lines went out
    // (LCD_BB_COUNT buffers are preloaded); an earlier VSYNC finishes first.
    if (!lcd.frame_finishing
        && lcd.lines_fed >= (size_t)lcd.display_lines + LCD_BB_COUNT * BOUNCE_BUF_LINES) {
        lcd_finish_frame_from_isr();
    }
#endif
    return task_awoken;
}

static void IRAM_ATTR start_ckv_cycles(int cycles) {
    rmt_compat_tx_configure_finite_loop(RMT_CKV_CHAN, cycles);
    rmt_compat_tx_reset_mem(RMT_CKV_CHAN);
    rmt_compat_tx_start(RMT_CKV_CHAN);
}

/**
 * Build the RMT signal according to the timing set in the lcd object.
 */
// RMT 的时间单位是 0.1us（通道时钟 10MHz）。config 里的 ckv_high_time 是按
// 默认 pclk 的行时间标定的，pclk 调高后行时间变短，高电平会顶满甚至超过一整行，
// 低电平算出负数——RMT 的 duration 是无符号字段，信号会彻底跑飞，面板收不到行
// 时钟，表现是画面完全不动而 MCU 侧毫无报错。所以这里给低电平留出下限，
// 让高电平跟着行时间一起收缩。
#define CKV_MIN_LOW_TIME 9

static void rebuild_line_geometry(int pclk_mhz) {
    if (pclk_mhz < 1) pclk_mhz = 1;
    const LcdLineTiming_t* t = &lcd.config.line;
    int end = t->line_end > 0 ? t->line_end : 4;
    lcd.line_cycles = lcd.lcd_res_h + t->le_high_time + t->line_front_porch + end;
    lcd.line_time_01us = (lcd.line_cycles * 10 + pclk_mhz / 2) / pclk_mhz;
    if (lcd.line_time_01us < 1) lcd.line_time_01us = 1;
    lcd.line_length_us = (lcd.line_time_01us + 9) / 10;
}

static void ckv_rmt_build_signal() {
    /* Clock/profile changes must never rewrite a running CKV item. */
    rmt_compat_tx_stop(RMT_CKV_CHAN);
    int total_time = lcd.line_time_01us;
    // 行时间还没算出来（init 阶段），等 epd_lcd_set_pixel_clock_MHz 再来构造
    if (total_time < 2 * CKV_MIN_LOW_TIME) {
        return;
    }

    int high_time = lcd.config.line.ckv_high_time;
    if (high_time > total_time - CKV_MIN_LOW_TIME) {
        high_time = total_time - CKV_MIN_LOW_TIME;
    }
    if (high_time < CKV_MIN_LOW_TIME) {
        high_time = CKV_MIN_LOW_TIME;
    }
    lcd.ckv_high_time = high_time;

    rmt_compat_write_single_item(
        RMT_CKV_CHAN, high_time, true, total_time - high_time, false, true
    );
}

/**
 * Configure the RMT peripheral for use as the CKV clock.
 */
static void init_ckv_rmt() {
    rmt_compat_reset_module();
    rmt_compat_enable_module(true);

    rmt_compat_enable_periph_clock(true);
    rmt_compat_set_group_clock_src(RMT_CKV_CHAN);
    // 通道时钟固定 10MHz（0.1us）。S3 的默认源是 80MHz；其他目标按实际源频率求分频。
    // / Channel clock is fixed at 10 MHz (0.1 us). S3's default source is 80 MHz;
    // other targets derive the divider from the actual source.
    uint32_t rmt_src_hz = 80000000;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    if (esp_clk_tree_src_get_freq_hz(
            (soc_module_clk_t)RMT_CLK_SRC_DEFAULT, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &rmt_src_hz
        )
            != ESP_OK
        || rmt_src_hz == 0) {
        rmt_src_hz = 80000000;
    }
#endif
    uint32_t div = (rmt_src_hz + 5000000) / 10000000;
    if (div < 1) div = 1;
    if (div > 255) div = 255;
    rmt_compat_set_clock_div(RMT_CKV_CHAN, (uint8_t)div);
    ESP_LOGI(TAG, "CKV RMT src=%u Hz div=%u", (unsigned)rmt_src_hz, (unsigned)div);
    rmt_compat_set_mem_blocks(RMT_CKV_CHAN, 2);
    rmt_compat_enable_mem_access_nonfifo(true);
    rmt_compat_tx_set_idle_level(RMT_CKV_CHAN, 0, true);
    rmt_compat_tx_enable_carrier(RMT_CKV_CHAN, false);
    rmt_compat_tx_enable_loop(RMT_CKV_CHAN, true);

    rmt_compat_connect_gpio(RMT_CKV_CHAN, lcd.config.bus.ckv);

    ckv_rmt_build_signal();
}

/**
 * Reset the CKV RMT configuration.
 */
static void deinit_ckv_rmt() {
    rmt_compat_reset_module();
    rmt_compat_enable_periph_clock(false);
    rmt_compat_enable_module(false);
    gpio_reset_pin(lcd.config.bus.ckv);
}

__attribute__((optimize("O3"))) IRAM_ATTR static void lcd_isr_vsync(void* args) {
    bool need_yield = false;

    uint32_t intr_status = lcd_ll_get_interrupt_status(lcd.hal.dev);
    lcd_ll_clear_interrupt_status(lcd.hal.dev, intr_status);

#ifdef LCD_LL_EVENT_UNDERRUN
    if (intr_status & LCD_LL_EVENT_UNDERRUN) {
        lcd.frame_error=3;
        lcd_finish_frame_from_isr();
        return;
    }
#endif

    if (intr_status & LCD_LL_EVENT_VSYNC_END) {
        // start_frame() already kicked off the first batch. This counter is the
        // number of *additional* full LINE_BATCH chunks still needed after that
        // first batch (integer division, not ceil). Using ceil made short panels
        // (e.g. 688 < 1000) run a duplicate second batch and roughly 2x frame time.
        int batches_needed = lcd.display_lines / LINE_BATCH;
        if (lcd.batches >= batches_needed) {
            lcd_finish_frame_from_isr();
        } else {
            int ckv_cycles = 0;
            // last batch
            if (lcd.batches == batches_needed - 1) {
                int last_lines = lcd.display_lines % LINE_BATCH;
                if (last_lines == 0) {
                    last_lines = LINE_BATCH;
                }
                lcd_ll_enable_auto_next_frame(lcd.hal.dev, false);
                lcd_ll_set_vertical_timing(lcd.hal.dev, 1, 0, last_lines, 10);
                ckv_cycles = last_lines + 10;
            } else {
                lcd_ll_set_vertical_timing(lcd.hal.dev, 1, 0, LINE_BATCH, 1);
                ckv_cycles = LINE_BATCH + 1;
            }
            // apparently, this is needed for the new timing to take effect.
            lcd_ll_start(lcd.hal.dev);

            // skip the LCD front porch line, which is not actual data
            esp_rom_delay_us(lcd.line_length_us);
            start_ckv_cycles(ckv_cycles);
        }

        lcd.batches += 1;
    }

    if (need_yield) {
        portYIELD_FROM_ISR();
    }
};

#include "lcd_dma_events.inc"

uint32_t epd_lcd_take_eof_gap_max_us(void) {
    uint32_t cycles = eof_gap_max_cycles;
    eof_gap_max_cycles = 0;
    uint32_t per_us = esp_rom_get_cpu_ticks_per_us();
    return per_us ? cycles / per_us : 0;
}
void epd_lcd_take_eof_stats(uint32_t* isr_max_us, uint32_t* catchups) {
    uint32_t per_us = esp_rom_get_cpu_ticks_per_us();
    if (isr_max_us) *isr_max_us = per_us ? eof_isr_max_cycles / per_us : 0;
    if (catchups) *catchups = eof_catchups;
    eof_isr_max_cycles = 0;
    eof_catchups = 0;
}

static esp_err_t init_dma_trans_link() {
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    const uint32_t initial_owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
#else
    const uint32_t initial_owner = DMA_DESCRIPTOR_BUFFER_OWNER_CPU;
#endif
    for (int i = 0; i < LCD_BB_COUNT; i++) {
        LCD_DMA_NODE(i)->dw0.suc_eof = 1;
        LCD_DMA_NODE(i)->dw0.size = lcd.bb_size;
        LCD_DMA_NODE(i)->dw0.length = lcd.bb_size;
        LCD_DMA_NODE(i)->dw0.owner = initial_owner;
        LCD_DMA_NODE(i)->buffer = lcd.bounce_buffer[i];
        // ring: the last node loops back to the first
        LCD_DMA_NODE(i)->next = LCD_DMA_NODE((i + 1) % LCD_BB_COUNT);
    }
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    if (lcd.bb_behind_cache) {
        esp_cache_msync(lcd.dma_nodes, LCD_BB_COUNT * sizeof(lcd.dma_nodes[0]),
                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    }
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    /* EOF refills the idle bounce buffer; one buffer is 8 lines (136 us at
       20 MHz). The default low-priority, non-IRAM interrupt lost that race on
       CPU0 under WiFi/TLS load (2026-11-20 capture: eof != expected with both
       feeders blocked on full queues). Use the highest GDMA level and keep it
       serviceable while the cache is off, as esp_lcd RGB does with
       CONFIG_LCD_RGB_ISR_IRAM_SAFE. The whole EOF path is IRAM/DRAM only. */
    gdma_channel_alloc_config_t dma_chan_config = {
        .intr_priority = 3,
        .flags.isr_cache_safe = 1,
    };
    ESP_RETURN_ON_ERROR(
        LCD_GDMA_NEW_CHANNEL(&dma_chan_config, &lcd.dma_chan, NULL), TAG, "alloc DMA channel failed"
    );
    // IDF 6.0's gdma_connect no longer implicitly resets the channel (removed).
    // Do it explicitly to match IDF 5.4 behavior.
    gdma_reset(lcd.dma_chan);
#else
    // alloc DMA channel and connect to LCD peripheral
    gdma_channel_alloc_config_t dma_chan_config = {
        .direction = GDMA_CHANNEL_DIRECTION_TX,
    };
    ESP_RETURN_ON_ERROR(
        gdma_new_channel(&dma_chan_config, &lcd.dma_chan), TAG, "alloc DMA channel failed"
    );
#endif
    gdma_trigger_t trigger = GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_LCD, 0);
    ESP_RETURN_ON_ERROR(gdma_connect(lcd.dma_chan, trigger), TAG, "dma connect error");
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    gdma_strategy_config_t dma_strategy = {
        /* Preserve the SDK RGB cyclic-bounce strategy. Enabling owner checking
           and TX writeback on this two-node S31 link failed the physical-clear
           device gate (2026-11-17); host callback tests do not model prefetch. */
        .eof_till_data_popped = false,
    };
    ESP_RETURN_ON_ERROR(gdma_apply_strategy(lcd.dma_chan, &dma_strategy), TAG, "dma strategy error");

    gdma_transfer_config_t trans_cfg = {
        .max_data_burst_size = 64,
#if defined(CONFIG_IDF_TARGET_ESP32S31)
        // bounce 在内部 SRAM；不开外存访问，避免 AXI 的 PSRAM 对齐检查。
        // / Bounce buffers are internal; no external access avoids AXI PSRAM alignment checks.
        .access_ext_mem = false,
#else
        .access_ext_mem = true,
#endif
    };
    ESP_RETURN_ON_ERROR(gdma_config_transfer(lcd.dma_chan, &trans_cfg), TAG, "dma setup error");
#else
    gdma_transfer_ability_t ability = {
        .psram_trans_align = 64,
        .sram_trans_align = 4,
    };
    ESP_RETURN_ON_ERROR(gdma_set_transfer_ability(lcd.dma_chan, &ability), TAG, "dma setup error");
#endif

    gdma_tx_event_callbacks_t cbs = {
        .on_trans_eof = lcd_rgb_panel_eof_handler,
        .on_descr_err = lcd_rgb_panel_dma_error,
    };
    ESP_RETURN_ON_ERROR(
        gdma_register_tx_event_callbacks(lcd.dma_chan, &cbs, NULL), TAG, "dma setup error"
    );

    return ESP_OK;
}

void deinit_dma_trans_link() {
    gdma_reset(lcd.dma_chan);
    gdma_disconnect(lcd.dma_chan);
    gdma_del_channel(lcd.dma_chan);
}

/**
 * Configure LCD peripheral and auxiliary GPIOs
 */
static esp_err_t init_bus_gpio() {
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    // 板级 D0..D7 = GPIO8..15，与 S31 专用 LCD_DATA0..7 IOMUX 脚一致。
    // / Board D0..D7 = GPIO8..15, matching the S31 dedicated LCD_DATA0..7 IOMUX pads.
    const soc_lcd_rgb_iomux_desc_t* iomux = &soc_lcd_rgb_iomux_descs[0];
    for (int i = 0; i < lcd.config.bus_width; i++) {
        const int want = lcd.config.bus.data[i];
        const int pad = iomux->data_pins[i].gpio_num;
        if (want != pad) {
            ESP_LOGE(TAG, "D%d gpio %d != IOMUX LCD_DATA%d pad %d", i, want, i, pad);
            return ESP_ERR_INVALID_ARG;
        }
        ESP_RETURN_ON_ERROR(gpio_iomux_output(pad, iomux->data_pins[i].func), TAG, "data iomux");
    }
#else
    const int DATA_LINES[16] = {
        lcd.config.bus.data[14], lcd.config.bus.data[15], lcd.config.bus.data[12],
        lcd.config.bus.data[13], lcd.config.bus.data[10], lcd.config.bus.data[11],
        lcd.config.bus.data[8],  lcd.config.bus.data[9],  lcd.config.bus.data[6],
        lcd.config.bus.data[7],  lcd.config.bus.data[4],  lcd.config.bus.data[5],
        lcd.config.bus.data[2],  lcd.config.bus.data[3],  lcd.config.bus.data[0],
        lcd.config.bus.data[1],
    };

    // connect peripheral signals via GPIO matrix
    for (size_t i = (16 - lcd.config.bus_width); i < 16; i++) {
        gpio_hal_func_sel(&hal, DATA_LINES[i], PIN_FUNC_GPIO);
        gpio_set_direction(DATA_LINES[i], GPIO_MODE_OUTPUT);
        esp_rom_gpio_connect_out_signal(DATA_LINES[i], LCD_PERIPH_SIG(data_sigs[i]), false, false);
    }
#endif
    gpio_hal_func_sel(&hal, lcd.config.bus.leh, PIN_FUNC_GPIO);
    gpio_set_direction(lcd.config.bus.leh, GPIO_MODE_OUTPUT);
    gpio_hal_func_sel(&hal, lcd.config.bus.clock, PIN_FUNC_GPIO);
    gpio_set_direction(lcd.config.bus.clock, GPIO_MODE_OUTPUT);
    gpio_hal_func_sel(&hal, lcd.config.bus.start_pulse, PIN_FUNC_GPIO);
    gpio_set_direction(lcd.config.bus.start_pulse, GPIO_MODE_OUTPUT);

    esp_rom_gpio_connect_out_signal(lcd.config.bus.leh, LCD_PERIPH_SIG(hsync_sig), false, false);
    esp_rom_gpio_connect_out_signal(lcd.config.bus.clock, LCD_PERIPH_SIG(pclk_sig), false, false);
    esp_rom_gpio_connect_out_signal(
        lcd.config.bus.start_pulse, LCD_PERIPH_SIG(de_sig), false, false
    );

    gpio_config_t vsync_gpio_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ull << lcd.config.bus.stv,
    };
    gpio_config(&vsync_gpio_conf);
    gpio_set_level(lcd.config.bus.stv, 1);
    return ESP_OK;
}

/**
 * Reset bus GPIO pin functions.
 */
static void deinit_bus_gpio() {
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    for (int i = 0; i < lcd.config.bus_width; i++) {
        gpio_reset_pin(lcd.config.bus.data[i]);
    }
#else
    for (size_t i = (16 - lcd.config.bus_width); i < 16; i++) {
        gpio_reset_pin(lcd.config.bus.data[i]);
    }
#endif

    gpio_reset_pin(lcd.config.bus.leh);
    gpio_reset_pin(lcd.config.bus.clock);
    gpio_reset_pin(lcd.config.bus.start_pulse);
    gpio_reset_pin(lcd.config.bus.stv);
}

/**
 * Check if the PSRAM cache is properly configured.
 */
static void check_cache_configuration() {
    if (EPDIY_DATA_CACHE_LINE_SIZE < 64) {
        ESP_LOGE(
            "epdiy",
            "cache line size is set to %d (< 64B)! This will degrade performance, please update "
            "this option in menuconfig.",
            EPDIY_DATA_CACHE_LINE_SIZE
        );
        ESP_LOGE(
            "epdiy",
            "If you are on arduino, you can't set this option yourself, you'll need to use a lower "
            "speed."
        );
        ESP_LOGE(
            "epdiy",
            "Reducing the pixel clock from %d MHz to %d MHz for now!",
            lcd.config.pixel_clock / 1000 / 1000,
            lcd.config.pixel_clock / 1000 / 1000 / 2
        );
        lcd.config.pixel_clock = lcd.config.pixel_clock / 2;

        // fixme: this would be nice, but doesn't work :(
        // uint32_t d_autoload = Cache_Suspend_DCache();
        /// Cache_Set_DCache_Mode(CACHE_SIZE_FULL, CACHE_4WAYS_ASSOC, CACHE_LINE_SIZE_32B);
        // Cache_Invalidate_DCache_All();
        // Cache_Resume_DCache(d_autoload);
    }
}

/**
 * Assign LCD configuration parameters from a given configuration, without allocating memory or
 * touching the LCD peripheral config.
 */
static void assign_lcd_parameters_from_config(
    const LcdEpdConfig_t* config, int display_width, int display_height
) {
    // copy over the configuraiton object
    memcpy(&lcd.config, config, sizeof(LcdEpdConfig_t));

    // Make sure the bounce buffers divide the display height evenly.
    lcd.display_lines = (((display_height + 7) / 8) * 8);

    lcd.line_bytes = display_width / 4;
    lcd.lcd_res_h = lcd.line_bytes / (lcd.config.bus_width / 8);

    // With 8 bit bus width, we need a dummy cycle before the actual data,
    // because the LCD peripheral behaves weirdly.
    // Also see:
    // https://blog.adafruit.com/2022/06/14/esp32uesday-hacking-the-esp32-s3-lcd-peripheral/
    lcd.dummy_bytes = lcd.config.bus_width / 8;

    // each bounce buffer holds a number of lines with data + dummy bytes each
    lcd.bb_size = BOUNCE_BUF_LINES * (lcd.line_bytes + lcd.dummy_bytes);

    check_cache_configuration();

    ESP_LOGI(TAG, "using resolution %dx%d", lcd.lcd_res_h, lcd.display_lines);
}

/**
 * Allocate buffers for LCD driver operation.
 */
static esp_err_t allocate_lcd_buffers() {
    uint32_t dma_flags = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;

    lcd.bb_behind_cache = false;
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    // 与 esp_lcd RGB 一致：S31 内部 SRAM 也可能经 cache。/ As esp_lcd RGB: S31 internal SRAM may be cached.
    lcd.bb_behind_cache = true;
#endif
    // allocate bounce buffers
    for (int i = 0; i < LCD_BB_COUNT; i++) {
        lcd.bounce_buffer[i] = heap_caps_aligned_calloc(LCD_BB_ALIGN, 1, lcd.bb_size, dma_flags);
        ESP_RETURN_ON_FALSE(lcd.bounce_buffer[i], ESP_ERR_NO_MEM, TAG, "install interrupt failed");
#if defined(CONFIG_IDF_TARGET_ESP32S31)
        esp_cache_msync(
            lcd.bounce_buffer[i], lcd.bb_size,
            ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED
        );
#endif
    }

    // So far, I haven't seen any displays with > 4096 pixels per line,
    // so we only need one DMA node for now.
    assert(lcd.bb_size < DMA_DESCRIPTOR_BUFFER_MAX_SIZE);
    lcd.dma_nodes = heap_caps_aligned_calloc(LCD_GDMA_DESC_ALIGN, LCD_BB_COUNT, sizeof(lcd_dma_node_t), dma_flags);
    ESP_RETURN_ON_FALSE(lcd.dma_nodes, ESP_ERR_NO_MEM, TAG, "no mem for dma nodes");
    return ESP_OK;
}

static void free_lcd_buffers() {
    for (int i = 0; i < LCD_BB_COUNT; i++) {
        uint8_t* buf = lcd.bounce_buffer[i];
        if (buf != NULL) {
            heap_caps_free(buf);
            lcd.bounce_buffer[i] = NULL;
        }
    }

    if (lcd.dma_nodes != NULL) {
        heap_caps_free(lcd.dma_nodes);
        lcd.dma_nodes = NULL;
    }
}

/**
 * Initialize the LCD peripheral itself and install interrupts.
 */
static esp_err_t init_lcd_peripheral() {
    esp_err_t ret = ESP_OK;

    // enable APB to access LCD registers
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    PERIPH_RCC_ACQUIRE_ATOMIC(soc_lcd_rgb_signals[0].module, ref_count) {
        if (ref_count == 0) {
            lcd_ll_enable_bus_clock(0, true);
            lcd_ll_reset_register(0);
        }
    }
#else
    periph_module_enable(PERIPH_LCD_CAM_MODULE);
    periph_module_reset(PERIPH_LCD_CAM_MODULE);
#endif

    lcd_hal_init(&lcd.hal, 0);
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    PERIPH_RCC_ATOMIC() {
        lcd_ll_enable_clock(lcd.hal.dev, true);
    }
    // 打开 RGB 传输缓冲防欠载（与 esp_lcd RGB 相同）。/ Enable the RGB trans buffer against underrun (as esp_lcd RGB).
    lcd_ll_mem_set_low_power_mode(lcd.hal.dev, LCD_LL_MEM_LP_MODE_SHUT_DOWN);
    lcd_ll_mem_power_by_pmu(lcd.hal.dev);
    lcd_ll_enable_trans_buffer(lcd.hal.dev, true);
    ESP_RETURN_ON_ERROR(
        esp_clk_tree_enable_src((soc_module_clk_t)LCD_CLK_SRC_DEFAULT, true),
        TAG,
        "enable lcd clk src failed"
    );
    if (esp_clk_tree_src_get_freq_hz(
            (soc_module_clk_t)LCD_CLK_SRC_DEFAULT, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,
            &lcd.src_clk_hz
        )
            != ESP_OK
        || lcd.src_clk_hz == 0) {
        lcd.src_clk_hz = 160000000;
    }
    PERIPH_RCC_ATOMIC() {
        lcd_ll_select_clk_src(lcd.hal.dev, LCD_CLK_SRC_DEFAULT);
    }
#elif ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    // IDF 6 requires explicit clock-tree enable; otherwise PCLK can fall back to a slow source.
    ESP_RETURN_ON_ERROR(
        esp_clk_tree_enable_src((soc_module_clk_t)LCD_CLK_SRC_PLL240M, true),
        TAG,
        "enable lcd clk src failed"
    );
    ESP_RETURN_ON_ERROR(
        esp_clk_tree_src_get_freq_hz(
            (soc_module_clk_t)LCD_CLK_SRC_PLL240M,
            ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,
            &lcd.src_clk_hz
        ),
        TAG,
        "get lcd clk freq failed"
    );
    PERIPH_RCC_ATOMIC() {
        lcd_ll_enable_clock(lcd.hal.dev, true);
        lcd_ll_select_clk_src(lcd.hal.dev, LCD_CLK_SRC_PLL240M);
    }
#else
    lcd.src_clk_hz = 240000000;
    lcd_ll_enable_clock(lcd.hal.dev, true);
    lcd_ll_select_clk_src(lcd.hal.dev, LCD_CLK_SRC_PLL240M);
#endif
    ESP_LOGI(TAG, "lcd src clk: %u Hz", (unsigned)lcd.src_clk_hz);
    ESP_RETURN_ON_ERROR(ret, TAG, "set source clock failed");

    lcd_ll_fifo_reset(lcd.hal.dev);
    lcd_ll_reset(lcd.hal.dev);
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    // 复位可能清掉传输缓冲使能，再开一次。/ Reset may clear the trans buffer enable; set it again.
    lcd_ll_enable_trans_buffer(lcd.hal.dev, true);
#endif

    // install interrupt service, (LCD peripheral shares the interrupt source with Camera by
    // different mask)
    int flags = ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_INTRDISABLED | ESP_INTR_FLAG_SHARED
                | ESP_INTR_FLAG_LOWMED;

    int source = LCD_PERIPH_SIG(irq_id);
    uint32_t status = (uint32_t)lcd_ll_get_interrupt_status_reg(lcd.hal.dev);
    ret = esp_intr_alloc_intrstatus(
        source, flags, status, LCD_LL_EVENT_VSYNC_END, lcd_isr_vsync, NULL, &lcd.vsync_intr
    );
    ESP_RETURN_ON_ERROR(ret, TAG, "install interrupt failed");

    status = (uint32_t)lcd_ll_get_interrupt_status_reg(lcd.hal.dev);
    ret = esp_intr_alloc_intrstatus(
        source, flags, status, LCD_LL_EVENT_TRANS_DONE
#ifdef LCD_LL_EVENT_UNDERRUN
            | LCD_LL_EVENT_UNDERRUN
#endif
        , lcd_isr_vsync, NULL, &lcd.done_intr
    );
    ESP_RETURN_ON_ERROR(ret, TAG, "install interrupt failed");

    // pixel clock phase and polarity
    lcd_ll_set_clock_idle_level(lcd.hal.dev, false);
    lcd_ll_set_pixel_clock_edge(lcd.hal.dev, false);

    // enable RGB mode and set data width
    lcd_ll_enable_rgb_mode(lcd.hal.dev, true);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    lcd_ll_set_dma_read_stride(lcd.hal.dev, lcd.config.bus_width);
    lcd_ll_set_data_wire_width(lcd.hal.dev, lcd.config.bus_width);
#else
    lcd_ll_set_data_width(lcd.hal.dev, lcd.config.bus_width);
#endif
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    // 示波器确认 S31 每字节 1 拍：只开数据相位，行长由 H 时序截取。
    // / Scope-confirmed 1 clock per byte on S31: data phase only, line length from H timing.
    lcd_ll_set_phase_cycles(lcd.hal.dev, 0, 0, 1);
    lcd_ll_reverse_wire_bit_order(lcd.hal.dev, false);
#else
    lcd_ll_set_phase_cycles(lcd.hal.dev, 0, (lcd.dummy_bytes > 0), 1);  // enable data phase only
#endif

    // number of data cycles is controlled by DMA buffer size
    lcd_ll_enable_output_always_on(lcd.hal.dev, true);
    lcd_ll_set_idle_level(lcd.hal.dev, false, true, true);

    // configure blank region timing
    // RGB panel always has a front and back blank (porch region)
    lcd_ll_set_blank_cycles(lcd.hal.dev, 1, 1);

    // output hsync even in porch region?
    lcd_ll_enable_output_hsync_in_porch_region(lcd.hal.dev, false);
    // send next frame automatically in stream mode
    lcd_ll_enable_auto_next_frame(lcd.hal.dev, false);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
    PERIPH_RCC_ATOMIC() {
        lcd_ll_enable_interrupt(lcd.hal.dev, LCD_LL_EVENT_VSYNC_END, true);
        lcd_ll_enable_interrupt(lcd.hal.dev, LCD_LL_EVENT_TRANS_DONE, true);
    }
#else
    lcd_ll_enable_interrupt(lcd.hal.dev, LCD_LL_EVENT_VSYNC_END, true);
    lcd_ll_enable_interrupt(lcd.hal.dev, LCD_LL_EVENT_TRANS_DONE, true);
#endif

#ifdef LCD_LL_EVENT_UNDERRUN
    PERIPH_RCC_ATOMIC() {
        lcd_ll_enable_interrupt(lcd.hal.dev,LCD_LL_EVENT_UNDERRUN,true);
    }
#endif

    // clear any stale interrupt events before enabling the ISR
    lcd_ll_clear_interrupt_status(lcd.hal.dev, UINT32_MAX);
    // enable intr
    esp_intr_enable(lcd.vsync_intr);
    esp_intr_enable(lcd.done_intr);
    return ret;
}

static void deinit_lcd_peripheral() {
    // disable and free interrupts
    esp_intr_disable(lcd.vsync_intr);
    esp_intr_disable(lcd.done_intr);
    esp_intr_free(lcd.vsync_intr);
    esp_intr_free(lcd.done_intr);

    lcd_ll_stop(lcd.hal.dev);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    PERIPH_RCC_ATOMIC() {
        lcd_ll_enable_clock(lcd.hal.dev, false);
    }
#else
    lcd_ll_enable_clock(lcd.hal.dev, false);
#endif

    lcd_ll_fifo_reset(lcd.hal.dev);
    lcd_ll_reset(lcd.hal.dev);
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    lcd_ll_enable_trans_buffer(lcd.hal.dev, false);
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    PERIPH_RCC_RELEASE_ATOMIC(soc_lcd_rgb_signals[0].module, ref_count) {
        if (ref_count == 0) {
            lcd_ll_enable_bus_clock(0, false);
        }
    }
#else
    periph_module_reset(PERIPH_LCD_CAM_MODULE);
    periph_module_disable(PERIPH_LCD_CAM_MODULE);
#endif
}

/**
 * Configure the LCD driver for epdiy.
 */
void epd_lcd_init(const LcdEpdConfig_t* config, int display_width, int display_height) {
    esp_err_t ret = ESP_OK;
    assign_lcd_parameters_from_config(config, display_width, display_height);

    ret = allocate_lcd_buffers();
    ESP_GOTO_ON_ERROR(ret, err, TAG, "lcd buffer allocation failed");

    ret = init_lcd_peripheral();
    ESP_GOTO_ON_ERROR(ret, err, TAG, "lcd peripheral init failed");

    ret = init_dma_trans_link();
    ESP_GOTO_ON_ERROR(ret, err, TAG, "install DMA failed");

    ret = init_bus_gpio();
    ESP_GOTO_ON_ERROR(ret, err, TAG, "configure GPIO failed");

    init_ckv_rmt();
    ret=init_ckv_counter();
    ESP_GOTO_ON_ERROR(ret,err,TAG,"CKV monitor init failed");

    // setup driver state
    epd_lcd_set_pixel_clock_MHz(lcd.config.pixel_clock / 1000 / 1000);
    epd_lcd_line_source_cb(NULL, NULL);

    ESP_LOGI(TAG, "LCD init done.");
    return;
err:
    ESP_LOGE(TAG, "LCD initialization failed!");
    abort();
}

/**
 * Deinitializue the LCD driver, i.e., free resources and peripherals.
 */
void epd_lcd_deinit() {
    epd_lcd_line_source_cb(NULL, NULL);
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    if(ckv_counter) {
        pcnt_unit_stop(ckv_counter);pcnt_unit_disable(ckv_counter);
        pcnt_del_channel(ckv_counter_channel);pcnt_del_unit(ckv_counter);
        ckv_counter=NULL;ckv_counter_channel=NULL;
    }
#endif

    deinit_bus_gpio();
    deinit_lcd_peripheral();
    deinit_dma_trans_link();
    free_lcd_buffers();
    deinit_ckv_rmt();

    ESP_LOGI(TAG, "LCD deinitialized.");
}

void epd_lcd_set_line_timing(const LcdLineTiming_t* timing) {
    if (timing == NULL) return;
    lcd.config.line = *timing;
}

void epd_lcd_set_pixel_clock_MHz(int frequency) {
    lcd.config.pixel_clock = frequency * 1000 * 1000;
    if (lcd.src_clk_hz == 0) {
        lcd.src_clk_hz = 240000000;
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    hal_utils_clk_div_t clk_div = {};
/**
 * There was a change in the parameters of this function in this commit:
 * https://github.com/espressif/esp-idf/commit/d39388fe4f4c5bfb0b52df9177307b1688f41016#diff-2df607d77e3f6e350bab8eb31cfd914500ae42744564e1640cec47006cc17a9c
 * There are different builds with the same IDF minor version, some with, some without the commit.
 * So we try to select the correct one by checking if the flag value is defined.
 */
#ifdef LCD_HAL_PCLK_FLAG_ALLOW_EQUAL_SYSCLK
    uint32_t freq
        = lcd_hal_cal_pclk_freq(&lcd.hal, lcd.src_clk_hz, lcd.config.pixel_clock, 0, &clk_div);
#else
    uint32_t freq
        = lcd_hal_cal_pclk_freq(&lcd.hal, lcd.src_clk_hz, lcd.config.pixel_clock, &clk_div);
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    PERIPH_RCC_ATOMIC() {
        lcd_ll_set_group_clock_coeff(
            lcd.hal.dev, (int)clk_div.integer, (int)clk_div.denominator, (int)clk_div.numerator
        );
    }
#else
    lcd_ll_set_group_clock_coeff(
        &LCD_CAM, (int)clk_div.integer, (int)clk_div.denominator, (int)clk_div.numerator
    );
#endif
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    // HAL 只写 HP 系统分频；LCD 自身的时钟门默认关闭、二级分频默认 4。
    // 按 Metalio 实测打开时钟门、选 160MHz 源、二级分频 1。
    // / The HAL only writes the HP system divider; the LCD's own clock gate is off
    // and its second divider defaults to 4. Per Metalio bench values: gate on,
    // 160 MHz source, second divider 1.
    lcd.hal.dev->lcd_clock.clk_en = 1;
    lcd.hal.dev->lcd_clock.lcd_clk_sel = 2;
    lcd.hal.dev->lcd_clock.lcd_clkm_div_num = 1;
    lcd.hal.dev->lcd_clock.lcd_clkm_div_a = 0;
    lcd.hal.dev->lcd_clock.lcd_clkm_div_b = 0;
#endif
#else
    uint32_t freq = lcd_hal_cal_pclk_freq(&lcd.hal, lcd.src_clk_hz, lcd.config.pixel_clock, 0);
#endif

    ESP_LOGI(TAG, "pclk freq: %u Hz (src %u Hz)", (unsigned)freq, (unsigned)lcd.src_clk_hz);
    rebuild_line_geometry(frequency);
    ckv_rmt_build_signal();
    ESP_LOGI(
        TAG, "line width: %d.%dus, %d cycles, ckv high %d.%dus", lcd.line_time_01us / 10,
        lcd.line_time_01us % 10, lcd.line_cycles, lcd.ckv_high_time / 10, lcd.ckv_high_time % 10
    );
}

void IRAM_ATTR epd_lcd_start_frame() {
    int initial_lines = min(LINE_BATCH, lcd.display_lines);

    // hsync: pulse width, back porch, active width, front porch
    const LcdLineTiming_t* t = &lcd.config.line;
    int end_line = lcd.line_cycles - lcd.lcd_res_h - t->le_high_time - t->line_front_porch;
    lcd_ll_set_horizontal_timing(
        lcd.hal.dev,
        t->le_high_time - (lcd.dummy_bytes > 0),
        t->line_front_porch,
        // a dummy byte is neeed in 8 bit mode to work around LCD peculiarities
        lcd.lcd_res_h + (lcd.dummy_bytes > 0),
        end_line
    );
    lcd_ll_set_vertical_timing(lcd.hal.dev, 1, 0, initial_lines, 1);

    // generate the hsync at the very beginning of line
    lcd_ll_set_hsync_position(lcd.hal.dev, 1);

    // reset FIFO of DMA and LCD, incase there remains old frame data
    gdma_reset(lcd.dma_chan);
    lcd_ll_stop(lcd.hal.dev);
#if !defined(CONFIG_IDF_TARGET_ESP32S31)
    // S31 的 lcd_ll_reset 会清传输缓冲使能，只复位 FIFO。/ On S31 lcd_ll_reset clears the trans buffer; reset the FIFO only.
    lcd_ll_reset(lcd.hal.dev);
#endif
    lcd_ll_fifo_reset(lcd.hal.dev);
    lcd_ll_enable_auto_next_frame(lcd.hal.dev, true);

    lcd.batches = 0;
    lcd.bb_eof_count = 0;
    eof_prev_cycles = 0;
    lcd.lines_fed = 0;
    lcd.frame_error = 0;
    lcd.frame_finishing = false;
    rmt_compat_tx_stop(RMT_CKV_CHAN);
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    if(pcnt_unit_clear_count(ckv_counter)!=ESP_OK) {
        lcd.frame_error=4;lcd_finish_frame_from_isr();return;
    }
#endif
    for (int i = 0; i < LCD_BB_COUNT; i++) fill_bounce_buffer(lcd.bounce_buffer[i]);
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    for (int i = 0; i < LCD_BB_COUNT; i++) LCD_DMA_NODE(i)->dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
    if (lcd.bb_behind_cache) {
        esp_cache_msync(lcd.dma_nodes, LCD_BB_COUNT * sizeof(lcd.dma_nodes[0]),
                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    }
#endif

    // the start of DMA should be prior to the start of LCD engine
    gdma_start(lcd.dma_chan, (intptr_t)LCD_DMA_NODE(0));

    // enter a critical section to ensure the frame start timing is correct
    taskENTER_CRITICAL(&frame_start_spinlock);

    // delay 1us is sufficient for DMA to pass data to LCD FIFO
    // in fact, this is only needed when LCD pixel clock is set too high
    gpio_set_level(lcd.config.bus.stv, 0);
    // esp_rom_delay_us(1);
    //  for picture clarity, it seems to be important to start CKV at a "good"
    //  time, seemingly start or towards end of line.
    start_ckv_cycles(initial_lines + 5);
    esp_rom_delay_us(lcd.line_length_us);
    gpio_set_level(lcd.config.bus.stv, 1);
    esp_rom_delay_us(lcd.line_length_us);
    esp_rom_delay_us(lcd.ckv_high_time / 10);

    // start LCD engine
    lcd_ll_start(lcd.hal.dev);

    taskEXIT_CRITICAL(&frame_start_spinlock);
}

#else

/// Dummy implementation to link on the old ESP32
void epd_lcd_init(const LcdEpdConfig_t* config, int display_width, int display_height) {
    assert(false);
}

#endif  // S3 Target
