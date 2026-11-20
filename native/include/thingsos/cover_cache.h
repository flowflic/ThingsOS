// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "thingsos/graphics.h"
/* Rebuildable SD cache: original path/size/mtime identity, payload checksum,
   bounded stack I/O and atomic replacement. Never owns a full image buffer. */
uint64_t thingsos_cover_source_stamp(const char* path);
bool thingsos_cover_cache_load(const char* path,uint64_t stamp,thingsos_canvas_t* canvas,
                               int x,int y,unsigned width,unsigned height);
bool thingsos_cover_cache_save(const char* path,uint64_t stamp,const thingsos_canvas_t* canvas,
                               int x,int y,unsigned width,unsigned height);
