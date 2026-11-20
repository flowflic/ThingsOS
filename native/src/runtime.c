// SPDX-License-Identifier: Apache-2.0
#include "thingsos/runtime.h"
#include <string.h>

void thingsos_runtime_init(thingsos_runtime_t* s) {
    if (s) {
        memset(s, 0, sizeof(*s));
        s->page = THINGSOS_LIBRARY;
    }
}

void thingsos_runtime_tick(thingsos_runtime_t* s, uint64_t now) {
    if (!s) return;
    if (!s->clock_ready || now < s->last_tick_ms) {
        s->last_tick_ms = now;
        s->last_activity_ms = now;
        s->clock_ready = true;
        return;
    }
    const uint64_t start = s->last_tick_ms;
    s->last_tick_ms = now;
    // A long scheduling gap may be sleep or a blocked task; never count it.
    if (now - start > 5000 || s->page != THINGSOS_READING ||
        (s->reading_overlay && !s->image_overlay))
        return;
    // Clip at the idle boundary rather than counting an entire crossing tick.
    const uint64_t idle_elapsed = start - s->last_activity_ms;
    if (idle_elapsed >= THINGSOS_IDLE_MS) return;
    const uint64_t remaining = THINGSOS_IDLE_MS - idle_elapsed;
    const uint64_t elapsed = now - start;
    uint32_t accepted=(uint32_t)(elapsed < remaining ? elapsed : remaining);
    s->reading_ms += accepted;
    if(accepted && s->reading_interval)s->reading_interval(s->reading_context,start,accepted);
}

void thingsos_runtime_activity(thingsos_runtime_t* s, uint64_t now) {
    if (!s) return;
    thingsos_runtime_tick(s, now);
    s->last_activity_ms = now;
}

void thingsos_runtime_navigate(thingsos_runtime_t* s, thingsos_page_t page, uint64_t now) {
    if (!s || page < THINGSOS_LIBRARY || page > THINGSOS_FILES) return;
    thingsos_runtime_activity(s, now);
    s->page = page;
    s->reading_overlay = false;
    s->image_overlay = false;
}

void thingsos_runtime_overlay(thingsos_runtime_t* s, bool visible, uint64_t now) {
    if (!s) return;
    thingsos_runtime_activity(s, now);
    s->reading_overlay = visible;
    s->image_overlay = false;
}

static bool same_location(thingsos_location_t a, thingsos_location_t b) {
    return a.book_id == b.book_id && a.chapter == b.chapter && a.byte_offset == b.byte_offset;
}

thingsos_mark_result_t thingsos_runtime_toggle_mark(thingsos_runtime_t* s) {
    if (!s || !s->location.book_id || s->bookmark_count > THINGSOS_BOOKMARK_LIMIT)
        return THINGSOS_MARK_INVALID;
    for (size_t i = 0; i < s->bookmark_count; ++i) {
        if (!same_location(s->bookmarks[i], s->location)) continue;
        memmove(
            &s->bookmarks[i], &s->bookmarks[i + 1],
            (s->bookmark_count - i - 1) * sizeof(s->bookmarks[0]));
        --s->bookmark_count;
        memset(&s->bookmarks[s->bookmark_count], 0, sizeof(s->bookmarks[0]));
        return THINGSOS_MARK_REMOVED;
    }
    if (s->bookmark_count == THINGSOS_BOOKMARK_LIMIT) return THINGSOS_MARK_FULL;
    s->bookmarks[s->bookmark_count++] = s->location;
    return THINGSOS_MARK_ADDED;
}

unsigned thingsos_progress_percent(uint32_t offset, uint32_t bytes) {
    if (!bytes) return 0;
    if (offset >= bytes) return 100;
    return (unsigned)((uint64_t)offset * 100u / bytes);
}

bool thingsos_user_file_allowed(const char* path) {
    if (!path || strncmp(path, "/sdcard/", 8) || !path[8]) return false;
    const char* part = path + 8;
    for (const char* p = part;; ++p) {
        if (*p == '\\' || ((unsigned char)*p < 32u && *p)) return false;
        if (*p != '/' && *p) continue;
        size_t len = (size_t)(p - part);
        if (!len || (len == 1 && part[0] == '.') || (len == 2 && part[0] == '.' && part[1] == '.'))
            return false;
        if (!*p) return true;
        part = p + 1;
    }
}

bool thingsos_font_allowed(const char* path) {
    if (!path) return false;
    if (!strcmp(path, THINGSOS_DEFAULT_FONT)) return true;
    if (strncmp(path, "/sdcard/fonts/", 14) || !thingsos_user_file_allowed(path)) return false;
    const size_t len = strlen(path);
    if (len < 18) return false;
    const char* ext = path + len - 4;
    return !strcmp(ext, ".ttf") || !strcmp(ext, ".TTF");
}

const unsigned char* thingsos_example_text(size_t* bytes) {
    static const unsigned char text[] = "Example Test";
    if (bytes) *bytes = sizeof(text) - 1;
    return text;
}

void thingsos_runtime_image_overlay(thingsos_runtime_t* s, bool visible, uint64_t now) {
    if (!s) return;
    thingsos_runtime_activity(s, now);
    s->reading_overlay = visible;
    s->image_overlay = visible;
}
