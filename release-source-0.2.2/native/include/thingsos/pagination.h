// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef struct { uint32_t chapter, offset; } thingsos_page_position_t;
/* Finds the containing page, including restored offsets inside a page. */
size_t thingsos_page_find(const thingsos_page_position_t*, size_t, uint32_t, uint32_t);
unsigned thingsos_book_percent(size_t, size_t);
size_t thingsos_page_percent(unsigned, size_t);
bool thingsos_page_cache_load(const char*, uint64_t, thingsos_page_position_t**, size_t*);
bool thingsos_page_cache_save(const char*, uint64_t, const thingsos_page_position_t*, size_t);
/* Only whole completed chapters are checkpointed. A partial index is never
   accepted by the full-index loader. The caller resumes at completed_chapters. */
bool thingsos_page_checkpoint_load(const char*, uint64_t, unsigned, thingsos_page_position_t**, size_t*, unsigned*);
bool thingsos_page_checkpoint_save(const char*, uint64_t, unsigned, const thingsos_page_position_t*, size_t, unsigned);
