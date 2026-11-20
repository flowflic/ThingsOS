// SPDX-License-Identifier: Apache-2.0
#include "thingsos/thumbnail_cache.h"
#include "thingsos/reader.h"
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
static void* allocate(size_t bytes) {
#ifdef ESP_PLATFORM
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return malloc(bytes);
#endif
}
static void drop(thingsos_thumbnail_cache_t* c, thingsos_thumbnail_t* t) {
    if (t->pixels) c->bytes -= (size_t)t->width * t->height;
    free(t->pixels);
    memset(t, 0, sizeof(*t));
}
void thingsos_thumbnail_clear(thingsos_thumbnail_cache_t* c) {
    if (!c) return;
    for (unsigned i = 0; i < THINGSOS_THUMBNAIL_SLOTS; ++i) drop(c, &c->slots[i]);
    c->clock = 0;
}
void thingsos_thumbnail_version(thingsos_thumbnail_cache_t* c,uint64_t key,unsigned w,unsigned h,uint64_t version) {
    if(!c)return;
    for(unsigned i=0;i<THINGSOS_THUMBNAIL_SLOTS;++i) {
        thingsos_thumbnail_t* t=&c->slots[i];
        if(t->valid && t->key==key && t->width==w && t->height==h)t->version=version;
    }
}
static thingsos_thumbnail_t* oldest(thingsos_thumbnail_cache_t* c) {
    thingsos_thumbnail_t* result = NULL;
    for (unsigned i = 0; i < THINGSOS_THUMBNAIL_SLOTS; ++i) {
        thingsos_thumbnail_t* t = &c->slots[i];
        if (t->valid && (!result || t->used < result->used)) result = t;
    }
    return result;
}
const thingsos_thumbnail_t* thingsos_thumbnail_find(thingsos_thumbnail_cache_t* c, uint64_t key,
                                                   unsigned w, unsigned h) {
    if (!c) return NULL;
    for (unsigned i = 0; i < THINGSOS_THUMBNAIL_SLOTS; ++i) {
        thingsos_thumbnail_t* t = &c->slots[i];
        if (t->valid && t->key == key && t->width == w && t->height == h) {
            t->used = ++c->clock;
            ++c->hits;
            return t;
        }
    }
    ++c->misses;
    return NULL;
}
const thingsos_thumbnail_t* thingsos_thumbnail_store(thingsos_thumbnail_cache_t* c, uint64_t key,
    unsigned w, unsigned h, const unsigned char* src, unsigned sw, unsigned sh) {
    if (!c || !w || !h || w > THINGSOS_THUMBNAIL_BYTES / h) return NULL;
    thingsos_size_t fit;
    if (src && (!sw || !sh || sw > SIZE_MAX / sh ||
                !thingsos_image_fit(sw, sh, w, h, &fit))) return NULL;
    /* Replace only this key/size; other sizes remain useful in another view. */
    for (unsigned i = 0; i < THINGSOS_THUMBNAIL_SLOTS; ++i) {
        thingsos_thumbnail_t* t = &c->slots[i];
        if (t->valid && t->key == key && t->width == w && t->height == h) drop(c, t);
    }
    size_t bytes = src ? (size_t)w * h : 0;
    while (c->bytes + bytes > THINGSOS_THUMBNAIL_BYTES) {
        thingsos_thumbnail_t* t = oldest(c);
        if (!t) return NULL;
        drop(c, t);
    }
    thingsos_thumbnail_t* slot = NULL;
    for (unsigned i = 0; i < THINGSOS_THUMBNAIL_SLOTS; ++i)
        if (!c->slots[i].valid) { slot = &c->slots[i]; break; }
    if (!slot) { slot = oldest(c); drop(c, slot); }
    unsigned char* pixels = NULL;
    if (src) {
        pixels = c->allocate ? c->allocate(bytes) : allocate(bytes);
        if (!pixels) return NULL;
        memset(pixels, 255, bytes);
        unsigned left = (w - fit.width) / 2, top = (h - fit.height) / 2;
        for (unsigned y = 0; y < fit.height; ++y) {
            const unsigned char* row = src + (size_t)((uint64_t)y * sh / fit.height) * sw;
            unsigned char* dst = pixels + (size_t)(top + y) * w + left;
            for (unsigned x = 0; x < fit.width; ++x)
                dst[x] = row[(unsigned)((uint64_t)x * sw / fit.width)];
        }
    }
    *slot = (thingsos_thumbnail_t){ true, key, ++c->clock, w, h, pixels, 0 };
    c->bytes += bytes;
    return slot;
}
void thingsos_thumbnail_paint(const thingsos_thumbnail_t* t, thingsos_canvas_t* c, int x, int y) {
    if (!t || !t->valid || !t->pixels || !c || !c->pixels || c->width <= 0 || c->height <= 0)
        return;
    for (unsigned row = 0; row < t->height; ++row) {
        int64_t yy = (int64_t)y + row;
        if (yy < 0 || yy >= c->height) continue;
        unsigned from = x < 0 ? (unsigned)(-(int64_t)x) : 0;
        if (from >= t->width || x >= c->width) continue;
        size_t n = t->width - from;
        size_t room = (size_t)((int64_t)c->width - x - from);
        if (n > room) n = room;
        memcpy(c->pixels + (size_t)yy * c->width + (size_t)((int64_t)x + from),
               t->pixels + (size_t)row * t->width + from, n);
    }
}
