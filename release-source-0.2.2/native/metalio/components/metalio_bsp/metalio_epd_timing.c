/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 按像素钟解出行/帧扫描参数。算法沿用 Read Pico read_pico_epd_timing.c（Apache-2.0，mindreset），
 * 换成 Metalio 的 8 位总线、像素钟区间与帧周期。
 *
 * Solve line/frame scan parameters from the pixel clock. The method follows
 * Read Pico read_pico_epd_timing.c (Apache-2.0, mindreset) with Metalio's 8-bit
 * bus, pixel-clock range and frame period.
 */
#include "metalio_bsp.h"

#include "epdiy.h"
#include "esp_log.h"

#define TAG "metalio_epd"

// 规格给的消隐下界：行起始 ≥0.3µs、行后肩 ≥0.8µs；E0470A14-AF 规格书 6.3.1 要求有效数据后
// 至少 10 个额外 XCL 时钟。
// / Spec blanking floors: line start ≥0.3 µs, back porch ≥0.8 µs; E0470A14-AF
// datasheet 6.3.1 needs at least 10 extra XCL clocks after the active data.
#define LINE_START_MIN_NS 300
#define LINE_BACK_MIN_NS 800
#define LINE_END_MIN_CLK 10

#define FRAME_START_LINES 1
#define FRAME_BACK_LINES 4
#define FRAME_END_LINES 12

// 栅极开门必须严格短于一行，低电平至少留 1.2µs（E0470A14-AF 高/低各 ≥1µs）。
// / Gate open stays shorter than a line; ≥1.2 µs low (E0470A14-AF: ≥1 µs each).
#define CKV_LOW_MIN_01US 12
#define CKV_HIGH_MAX_01US 134

// 厂商固件在本机上把每相垫到至少 12ms（TB8 波形相太短时 DU/GL 残影加重）。
// / The vendor firmware pads each phase to at least 12 ms on this board (short TB8
// phases leave more DU/GL ghosting).
#define METALIO_FRAME_TARGET_US 12000

typedef struct {
    int pclk_mhz;
    int line_start;
    int line_back_porch;
    int line_data;
    int line_end;
    int line_us;
    int frame_us;
    int ckv_high_01us;
} scan_t;

static int s_pclk_mhz;
static metalio_epd_scan_profile_t s_profile = METALIO_EPD_SCAN_FULL;

static int clampi(int v, int lo, int hi) {
    return v > hi ? hi : v < lo ? lo : v;
}

static int ceil_div(int n, int d) {
    return (n + d - 1) / d;
}

static int clocks_for_ns(int ns, int pclk_mhz) {
    return ceil_div(ns * pclk_mhz, 1000);
}

static void solve(int pclk_mhz, metalio_epd_scan_profile_t profile, int width, int height,
                  int bus_width, scan_t* out) {
    pclk_mhz = clampi(pclk_mhz, METALIO_EPD_PCLK_MIN_MHZ, METALIO_EPD_PCLK_MAX_MHZ);
    const int p = bus_width / 2;
    const int ldl = width / p;
    const int vall = FRAME_START_LINES + FRAME_BACK_LINES + height + FRAME_END_LINES;
    const int lsl = clocks_for_ns(LINE_START_MIN_NS, pclk_mhz);
    const int lbl = clocks_for_ns(LINE_BACK_MIN_NS, pclk_mhz);

    int line_us = ceil_div(ldl + lsl + lbl + LINE_END_MIN_CLK, pclk_mhz);
    if (profile == METALIO_EPD_SCAN_FULL) {
        const int target_line_us = (METALIO_FRAME_TARGET_US + vall / 2) / vall;
        if (target_line_us > line_us) line_us = target_line_us;
    }
    int line_clocks = line_us * pclk_mhz;
    int lel = line_clocks - ldl - lsl - lbl;
    if (lel < LINE_END_MIN_CLK) {
        line_us++;
        line_clocks = line_us * pclk_mhz;
        lel = line_clocks - ldl - lsl - lbl;
    }
    out->pclk_mhz = pclk_mhz;
    out->line_start = lsl;
    out->line_back_porch = lbl;
    out->line_data = ldl;
    out->line_end = lel;
    out->line_us = line_us;
    out->frame_us = line_us * vall;
    const int data_end_01us = ceil_div((lsl + lbl + ldl) * 10, pclk_mhz);
    int ckv = CKV_HIGH_MAX_01US;
    if (ckv < data_end_01us) ckv = data_end_01us;
    out->ckv_high_01us = clampi(ckv, CKV_LOW_MIN_01US, line_us * 10 - CKV_LOW_MIN_01US);
}

static LcdLineTiming_t to_line(const scan_t* s) {
    return (LcdLineTiming_t){
        .le_high_time = s->line_start,
        .line_front_porch = s->line_back_porch,
        .line_end = s->line_end,
        .ckv_high_time = s->ckv_high_01us,
    };
}

static void apply(int pclk_mhz, metalio_epd_scan_profile_t profile) {
    const EpdDisplay_t* d = epd_get_display();
    scan_t scan;
    solve(pclk_mhz, profile, d->width, d->height, d->bus_width, &scan);
    LcdLineTiming_t line = to_line(&scan);
    epd_lcd_set_line_timing(&line);
    epd_set_lcd_pixel_clock_MHz(scan.pclk_mhz);
    // FULL 档行长有余量，32 行预填足够；FAST 档留 64 行。/ FULL has slack for 32 prefill lines; FAST keeps 64.
    epd_lcd_set_prefill_lines(profile == METALIO_EPD_SCAN_FULL ? 127 : 64);
    s_pclk_mhz = scan.pclk_mhz;
    s_profile = profile;
    ESP_LOGI(TAG, "scan %s %d MHz: line %dus (%d+%d+%d+%d clk), frame %dus, ckv %d.%dus",
             profile == METALIO_EPD_SCAN_FULL ? "full" : "fast", scan.pclk_mhz, scan.line_us,
             scan.line_start, scan.line_back_porch, scan.line_data, scan.line_end, scan.frame_us,
             scan.ckv_high_01us / 10, scan.ckv_high_01us % 10);
}

void metalio_epd_init_lcd(const lcd_bus_config_t* bus, int bus_width, int width, int height,
                          int pclk_mhz) {
    if (bus == NULL) return;
    scan_t scan;
    solve(pclk_mhz, METALIO_EPD_SCAN_FULL, width, height, bus_width, &scan);
    LcdEpdConfig_t config = {
        .pixel_clock = (size_t)scan.pclk_mhz * 1000 * 1000,
        .line = to_line(&scan),
        .bus_width = bus_width,
        .bus = *bus,
    };
    epd_lcd_init(&config, width, height);
    s_pclk_mhz = scan.pclk_mhz;
    s_profile = METALIO_EPD_SCAN_FULL;
}

void metalio_epd_set_pclk(int pclk_mhz) {
    apply(pclk_mhz, s_profile);
}

int metalio_epd_pclk_mhz(void) {
    return s_pclk_mhz;
}

void metalio_epd_use_scan(metalio_epd_scan_profile_t profile) {
    if (profile == s_profile) return;
    apply(s_pclk_mhz, profile);
}
