// SPDX-License-Identifier: Apache-2.0
#ifndef THINGSOS_NATIVE_RUNTIME_H
#define THINGSOS_NATIVE_RUNTIME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define THINGSOS_BOOKMARK_LIMIT 32
#define THINGSOS_IDLE_MS 300000u
#define THINGSOS_DEFAULT_FONT "builtin:方正屏显雅宋.TTF"

typedef enum {
    THINGSOS_LIBRARY,
    THINGSOS_RECENT,
    THINGSOS_TRANSFER,
    THINGSOS_SETTINGS,
    THINGSOS_READING,
    THINGSOS_CHAPTERS,
    THINGSOS_FILES
} thingsos_page_t;
typedef struct {
    uint64_t book_id;
    uint32_t chapter;
    uint32_t byte_offset;
} thingsos_location_t;
typedef enum {
    THINGSOS_MARK_ADDED,
    THINGSOS_MARK_REMOVED,
    THINGSOS_MARK_FULL,
    THINGSOS_MARK_INVALID
} thingsos_mark_result_t;
typedef struct {
    thingsos_page_t page;
    bool reading_overlay;
    bool image_overlay;
    bool clock_ready;
    uint64_t last_tick_ms;
    uint64_t last_activity_ms;
    uint64_t reading_ms;
    thingsos_location_t location;
    thingsos_location_t bookmarks[THINGSOS_BOOKMARK_LIMIT];
    size_t bookmark_count;
    void (*reading_interval)(void*,uint64_t,uint32_t);
    void* reading_context;
} thingsos_runtime_t;

void thingsos_runtime_init(thingsos_runtime_t* state);
void thingsos_runtime_tick(thingsos_runtime_t* state, uint64_t now_ms);
void thingsos_runtime_navigate(thingsos_runtime_t* state, thingsos_page_t page, uint64_t now_ms);
void thingsos_runtime_overlay(thingsos_runtime_t* state, bool visible, uint64_t now_ms);
void thingsos_runtime_image_overlay(thingsos_runtime_t* state, bool visible, uint64_t now_ms);
void thingsos_runtime_activity(thingsos_runtime_t* state, uint64_t now_ms);
thingsos_mark_result_t thingsos_runtime_toggle_mark(thingsos_runtime_t* state);
unsigned thingsos_progress_percent(uint32_t byte_offset, uint32_t file_bytes);
bool thingsos_user_file_allowed(const char* path);
bool thingsos_font_allowed(const char* path);
const unsigned char* thingsos_example_text(size_t* bytes);
#endif
