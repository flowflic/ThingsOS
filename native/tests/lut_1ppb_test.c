/* SPDX-License-Identifier: Apache-2.0
 * Host check for the Metalio 1ppB page lookup (lut_1ppb_pages.h).
 *
 * The feeder used to run four scaled 32-bit LUT loads, then reverse the four
 * 2-bit pixels of every output byte for the S31 panel. The page layout folds
 * both steps into the table, so this test pins it against the old pipeline:
 * build a waveform, convert a line with the new code and compare every output
 * byte with the byte-at-a-time reference that does old order + reversal.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lut_1ppb_pages.h"

#define CHECK(x)                                                                         \
    do {                                                                                 \
        if (!(x)) {                                                                       \
            fprintf(stderr, "lut_1ppb:%d: %s\n", __LINE__, #x);                           \
            exit(1);                                                                     \
        }                                                                                \
    } while (0)

#define WIDTH 1216u
#define FRAMES 8

static uint32_t s_pixels[WIDTH / 4]; /* 4-byte aligned, as the feeder's line is */
#define PIXELS ((uint8_t*)s_pixels)

/** Phase stored for one (to, from) pair of a packed waveform block. */
static unsigned phase_at(const uint8_t* waveform, int frame, unsigned index) {
    const unsigned to = index >> 4, from = index & 0x0Fu;
    const uint8_t packed = waveform[64 * frame + to * 4 + from / 4];
    return (packed >> (6 - 2 * (from & 3u))) & 3u;
}

/** What the old code produced: four lookups in S3 order, then the byte reversal. */
static uint8_t reference_byte(const uint8_t* waveform, int frame, const uint8_t* pixels) {
    const uint8_t v0 = (uint8_t)phase_at(waveform, frame, pixels[0]);
    const uint8_t v1 = (uint8_t)phase_at(waveform, frame, pixels[1]);
    const uint8_t v2 = (uint8_t)phase_at(waveform, frame, pixels[2]);
    const uint8_t v3 = (uint8_t)phase_at(waveform, frame, pixels[3]);
    const uint8_t s3_order = (uint8_t)((v3 << 6) | (v2 << 4) | (v1 << 2) | v0);
    return (uint8_t)(((s3_order & 0x03u) << 6) | ((s3_order & 0x0Cu) << 2)
                     | ((s3_order & 0x30u) >> 2) | ((s3_order & 0xC0u) >> 6));
}

/** Packed waveform with phase = (to * 4 + from * 3) & 3, all 256 entries used. */
static void fill_waveform(uint8_t* waveform) {
    for (int frame = 0; frame < FRAMES; ++frame) {
        for (unsigned to = 0; to < 16; ++to) {
            for (unsigned from_packed = 0; from_packed < 4; ++from_packed) {
                uint8_t packed = 0;
                for (unsigned k = 0; k < 4; ++k) {
                    const unsigned from = from_packed * 4 + k;
                    packed |= (uint8_t)(((to * 4 + from * 3) & 3u) << (6 - 2 * k));
                }
                waveform[64 * frame + to * 4 + from_packed] = packed;
            }
        }
    }
}

/** Deterministic byte source; MSVC's rand() only has 15 bits. */
static uint32_t s_seed = 0x1BBu;
static uint8_t next_byte(void) {
    s_seed = s_seed * 1103515245u + 12345u;
    return (uint8_t)(s_seed >> 16);
}

static void check_line(const uint8_t* waveform, const uint8_t* table, uint32_t width, int frame) {
    static uint8_t out[WIDTH / 4];
    epd_1ppb_convert(s_pixels, out, table, width);
    const unsigned bytes = width >> 2;
    for (unsigned j = 0; j < bytes; ++j) {
        const uint8_t want = reference_byte(waveform, frame, PIXELS + 4 * j);
        if (out[j] != want) {
            fprintf(stderr, "lut_1ppb:%d: frame %d width %u byte %u: got %02x want %02x\n",
                    __LINE__, frame, (unsigned)width, j, out[j], want);
            exit(1);
        }
    }
}

int main(void) {
    static uint8_t waveform[64 * FRAMES];
    static uint8_t table[EPD_1PPB_LUT_BYTES];

    fill_waveform(waveform);

    for (int frame = 0; frame < FRAMES; ++frame) {
        /* Unused bytes must not be trusted: poison, then build. */
        memset(table, 0xA5, sizeof(table));
        epd_1ppb_build_pages(table, waveform, frame);

        for (unsigned i = 0; i < WIDTH; ++i) {
            PIXELS[i] = next_byte();
        }
        check_line(waveform, table, WIDTH, frame);
        check_line(waveform, table, 4u, frame);
        check_line(waveform, table, 8u, frame);

        /* Whole-frame constants: 0x00 and 0xFF are the two (to == from) states. */
        memset(PIXELS, 0xFF, WIDTH);
        check_line(waveform, table, WIDTH, frame);
        memset(PIXELS, 0x00, WIDTH);
        check_line(waveform, table, WIDTH, frame);
    }

    /* Pixel order is what the panel sees: phase = to, so the first pixel of a
       group must land in bits 7:6 (pixel 0,1,2,3 -> phases 0,1,2,3 -> 0x1B). */
    {
        static uint8_t order_waveform[64];
        memset(order_waveform, 0, sizeof(order_waveform));
        for (unsigned to = 0; to < 16; ++to) {
            for (unsigned from_packed = 0; from_packed < 4; ++from_packed) {
                order_waveform[to * 4 + from_packed] = (uint8_t)(to * 0x55u);
            }
        }
        epd_1ppb_build_pages(table, order_waveform, 0);
        PIXELS[0] = 0x00u;
        PIXELS[1] = 0x10u;
        PIXELS[2] = 0x20u;
        PIXELS[3] = 0x30u;
        uint8_t out[4];
        epd_1ppb_convert(s_pixels, out, table, 4u);
        CHECK(out[0] == 0x1B);
        PIXELS[0] = 0x30u;
        PIXELS[1] = 0x20u;
        PIXELS[2] = 0x10u;
        PIXELS[3] = 0x00u;
        epd_1ppb_convert(s_pixels, out, table, 4u);
        CHECK(out[0] == 0xE4);
    }

    /* Equal white (to == from == 15) encodes the GC16 white-recovery action in
       bits 1:0 of page 3; the stagger test asserts on that byte. */
    {
        static uint8_t white_waveform[64];
        const unsigned index = (15u << 4) | 15u;
        memset(white_waveform, 0, sizeof(white_waveform));
        white_waveform[15 * 4 + 15 / 4] = (uint8_t)(2u << (6 - 2 * (index & 3u)));
        epd_1ppb_build_pages(table, white_waveform, 0);
        CHECK(table[index] == 2);
        CHECK(table[3 * EPD_1PPB_PAGE_SIZE + index] == (2u << 6));
    }

    /* Panel-order line_mask alignment (bug fix: button top/bottom borders at col%4==2..3
       and welcome button cleanup at col%4==0..1): reversing the 2-bit groups of line_mask
       when lut_output_panel_order is true ensures every single column col is gated by its
       own dirty bit, not the mirrored column (col ^ 3). */
    {
        static uint8_t mask_waveform[64];
        memset(mask_waveform, 0, sizeof(mask_waveform));
        for (unsigned to = 0; to < 16; ++to) {
            for (unsigned from_packed = 0; from_packed < 4; ++from_packed) {
                mask_waveform[to * 4 + from_packed] = (uint8_t)(to ? 0x55u : 0xAAu);
            }
        }
        epd_1ppb_build_pages(table, mask_waveform, 0);
        /* Model dirty 2px borders starting at col%4==0 (y=124/868) and col%4==2 (y=226/974/1006). */
        const unsigned test_cols[4] = {224, 225, 226, 227};
        for (unsigned t = 0; t < 4; ++t) {
            uint8_t dirty_columns[WIDTH / 2];
            memset(dirty_columns, 0, sizeof(dirty_columns));
            const unsigned col = test_cols[t];
            dirty_columns[col / 2] = (uint8_t)(15u << (4u * (col & 1u)));

            uint8_t line_mask[WIDTH / 4];
            for (unsigned c = 0; c < WIDTH / 2; c += 2) {
                uint8_t m = 0;
                m |= (dirty_columns[c + 1] & 0xF0u) ? 0xC0u : 0x00u;
                m |= (dirty_columns[c + 1] & 0x0Fu) ? 0x30u : 0x00u;
                m |= (dirty_columns[c] & 0xF0u) ? 0x0Cu : 0x00u;
                m |= (dirty_columns[c] & 0x0Fu) ? 0x03u : 0x00u;
                line_mask[c / 2] = m;
            }
            /* Exact permutation applied by render.c via epd_output_reverse_pixels(). */
            for (unsigned i = 0; i < WIDTH / 4; ++i) {
                const uint8_t m = line_mask[i];
                line_mask[i] = (uint8_t)(((m & 0x03u) << 6) | ((m & 0x0Cu) << 2)
                                         | ((m & 0x30u) >> 2) | ((m & 0xC0u) >> 6));
            }
            memset(PIXELS, 0x10u, WIDTH);
            uint8_t out[WIDTH / 4];
            epd_1ppb_convert(s_pixels, out, table, WIDTH);
            for (unsigned i = 0; i < WIDTH / 4; ++i) {
                out[i] &= line_mask[i];
            }
            for (unsigned x = 0; x < WIDTH; ++x) {
                const unsigned shift = 6u - 2u * (x & 3u);
                const unsigned got = (out[x >> 2] >> shift) & 3u;
                CHECK(got == (x == col ? 1u : 0u));
            }
        }
    }

    puts("PASS: 1ppB page lookup matches the old lookup + pixel reversal for 8 frames, "
         "1216/8/4-pixel lines, random and constant input, panel pixel order, white action, panel-order line_mask");
    return 0;
}
