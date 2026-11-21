// SPDX-License-Identifier: Apache-2.0
#include "thingsos/audio_denoise.h"
#include <string.h>

void thingsos_audio_denoise_init(thingsos_audio_denoise_t* s,
                                thingsos_denoise_process_t process, void* context) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->process = process;
    s->context = context;
    s->skip = THINGSOS_DENOISE_HOP;
}

static bool run(thingsos_audio_denoise_t* s, int16_t* out, size_t* count) {
    if (!s->process(s->context, s->block, s->filtered)) {
        s->failed = true;
        return false;
    }
    size_t skip = s->skip < THINGSOS_DENOISE_HOP ? s->skip : THINGSOS_DENOISE_HOP;
    s->skip -= skip;
    size_t n = THINGSOS_DENOISE_HOP - skip;
    if (n > s->accepted - s->emitted) n = (size_t)(s->accepted - s->emitted);
    if (n) memcpy(out + *count, s->filtered + skip, n * sizeof(*out));
    *count += n;
    s->emitted += n;
    s->used = 0;
    return true;
}

bool thingsos_audio_denoise_feed(thingsos_audio_denoise_t* s, const int16_t* in,
                                size_t count, int16_t* out, size_t capacity, size_t* produced) {
    if (produced) *produced = 0;
    if (!s || !s->process || !produced || (!in && count) || !out ||
        s->failed || s->finished || count > SIZE_MAX - s->used ||
        count > UINT64_MAX - s->accepted) return false;
    size_t required = (s->used + count) / THINGSOS_DENOISE_HOP * THINGSOS_DENOISE_HOP;
    required -= s->skip < required ? s->skip : required;
    if (capacity < required) return false;
    s->accepted += count;
    while (count) {
        size_t n = THINGSOS_DENOISE_HOP - s->used;
        if (n > count) n = count;
        memcpy(s->block + s->used, in, n * sizeof(*in));
        s->used += n;
        in += n;
        count -= n;
        if (s->used == THINGSOS_DENOISE_HOP && !run(s, out, produced)) return false;
    }
    return true;
}

bool thingsos_audio_denoise_finish(thingsos_audio_denoise_t* s, int16_t* out,
                                  size_t capacity, size_t* produced) {
    if (produced) *produced = 0;
    if (!s || !s->process || !out || !produced || s->failed) return false;
    if (s->finished) return true;
    if (s->accepted - s->emitted > capacity) return false;
    /* At most a partial hop and the one-hop overlap tail remain. */
    for (unsigned i = 0; s->emitted < s->accepted && i < 2; ++i) {
        memset(s->block + s->used, 0, (THINGSOS_DENOISE_HOP - s->used) * sizeof(s->block[0]));
        if (!run(s, out, produced)) return false;
    }
    s->finished = s->emitted == s->accepted;
    return s->finished;
}
