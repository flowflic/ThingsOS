// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "thingsos/graphics.h"
#define THINGSOS_THUMBNAIL_SLOTS 24
#define THINGSOS_THUMBNAIL_BYTES (512u * 1024u)
typedef struct {
    bool valid;
    uint64_t key, used;
    unsigned width, height;
    unsigned char* pixels; /* NULL is a cached missing/invalid cover, not white pixels. */
    uint64_t version; /* Optional source fingerprint; zero means untagged. */
} thingsos_thumbnail_t;
typedef struct {
    thingsos_thumbnail_t slots[THINGSOS_THUMBNAIL_SLOTS];
    size_t bytes;
    uint64_t clock;
    unsigned hits, misses;
    /* Optional malloc-compatible allocator for host failure-path testing. */
    void* (*allocate)(size_t);
} thingsos_thumbnail_cache_t;
void thingsos_thumbnail_clear(thingsos_thumbnail_cache_t*);
void thingsos_thumbnail_version(thingsos_thumbnail_cache_t*,uint64_t,unsigned,unsigned,uint64_t);
const thingsos_thumbnail_t* thingsos_thumbnail_find(thingsos_thumbnail_cache_t*, uint64_t,
                                                   unsigned width, unsigned height);
/* Source is owned by caller. NULL records a negative result. Allocation failure
   returns NULL and does not incorrectly cache a missing cover. */
const thingsos_thumbnail_t* thingsos_thumbnail_store(thingsos_thumbnail_cache_t*, uint64_t,
    unsigned width, unsigned height, const unsigned char* source, unsigned sw, unsigned sh);
void thingsos_thumbnail_paint(const thingsos_thumbnail_t*, thingsos_canvas_t*, int x, int y);
