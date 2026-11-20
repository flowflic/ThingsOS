// SPDX-License-Identifier: Apache-2.0
#include "thingsos/runtime.h"
#include "thingsos/touch.h"
#include "thingsos/keys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "Failed at %d: %s\n", __LINE__, #expr); exit(1); } } while (0)
int main(void) {
    thingsos_keys_t keys = {0};
    CHECK(thingsos_keys_step(&keys, true, 80, 1400, 0) == THINGSOS_KEY_NONE);
    CHECK(thingsos_keys_step(&keys, false, 80, 1400, 100) == THINGSOS_KEY_PREVIOUS);
    CHECK(thingsos_keys_step(&keys, true, 400, 1400, 200) == THINGSOS_KEY_NONE);
    CHECK(thingsos_keys_step(&keys, false, 400, 1400, 300) == THINGSOS_KEY_NEXT);
    CHECK(thingsos_keys_step(&keys, true, 240, 1400, 400) == THINGSOS_KEY_NONE);
    CHECK(thingsos_keys_step(&keys, false, 240, 1400, 600) == THINGSOS_KEY_MENU);
    /* The middle key goes home one second into the hold, without waiting for the
       release; the release itself then adds nothing. */
    CHECK(thingsos_keys_step(&keys, true, 240, 1400, 1000) == THINGSOS_KEY_NONE);
    CHECK(thingsos_keys_step(&keys, true, 240, 1400, 1999) == THINGSOS_KEY_NONE);
    CHECK(thingsos_keys_step(&keys, true, 240, 1400, 2000) == THINGSOS_KEY_HOME);
    CHECK(thingsos_keys_step(&keys, true, 240, 1400, 2600) == THINGSOS_KEY_NONE);
    CHECK(thingsos_keys_step(&keys, false, 240, 1400, 3000) == THINGSOS_KEY_NONE);
    /* A hold whose only sample arrives after the threshold still reports home. */
    CHECK(thingsos_keys_step(&keys, true, 240, 1400, 4000) == THINGSOS_KEY_NONE);
    CHECK(thingsos_keys_step(&keys, false, 240, 1400, 5000) == THINGSOS_KEY_HOME);
    /* Short presses stay MENU. */
    CHECK(thingsos_keys_step(&keys, true, 240, 1400, 6000) == THINGSOS_KEY_NONE);
    CHECK(thingsos_keys_step(&keys, false, 240, 1400, 6900) == THINGSOS_KEY_MENU);
    CHECK(thingsos_keys_step(&keys, true, 80, 1400, 7000) == THINGSOS_KEY_NONE);
    CHECK(thingsos_keys_step(&keys, true, 240, 1400, 7100) == THINGSOS_KEY_NONE);
    CHECK(thingsos_keys_step(&keys, false, 240, 1400, 7200) == THINGSOS_KEY_NONE);
    CHECK(thingsos_keys_step(&keys, true, 240, 1100, 8000) == THINGSOS_KEY_NONE);
    CHECK(thingsos_keys_step(&keys, false, 240, 1100, 8200) == THINGSOS_KEY_NONE);
    uint16_t tx = 0, ty = 0;
    uint8_t packet[15] = {0};
    CHECK(!thingsos_touch_primary(packet, &tx, &ty)); // zero-filled idle slots
    packet[3] = 2; packet[4] = 100; packet[5] = 4; packet[6] = 120;
    CHECK(!thingsos_touch_primary(packet, &tx, &ty)); // stale coordinates, count zero
    packet[2] = 1;
    CHECK(thingsos_touch_primary(packet, &tx, &ty));
    CHECK(tx == 612 && ty == 1144);
    packet[3] |= 0x40;
    CHECK(!thingsos_touch_primary(packet, &tx, &ty)); // UP, unused second slot zero
    packet[3] = 0x82;
    CHECK(thingsos_touch_primary(packet, &tx, &ty)); // moving contact
    packet[2] = 3;
    CHECK(!thingsos_touch_primary(packet, &tx, &ty)); // malformed count
    thingsos_runtime_t s;
    thingsos_runtime_init(&s);
    thingsos_runtime_navigate(&s, THINGSOS_READING, 1000);
    thingsos_runtime_tick(&s, 2000);
    CHECK(s.reading_ms == 1000);
    thingsos_runtime_overlay(&s, true, 2500);
    thingsos_runtime_tick(&s, 3500);
    CHECK(s.reading_ms == 1500);
    thingsos_runtime_overlay(&s, false, 4000);
    thingsos_runtime_tick(&s, 5000);
    CHECK(s.reading_ms == 2500);
    thingsos_runtime_navigate(&s, THINGSOS_SETTINGS, 5500);
    thingsos_runtime_tick(&s, 6500);
    CHECK(s.reading_ms == 3000);
    thingsos_runtime_navigate(&s, THINGSOS_READING, 7000);
    for (uint64_t t = 8000; t <= 308000; t += 1000) thingsos_runtime_tick(&s, t);
    CHECK(s.reading_ms == 303000);
    thingsos_runtime_activity(&s, 308500);
    thingsos_runtime_tick(&s, 309000);
    CHECK(s.reading_ms == 303500);
    thingsos_runtime_tick(&s, 400000); // no sleep time credited
    CHECK(s.reading_ms == 303500);
    thingsos_runtime_tick(&s, 100); // clock rollback rebases
    thingsos_runtime_tick(&s, 200);
    CHECK(s.reading_ms == 303600);
    CHECK(thingsos_runtime_toggle_mark(&s) == THINGSOS_MARK_INVALID);
    s.location = (thingsos_location_t){ .book_id=7, .chapter=2, .byte_offset=19 };
    CHECK(thingsos_runtime_toggle_mark(&s) == THINGSOS_MARK_ADDED);
    CHECK(thingsos_runtime_toggle_mark(&s) == THINGSOS_MARK_REMOVED);
    for (unsigned i=0; i<THINGSOS_BOOKMARK_LIMIT; ++i) {
        s.location.byte_offset = i;
        CHECK(thingsos_runtime_toggle_mark(&s) == THINGSOS_MARK_ADDED);
    }
    s.location.byte_offset = 100;
    CHECK(thingsos_runtime_toggle_mark(&s) == THINGSOS_MARK_FULL);
    s.location.byte_offset = 0;
    CHECK(thingsos_runtime_toggle_mark(&s) == THINGSOS_MARK_REMOVED);
    CHECK(s.bookmarks[0].byte_offset == 1);
    s.location.book_id = 8;
    CHECK(thingsos_runtime_toggle_mark(&s) == THINGSOS_MARK_ADDED);
    CHECK(thingsos_progress_percent(UINT32_MAX-1, UINT32_MAX) == 99);
    CHECK(thingsos_progress_percent(50, 100) == 50);
    CHECK(thingsos_progress_percent(101, 100) == 100);
    CHECK(thingsos_progress_percent(0, 0) == 0);
    CHECK(thingsos_user_file_allowed("/sdcard/books/中文.txt"));
    CHECK(!thingsos_user_file_allowed("/sdcard/../flash/Example.txt"));
    CHECK(!thingsos_user_file_allowed("/sdcard/books/./x.txt"));
    CHECK(!thingsos_user_file_allowed("/sdcard/fonts//x.ttf"));
    CHECK(!thingsos_user_file_allowed("/sdcard/fonts\\x.ttf"));
    CHECK(!thingsos_user_file_allowed("/flash/Example.txt"));
    CHECK(!thingsos_user_file_allowed("/sdcard/"));
    CHECK(thingsos_font_allowed(THINGSOS_DEFAULT_FONT));
    CHECK(thingsos_font_allowed("/sdcard/fonts/其他字体.TTF"));
    CHECK(!thingsos_font_allowed("/sdcard/fontsevil/x.ttf"));
    CHECK(!thingsos_font_allowed("/sdcard/fonts/../books/x.ttf"));
    size_t bytes=0;
    CHECK(!memcmp(thingsos_example_text(&bytes), "Example Test", 12));
    CHECK(bytes == 12);
    puts("PASS: reading/idle/overlay/clock, bookmark boundaries, progress overflow, SD/font paths, read-only example");
    return 0;
}
