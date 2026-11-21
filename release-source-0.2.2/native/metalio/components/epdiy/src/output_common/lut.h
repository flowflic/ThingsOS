#pragma once

#include <stdint.h>
#include "epdiy.h"

// Make a block of 4 pixels lighter on the EPD.
#define CLEAR_BYTE 0B10101010
// Make a block of 4 pixels darker on the EPD.
#define DARK_BYTE 0B01010101

/**
 * Type signature of a framebuffer to display output lookup function.
 */
typedef void (*lut_func_t)(
    const uint32_t* line_buffer, uint8_t* epd_input, const uint8_t* lut, uint32_t epd_width
);

/**
 * Type signature of a LUT preparation function.
 */
typedef void (*lut_build_func_t)(uint8_t* lut, const EpdWaveformPhases* phases, int frame);

typedef struct {
    lut_build_func_t build_func;
    lut_func_t lookup_func;
    /// True when lookup_func writes the four pixels of an output byte in panel
    /// order, so the line feeder must not reverse them afterwards.
    bool output_panel_order;
} LutFunctionPair;

/**
 * Select the appropriate LUT building and lookup function
 * for the selected draw mode and allocated LUT size.
 */
LutFunctionPair find_lut_functions(enum EpdDrawMode mode, uint32_t lut_size);

/// 1ppB 1 KiB 表。index = (to << 4) | from，布局随目标而定：
/// ESP32-S3：256 项 × 4 字节（lut.S 用）；ESP32-S31：4 页 × 256 字节，
/// 每页把相位预移位到对应的输出位对（见 lut_1ppb_pages.h），构建函数与
/// find_lut_functions() 的选择必须成对使用。
/// / 1ppB 1 KiB table, index (to << 4) | from. Layout is target-specific:
/// 256 x 4-byte entries for the S3 vector lookup, four pre-shifted 256-byte
/// pages on the S31. The builder and the lookup chosen by find_lut_functions()
/// must always be paired.
#define EPD_1PPB_LUT_1K_SIZE 1024
void epd_build_1ppB_lut_1k(uint8_t* lut, const EpdWaveformPhases* phases, int frame);

/**
 * Apply a mask to a line buffer.
 * `len` must be divisible by 4.
 */
void epd_apply_line_mask(uint8_t* buf, const uint8_t* mask, int len);

/// 把每个输出字节内的 4 个 2 位像素倒序（仅 EPD_OUTPUT_REVERSE_PIXELS 目标）。
/// 2ppB/8ppB 等查表仍按 S3 顺序（像素 0 在 bit1:0）产出，最后在这里换成面板顺序；
/// S31 的 1ppB 页面布局自己就输出面板顺序，喂线器不再调用本函数。
/// / Reverse the four 2-bit pixels in each output byte (EPD_OUTPUT_REVERSE_PIXELS
/// targets only). The 2ppB/8ppB lookups still produce S3 order (pixel 0 in bits
/// 1:0) and are converted here; the S31 1ppB page layout is already in panel
/// order, so the line feeder skips this call for it.
void epd_output_reverse_pixels(uint8_t* buf, int len);

// legacy functions
void bit_shift_buffer_right(uint8_t* buf, uint32_t len, int shift);
void nibble_shift_buffer_right(uint8_t* buf, uint32_t len);
