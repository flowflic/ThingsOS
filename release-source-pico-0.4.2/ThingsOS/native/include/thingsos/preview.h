// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
typedef struct {
    char* text;
    char title[160];
    size_t chapter, chapters, offset, next;
} thingsos_preview_t;
/* Bounded plain-text pages; no source HTML/scripts are returned. Caller owns I/O. */
bool thingsos_preview_read(const char*,size_t chapter,size_t offset,thingsos_preview_t*);
void thingsos_preview_close(thingsos_preview_t*);
