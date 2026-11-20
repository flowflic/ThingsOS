/* SPDX-License-Identifier: LGPL-3.0-or-later
 * 1ppB difference-image lookup in the Metalio / ESP32-S31 byte-page layout.
 *
 * The 1 KiB table is four 256-byte pages. Page p holds the 2-bit wave phase of
 * every (to << 4) | from index, pre-shifted to output bits (2p+1):2p:
 *
 *   page 0   phase            page 2   phase << 4
 *   page 1   phase << 2       page 3   phase << 6
 *
 * The first byte of a four-pixel group is looked up in page 3, the last in
 * page 0, so one OR of four byte loads yields the byte the panel wants: it
 * shifts the high 2 bits of a byte first, which is why the S3-order output
 * needed epd_output_reverse_pixels() before. Folding the page choice into the
 * table removes both that pass and the per-pixel index scaling.
 *
 * This is plain C on purpose: lut.c builds the target function from it and
 * tests/lut_1ppb_test.c compares it against a byte-at-a-time reference.
 * / 纯 C 实现：lut.c 用它生成目标函数，主机测试逐字节比对参考实现。
 */
#pragma once

#include <stdint.h>

#define EPD_1PPB_PAGE_SIZE 256
#define EPD_1PPB_PAGE_COUNT 4
#define EPD_1PPB_LUT_BYTES (EPD_1PPB_PAGE_SIZE * EPD_1PPB_PAGE_COUNT)

#if defined(__GNUC__)
#define EPD_1PPB_INLINE static inline __attribute__((always_inline))
#else
#define EPD_1PPB_INLINE static inline
#endif

/**
 * Unpack one 64-byte waveform block (16 `to` x 16 `from`, two bits each) into
 * the four pages. `waveform` points at the whole packed table, `frame` selects
 * the block, exactly as EpdWaveformPhases.luts does.
 */
EPD_1PPB_INLINE void epd_1ppb_build_pages(uint8_t* table, const uint8_t* waveform, int frame) {
    const uint8_t* packed_lut = waveform + (16 * 4 * frame);
    for (unsigned to = 0; to < 16; ++to) {
        for (unsigned from_packed = 0; from_packed < 4; ++from_packed) {
            const uint8_t packed = *packed_lut++;
            for (unsigned k = 0; k < 4; ++k) {
                const unsigned phase = (packed >> (6 - 2 * k)) & 3u;
                const unsigned index = (to << 4) | (from_packed * 4 + k);
                table[index] = (uint8_t)phase;
                table[EPD_1PPB_PAGE_SIZE + index] = (uint8_t)(phase << 2);
                table[2 * EPD_1PPB_PAGE_SIZE + index] = (uint8_t)(phase << 4);
                table[3 * EPD_1PPB_PAGE_SIZE + index] = (uint8_t)(phase << 6);
            }
        }
    }
}

/** One output byte: the four pixels of a group, first pixel in the high bits. */
EPD_1PPB_INLINE uint8_t epd_1ppb_byte(const uint8_t* pixels, const uint8_t* table) {
    return (uint8_t)(table[3 * EPD_1PPB_PAGE_SIZE + pixels[0]]
                     | table[2 * EPD_1PPB_PAGE_SIZE + pixels[1]]
                     | table[EPD_1PPB_PAGE_SIZE + pixels[2]] | table[pixels[3]]);
}

/** Convert one line of `width` pixels (1 byte per pixel) into `width / 4` bytes. */
EPD_1PPB_INLINE void
epd_1ppb_convert(const uint32_t* in, uint8_t* out, const uint8_t* table, uint32_t width) {
    const uint32_t bytes = width >> 2;
    uint32_t j = 0;
    /* Four output bytes per iteration (16 pixels): the independent load groups
       give the in-order core something to overlap, and the loop overhead is
       paid once per 16 pixels instead of once per 4. */
    for (; j + 4 <= bytes; j += 4) {
        out[j] = epd_1ppb_byte((const uint8_t*)(in + j), table);
        out[j + 1] = epd_1ppb_byte((const uint8_t*)(in + j + 1), table);
        out[j + 2] = epd_1ppb_byte((const uint8_t*)(in + j + 2), table);
        out[j + 3] = epd_1ppb_byte((const uint8_t*)(in + j + 3), table);
    }
    for (; j < bytes; ++j) {
        out[j] = epd_1ppb_byte((const uint8_t*)(in + j), table);
    }
}
