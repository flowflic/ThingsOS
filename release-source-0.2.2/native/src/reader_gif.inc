// SPDX-License-Identifier: Apache-2.0
/* Iterative, PSRAM-heap-allocated first-frame GIF decoder.
   Avoids stb_image's ~35 KiB stack struct and recursive LZW emission, which
   overflowed ESP32-S31's 24 KiB main task stack on inline GIFs. */
typedef struct {
    int16_t prefix;
    uint8_t first;
    uint8_t suffix;
} reader_gif_lzw_t;

typedef struct {
    uint8_t pal[256];
    reader_gif_lzw_t codes[4096];
    uint8_t emit[4096];
} reader_gif_work_t;

typedef struct {
    unsigned sw, sh, ow, oh, step;
    unsigned ix, iy, iw, ih;
    unsigned cur_x, cur_y, step_y, pass;
    int transp;
    bool interlaced, frame_done;
    uint8_t* output;
    const uint8_t* pal;
} reader_gif_raster_t;

static unsigned reader_gif_u16(const unsigned char* p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static uint8_t reader_gif_gray(uint8_t r, uint8_t g, uint8_t b) {
    return (uint8_t)(((unsigned)r * 77u + (unsigned)g * 150u + (unsigned)b * 29u) >> 8);
}

static bool reader_gif_is_gif(const unsigned char* data, size_t bytes) {
    return data && bytes >= 10 &&
        (!memcmp(data, "GIF87a", 6) || !memcmp(data, "GIF89a", 6));
}

static bool reader_gif_info(
    const unsigned char* data, size_t bytes, unsigned* width, unsigned* height) {
    if (!reader_gif_is_gif(data, bytes) || !width || !height) return false;
    unsigned w = reader_gif_u16(data + 6);
    unsigned h = reader_gif_u16(data + 8);
    if (!w || !h || w > 4096u || h > 4096u || (uint64_t)w * h > 2u * 1024u * 1024u)
        return false;
    *width = w;
    *height = h;
    return true;
}

static void reader_gif_put(reader_gif_raster_t* r, uint8_t idx) {
    if (r->frame_done) return;
    unsigned px = r->ix + r->cur_x;
    unsigned py = r->iy + r->cur_y;
    if ((int)idx != r->transp && px < r->sw && py < r->sh &&
        (px % r->step == 0) && (py % r->step == 0)) {
        r->output[(size_t)(py / r->step) * r->ow + (px / r->step)] = r->pal[idx];
    }
    ++r->cur_x;
    if (r->cur_x >= r->iw) {
        r->cur_x = 0;
        r->cur_y += r->step_y;
        while (r->interlaced && r->cur_y >= r->ih && r->pass < 3u) {
            ++r->pass;
            r->cur_y = r->pass == 1u ? 4u : r->pass == 2u ? 2u : 1u;
            r->step_y = r->pass == 1u ? 8u : r->pass == 2u ? 4u : 2u;
        }
        if (r->cur_y >= r->ih) r->frame_done = true;
    }
}

static bool reader_gif_lzw(
    const unsigned char* data, size_t bytes, size_t pos,
    reader_gif_work_t* work, reader_gif_raster_t* r) {
    if (pos >= bytes) return false;
    unsigned lzw_cs = data[pos++];
    if (lzw_cs < 1u || lzw_cs > 11u) return false;
    unsigned clear = 1u << lzw_cs;
    unsigned eoi = clear + 1u;
    unsigned avail = clear + 2u;
    unsigned codesize = lzw_cs + 1u;
    unsigned codemask = (1u << codesize) - 1u;
    for (unsigned i = 0; i < clear; ++i) {
        work->codes[i].prefix = -1;
        work->codes[i].first = (uint8_t)i;
        work->codes[i].suffix = (uint8_t)i;
    }
    int32_t oldcode = -1;
    uint32_t bits = 0;
    unsigned valid_bits = 0;
    unsigned sub_left = 0;
    bool eoi_seen = false;
    for (;;) {
        while (valid_bits < codesize) {
            if (sub_left == 0u) {
                if (pos >= bytes) return false;
                sub_left = data[pos++];
                if (sub_left == 0u) return r->frame_done || eoi_seen;
                if ((size_t)sub_left > bytes - pos) return false;
            }
            bits |= (uint32_t)data[pos++] << valid_bits;
            valid_bits += 8u;
            --sub_left;
        }
        unsigned code = (unsigned)(bits & codemask);
        bits >>= codesize;
        valid_bits -= codesize;
        if (code == clear) {
            codesize = lzw_cs + 1u;
            codemask = (1u << codesize) - 1u;
            avail = clear + 2u;
            oldcode = -1;
            continue;
        }
        if (code == eoi) {
            eoi_seen = true;
            return true;
        }
        if (code > avail || code >= 4096u) return false;
        if (oldcode < 0) {
            if (code >= clear) return false;
            reader_gif_put(r, work->codes[code].suffix);
            oldcode = (int32_t)code;
            continue;
        }
        bool kwkwk = (code == avail);
        int32_t walk = kwkwk ? oldcode : (int32_t)code;
        unsigned len = 0;
        while (walk >= 0) {
            if (len >= 4096u) return false;
            work->emit[len++] = work->codes[walk].suffix;
            walk = work->codes[walk].prefix;
        }
        uint8_t first_char = work->emit[len - 1u];
        while (len) reader_gif_put(r, work->emit[--len]);
        if (kwkwk) reader_gif_put(r, work->codes[oldcode].first);
        if (avail < 4096u) {
            work->codes[avail].prefix = (int16_t)oldcode;
            work->codes[avail].first = work->codes[oldcode].first;
            work->codes[avail].suffix = first_char;
            ++avail;
            if ((avail & codemask) == 0u && avail < 4096u && codesize < 12u) {
                ++codesize;
                codemask = (1u << codesize) - 1u;
            }
        }
        oldcode = (int32_t)code;
    }
}

static bool reader_gif_decode(
    const unsigned char* data, size_t bytes, size_t max_pixels, bool exact,
    unsigned char** gray, unsigned* width, unsigned* height) {
    if (gray) *gray = NULL;
    if (!reader_gif_is_gif(data, bytes) || !gray || !width || !height || bytes < 13u || !max_pixels)
        return false;
    unsigned sw = reader_gif_u16(data + 6);
    unsigned sh = reader_gif_u16(data + 8);
    if (!sw || !sh || sw > 4096u || sh > 4096u) return false;
    if (exact && (uint64_t)sw * sh > max_pixels) return false;
    unsigned step = 1u;
    while (!exact && (uint64_t)((sw + step - 1u) / step) * ((sh + step - 1u) / step) > max_pixels)
        step *= 2u;
    unsigned ow = (sw + step - 1u) / step;
    unsigned oh = (sh + step - 1u) / step;
    if (!ow || !oh || (uint64_t)ow * oh > max_pixels) return false;

    uint8_t gpal[256];
    memset(gpal, 0, sizeof(gpal));
    size_t pos = 13u;
    uint8_t flags = data[10];
    if (flags & 0x80u) {
        unsigned n = 1u << ((flags & 7u) + 1u);
        if ((size_t)n * 3u > bytes - pos) return false;
        for (unsigned i = 0; i < n; ++i)
            gpal[i] = reader_gif_gray(data[pos + i * 3u], data[pos + i * 3u + 1u], data[pos + i * 3u + 2u]);
        pos += (size_t)n * 3u;
    }

    int transp = -1;
    unsigned ix = 0, iy = 0, iw = 0, ih = 0;
    uint8_t lflags = 0;
    bool found_image = false;
    while (pos < bytes) {
        uint8_t tag = data[pos++];
        if (tag == 0x3Bu) return false;
        if (tag == 0x21u) {
            if (pos >= bytes) return false;
            uint8_t ext = data[pos++];
            if (ext == 0xF9u) {
                if (pos >= bytes) return false;
                uint8_t sz = data[pos++];
                if (sz > bytes - pos) return false;
                if (sz >= 4u)
                    transp = (data[pos] & 1u) ? (int)data[pos + 3u] : -1;
                pos += sz;
            }
            for (;;) {
                if (pos >= bytes) return false;
                uint8_t sub = data[pos++];
                if (!sub) break;
                if (sub > bytes - pos) return false;
                pos += sub;
            }
        } else if (tag == 0x2Cu) {
            if (bytes - pos < 9u) return false;
            ix = reader_gif_u16(data + pos);
            iy = reader_gif_u16(data + pos + 2u);
            iw = reader_gif_u16(data + pos + 4u);
            ih = reader_gif_u16(data + pos + 6u);
            lflags = data[pos + 8u];
            pos += 9u;
            if (!iw || !ih || iw > 16384u || ih > 16384u) return false;
            found_image = true;
            break;
        } else {
            return false;
        }
    }
    if (!found_image) return false;

    if (decode_arena) {
        decode_arena_used = 0;
        memset(&arena_slots, 0, sizeof(arena_slots));
    }
    reader_gif_work_t* work = image_alloc(sizeof(*work));
    unsigned char* output = work ? image_alloc((size_t)ow * oh) : NULL;
    if (!work || !output) {
        image_free(output);
        image_free(work);
        if (decode_arena) {
            decode_arena_used = 0;
            memset(&arena_slots, 0, sizeof(arena_slots));
        }
        return false;
    }
    memset(output, 255, (size_t)ow * oh);
    memcpy(work->pal, gpal, sizeof(gpal));
    if (lflags & 0x80u) {
        unsigned n = 1u << ((lflags & 7u) + 1u);
        if ((size_t)n * 3u > bytes - pos) {
            image_free(output);
            image_free(work);
            if (decode_arena) {
                decode_arena_used = 0;
                memset(&arena_slots, 0, sizeof(arena_slots));
            }
            return false;
        }
        for (unsigned i = 0; i < n; ++i)
            work->pal[i] = reader_gif_gray(data[pos + i * 3u], data[pos + i * 3u + 1u], data[pos + i * 3u + 2u]);
        pos += (size_t)n * 3u;
    }

    reader_gif_raster_t r = {
        .sw = sw, .sh = sh, .ow = ow, .oh = oh, .step = step,
        .ix = ix, .iy = iy, .iw = iw, .ih = ih,
        .cur_x = 0, .cur_y = 0, .step_y = (lflags & 0x40u) ? 8u : 1u, .pass = 0,
        .transp = transp, .interlaced = (lflags & 0x40u) != 0, .frame_done = false,
        .output = output, .pal = work->pal,
    };
    bool ok = reader_gif_lzw(data, bytes, pos, work, &r);
    image_free(work);
    if (!ok) {
        image_free(output);
        if (decode_arena) {
            decode_arena_used = 0;
            memset(&arena_slots, 0, sizeof(arena_slots));
        }
        return false;
    }
    if (decode_arena) {
        unsigned char* copy = thingsos_image_buffer((size_t)ow * oh);
        if (!copy) {
            decode_arena_used = 0;
            memset(&arena_slots, 0, sizeof(arena_slots));
            return false;
        }
        memcpy(copy, output, (size_t)ow * oh);
        decode_arena_used = 0;
        memset(&arena_slots, 0, sizeof(arena_slots));
        output = copy;
    } else {
        image_detach(output);
    }
    *gray = output;
    *width = ow;
    *height = oh;
    return true;
}
