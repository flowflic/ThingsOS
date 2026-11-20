// SPDX-License-Identifier: Apache-2.0
#include "thingsos/reader.h"
#include <limits.h>

static unsigned utf8(const unsigned char* p, size_t left, uint32_t* cp) {
    if (!left) return 0;
    unsigned n;
    uint32_t v, min;
    if (p[0] < 128) {
        *cp = p[0];
        return 1;
    }
    if (p[0] >= 0xc2 && p[0] <= 0xdf) {
        n = 2;
        v = p[0] & 31;
        min = 0x80;
    } else if (p[0] >= 0xe0 && p[0] <= 0xef) {
        n = 3;
        v = p[0] & 15;
        min = 0x800;
    } else if (p[0] >= 0xf0 && p[0] <= 0xf4) {
        n = 4;
        v = p[0] & 7;
        min = 0x10000;
    } else
        return 0;
    if (left < n) return 0;
    for (unsigned i = 1; i < n; i++) {
        if ((p[i] & 0xc0) != 0x80) return 0;
        v = (v << 6) | (p[i] & 63);
    }
    if (v < min || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) return 0;
    *cp = v;
    return n;
}
thingsos_text_result_t thingsos_txt_pages(
    const unsigned char* text, size_t length, unsigned columns, unsigned rows, size_t* starts,
    size_t cap, size_t* count) {
    if (count) *count = 0;
    if (!text || !starts || !count || !columns || !rows || !cap) return THINGSOS_TEXT_INVALID;
    size_t pos = 0, pages = 1;
    unsigned col = 0, row = 0;
    if (length >= 3 && text[0] == 0xef && text[1] == 0xbb && text[2] == 0xbf) pos = 3;
    starts[0] = pos;
    while (pos < length) {
        uint32_t cp;
        unsigned n = utf8(text + pos, length - pos, &cp);
        if (!n || cp == 0) return THINGSOS_TEXT_INVALID;
        unsigned cells = cp == '\t' ? 4 : (cp >= 0x1100 ? 2 : 1);
        if (cp != '\r' && cp != '\n' && cells > columns) return THINGSOS_TEXT_INVALID;
        if (cp != '\r' && cp != '\n' && col > columns - cells) {
            col = 0;
            ++row;
        }
        if (row >= rows) {
            if (pages >= cap) return THINGSOS_TEXT_CAPACITY;
            starts[pages++] = pos;
            row = 0;
            col = 0;
        }
        pos += n;
        if (cp == '\r' || cp == '\n') {
            if (cp == '\r' && pos < length && text[pos] == '\n') ++pos;
            ++row;
            col = 0;
        } else
            col += cells;
    }
    *count = pages;
    return THINGSOS_TEXT_OK;
}
bool thingsos_image_fit(uint32_t w, uint32_t h, uint32_t bw, uint32_t bh, thingsos_size_t* out) {
    if (!w || !h || !bw || !bh || !out) return false;
    if (w > bw || h > bh) return thingsos_image_contain(w, h, bw, bh, out);
    *out = (thingsos_size_t){w, h};
    return true;
}
bool thingsos_image_contain(uint32_t w, uint32_t h, uint32_t bw, uint32_t bh, thingsos_size_t* out) {
    if (!w || !h || !bw || !bh || !out) return false;
    uint32_t ow, oh;
    if ((uint64_t)w * bh > (uint64_t)h * bw) {
        ow = bw;
        oh = (uint32_t)((uint64_t)h * bw / w);
    } else {
        oh = bh;
        ow = (uint32_t)((uint64_t)w * bh / h);
    }
    out->width = ow ? ow : 1;
    out->height = oh ? oh : 1;
    return true;
}
bool thingsos_image_fit_width(uint32_t w,uint32_t h,uint32_t bw,thingsos_size_t* out) {
    if(!w || !h || !bw || !out)return false;
    uint64_t oh=(uint64_t)h*bw/w;if(oh>UINT32_MAX)return false;
    out->width=bw;out->height=oh?(uint32_t)oh:1;return true;
}
bool thingsos_image_resize(
    const thingsos_gray_t* s, uint8_t* d, size_t bytes, uint32_t w, uint32_t h) {
    if (!s || !s->pixels || !d || !s->width || !s->height || s->stride < s->width || !w || !h)
        return false;
    uint64_t need = (uint64_t)(s->height - 1) * s->stride + s->width;
    if (need > s->bytes || (uint64_t)w * h > bytes) return false;
    for (uint32_t y = 0; y < h; y++) {
        uint32_t sy = (uint32_t)((uint64_t)y * s->height / h);
        for (uint32_t x = 0; x < w; x++) {
            uint32_t sx = (uint32_t)((uint64_t)x * s->width / w);
            d[(size_t)y * w + x] = s->pixels[(size_t)sy * s->stride + sx];
        }
    }
    return true;
}
